/**
 * @file main.c
 * @brief ESP32 Rowing Monitor - Application Entry Point
 * 
 * This is the main application for the ESP32 Rowing Monitor.
 * It initializes all subsystems and runs the main application loop.
 * 
 * Features:
 * - GPIO interrupt handling for flywheel and seat sensors
 * - Physics-based rowing metrics calculations
 * - Bluetooth Low Energy FTMS service for fitness app integration
 * - WiFi web server with WebSocket for real-time metrics
 * - NVS persistent configuration storage
 * - Simple SoftAP + web-based WiFi setup (no ESP-IDF provisioning component)
 * 
 * Compatible with ESP-IDF 6.0+
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

// Application modules
#include "app_config.h"
#include "rowing_physics.h"
#include "sensor_manager.h"
#include "stroke_detector.h"
#include "metrics_calculator.h"
#include "ble_ftms_server.h"
#include "ble_hr_client.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "config_manager.h"
#include "session_manager.h"
#include "hr_receiver.h"
#include "dns_server.h"
#include "utils.h"
#include "hardware.h"

static const char *TAG = "MAIN";

// Global metrics and configuration
static rowing_metrics_t g_metrics;
static config_t g_config;

// Task handles
static TaskHandle_t metrics_task_handle = NULL;
static TaskHandle_t broadcast_task_handle = NULL;

// Task running flags
static volatile bool g_running = true;

/**
 * Print startup banner.
 *
 * Also logs the previous reset reason and detailed heap stats so that silent
 * crashes (which previously left no trail) are surfaced on the next boot.
 * If the device crashed mid-session, this is the only practical way to
 * diagnose the cause after the fact.
 */
static void print_banner(void) {
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "  ESP32 Rowing Monitor v%s", APP_VERSION_STRING);
    ESP_LOGI(TAG, "====================================");

    // Reset reason — the most important diagnostic for "device just crashed"
    esp_reset_reason_t reason = esp_reset_reason();
    const char *reason_str;
    switch (reason) {
        case ESP_RST_POWERON:    reason_str = "POWERON (cold boot)"; break;
        case ESP_RST_EXT:        reason_str = "EXT (external pin)"; break;
        case ESP_RST_SW:         reason_str = "SW (esp_restart called)"; break;
        case ESP_RST_PANIC:      reason_str = "PANIC (exception/abort) — CRASH!"; break;
        case ESP_RST_INT_WDT:    reason_str = "INT_WDT (interrupt watchdog) — CRASH!"; break;
        case ESP_RST_TASK_WDT:   reason_str = "TASK_WDT (task watchdog) — CRASH!"; break;
        case ESP_RST_WDT:        reason_str = "WDT (other watchdog) — CRASH!"; break;
        case ESP_RST_DEEPSLEEP:  reason_str = "DEEPSLEEP wakeup"; break;
        case ESP_RST_BROWNOUT:   reason_str = "BROWNOUT (under-voltage) — CRASH!"; break;
        case ESP_RST_SDIO:       reason_str = "SDIO"; break;
        default:                 reason_str = "UNKNOWN"; break;
    }
    ESP_LOGI(TAG, "Reset reason: %s (%d)", reason_str, (int)reason);

    if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
        reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
        reason == ESP_RST_BROWNOUT) {
        ESP_LOGW(TAG, "*** Previous boot ended in a crash. Check serial log /");
        ESP_LOGW(TAG, "*** coredump partition for the panic backtrace.");
    }

    // Detailed heap stats — fragmentation & largest free block are the best
    // early-warning indicators of the kind of slow leak that can take down
    // a long-running session.
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free = esp_get_minimum_free_heap_size();
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "Heap: free=%u min_free=%u largest_block=%u",
             (unsigned)free_heap, (unsigned)min_free, (unsigned)largest_block);
}

/**
 * Periodic heap monitor (called from metrics task).
 *
 * Logs heap stats on a long interval and explicitly warns when the heap is
 * trending towards exhaustion or fragmentation. Catches slow leaks that
 * eventually take down the session save.
 */
static void monitor_heap(void) {
    static uint32_t monitor_counter = 0;
    monitor_counter++;
    // metrics task runs at 10Hz, log every 60 seconds (600 ticks)
    if (monitor_counter >= 600) {
        monitor_counter = 0;
        size_t free_heap = esp_get_free_heap_size();
        size_t min_free = esp_get_minimum_free_heap_size();
        size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);

        ESP_LOGI(TAG, "Heap monitor: free=%u min=%u largest_block=%u",
                 (unsigned)free_heap, (unsigned)min_free, (unsigned)largest_block);

        // Per-task stack high water marks. If any task has come within ~256
        // bytes of overflowing its stack we surface a warning while the
        // device is still alive — this is the earliest possible signal of an
        // imminent stack-overflow crash.
        TaskHandle_t self = xTaskGetCurrentTaskHandle();
        UBaseType_t self_hw = uxTaskGetStackHighWaterMark(self);
        ESP_LOGI(TAG, "Stack high watermark: metrics=%u",
                 (unsigned)self_hw);
        if (self_hw < STACK_LOW_WARN_BYTES) {
            ESP_LOGW(TAG, "STACK NEAR OVERFLOW (metrics task): %u bytes free",
                     (unsigned)self_hw);
        }
        if (broadcast_task_handle != NULL) {
            UBaseType_t b_hw = uxTaskGetStackHighWaterMark(broadcast_task_handle);
            ESP_LOGI(TAG, "Stack high watermark: broadcast=%u", (unsigned)b_hw);
            if (b_hw < STACK_LOW_WARN_BYTES) {
                ESP_LOGW(TAG, "STACK NEAR OVERFLOW (broadcast task): %u bytes free",
                         (unsigned)b_hw);
            }
        }

        if (free_heap < LOW_HEAP_WARN_BYTES) {
            ESP_LOGW(TAG, "LOW HEAP: %u bytes free — risk of allocation failure",
                     (unsigned)free_heap);
        }
        // If the largest free block is much smaller than total free heap, we're
        // fragmented and a large allocation (like the session JSON or NVS save)
        // will fail even though "enough" memory is technically available.
        if (free_heap > 0 && largest_block < free_heap / 4 && largest_block < HEAP_FRAGMENT_WARN_BYTES) {
            ESP_LOGW(TAG, "HEAP FRAGMENTED: free=%u largest_block=%u",
                     (unsigned)free_heap, (unsigned)largest_block);
        }
    }
}

/**
 * Metrics update task
 * Periodically updates derived metrics
 */
static void metrics_update_task(void *arg) {
    ESP_LOGI(TAG, "Metrics update task started");
    
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t update_period = pdMS_TO_TICKS(100);  // 10Hz update rate
    uint32_t sample_counter = 0;  // Counter for 1-second sample recording
    
    while (g_running) {
        // Update derived metrics
        metrics_calculator_update(&g_metrics, &g_config);
        
        // Calculate calories
        rowing_physics_calculate_calories(&g_metrics, g_config.user_weight_kg);
        
        // Check for auto-start/pause based on flywheel activity
        session_manager_check_activity(&g_metrics, &g_config);
        
        // Record per-second sample for graphs (every 10 updates = 1 second)
        sample_counter++;
        if (sample_counter >= 10) {
            sample_counter = 0;
            // Only record if session is active and not paused
            if (session_manager_get_current_session_id() > 0 && !g_metrics.is_paused) {
                uint8_t hr = hr_receiver_get_current();
                session_manager_record_sample(&g_metrics, hr);
            }
            // Refresh the optional display once per second.
            hardware_render_metrics(&g_metrics);

            // Periodic heap monitor — surfaces slow leaks before they crash a session
            monitor_heap();
        }
        
        vTaskDelayUntil(&last_wake_time, update_period);
    }
    
    ESP_LOGI(TAG, "Metrics update task stopped");
    vTaskDelete(NULL);
}

/**
 * Broadcast task
 * Sends metrics to BLE and WebSocket clients
 */
static void broadcast_task(void *arg) {
    ESP_LOGI(TAG, "Broadcast task started");
    
    uint32_t ble_counter = 0;
    uint32_t ws_counter = 0;
    const uint32_t ble_divisor = BLE_NOTIFY_INTERVAL_MS / 100;
    const uint32_t ws_divisor = WS_BROADCAST_INTERVAL_MS / 100;
    
    while (g_running) {
        ble_counter++;
        ws_counter++;
        
        bool need_ble = (ble_counter >= ble_divisor && g_config.ble_enabled);
        bool need_ws = (ws_counter >= ws_divisor && g_config.wifi_enabled);

        // Take a single atomic snapshot per tick if either send is needed.
        // Avoids two snapshots per iteration and ensures BLE & WS see the
        // same state.
        rowing_metrics_t snap;
        bool have_snap = false;
        if (need_ble || need_ws) {
            metrics_calculator_get_snapshot(&g_metrics, &snap);
            have_snap = true;
        }

        // Send BLE notification
        if (need_ble) {
            ble_counter = 0;
            if (ble_ftms_is_connected()) {
                ble_ftms_notify_metrics(&snap);
            }
        }
        
        // Send WebSocket broadcast
        if (need_ws) {
            ws_counter = 0;
            if (web_server_has_ws_clients()) {
                web_server_broadcast_metrics(&snap);
            }
        }
        (void)have_snap;
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "Broadcast task stopped");
    vTaskDelete(NULL);
}

/**
 * Initialize all subsystems
 */
static esp_err_t init_subsystems(void) {
    esp_err_t ret;
    bool provisioned = true;  // Assume provisioned unless WiFi says otherwise
    
    // Initialize NVS and load configuration
    ESP_LOGI(TAG, "Initializing configuration manager...");
    ret = config_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize config manager");
        return ret;
    }
    
    ret = config_manager_load(&g_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load config, using defaults");
        config_manager_get_defaults(&g_config);
    }
    
    // Initialize session manager
    ESP_LOGI(TAG, "Initializing session manager...");
    ret = session_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize session manager");
    }
    
    // Initialize heart rate receiver
    ESP_LOGI(TAG, "Initializing heart rate receiver...");
    ret = hr_receiver_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize heart rate receiver");
    }
    
    // Initialize metrics calculator
    ESP_LOGI(TAG, "Initializing metrics calculator...");
    metrics_calculator_init(&g_metrics, &g_config);
    
    // Initialize stroke detector
    ESP_LOGI(TAG, "Initializing stroke detector...");
    stroke_detector_init(&g_config);
    
    // Initialize sensor manager
    ESP_LOGI(TAG, "Initializing sensor manager...");
    ret = sensor_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sensor manager");
        return ret;
    }

    // Initialize optional hardware peripherals (display, touch, buttons,
    // encoder, audio, status LED) selected via Kconfig. Failures here are
    // non-fatal — the rower still works without any of them.
    ESP_LOGI(TAG, "Initializing optional hardware peripherals...");
    esp_err_t hw_ret = hardware_init(&g_metrics, &g_config);
    if (hw_ret != ESP_OK) {
        ESP_LOGW(TAG, "hardware_init reported 0x%x — continuing", hw_ret);
    }

    // Start sensor processing task
    ret = sensor_manager_start_task(&g_metrics, &g_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sensor task");
        return ret;
    }
    
    // Initialize WiFi if enabled
    if (g_config.wifi_enabled) {
        ESP_LOGI(TAG, "Initializing WiFi...");
        
        // Initialize the wifi_manager (simple approach - no network_provisioning component)
        ret = wifi_manager_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize WiFi manager");
            return ret;
        }
        
        // Check if STA credentials are configured
        provisioned = g_config.sta_configured && strlen(g_config.sta_ssid) > 0;
        
        ESP_LOGI(TAG, "WiFi config check: sta_configured=%d, sta_ssid='%s', provisioned=%d",
                 g_config.sta_configured, g_config.sta_ssid, provisioned);
        
        if (provisioned) {
            ESP_LOGI(TAG, "WiFi credentials configured - connecting to: %s", g_config.sta_ssid);
            
            // Try to connect to the configured network
            bool connected = wifi_manager_connect_sta_with_timeout(
                g_config.sta_ssid, 
                g_config.sta_password,
                30  // 30 second timeout
            );
            
            if (connected) {
                ESP_LOGI(TAG, "Connected to WiFi!");
                
                // Start web server after connection
                ESP_LOGI(TAG, "Starting web server...");
                ret = web_server_start(&g_metrics, &g_config);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start web server");
                    return ret;
                }
            } else {
                ESP_LOGW(TAG, "Could not connect to saved WiFi - starting AP mode for setup");
                provisioned = false;
            }
        }
        
        // If not provisioned or connection failed, start AP mode with web setup
        if (!provisioned) {
            ESP_LOGI(TAG, "Starting SoftAP for WiFi setup...");
            
            // Start simple SoftAP using wifi_manager
            ret = wifi_manager_start_ap(g_config.wifi_ssid, NULL);  // Open network
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start SoftAP");
                return ret;
            }
            
            // Wait for AP to fully initialize (same delay as DHCP server init)
            vTaskDelay(pdMS_TO_TICKS(WIFI_DHCP_INIT_DELAY_MS));
            
            // Start the web server in captive portal mode (pass config for WiFi credential saving)
            ESP_LOGI(TAG, "Starting web server (captive portal mode)...");
            ret = web_server_start_captive_portal(&g_config);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start web server");
                return ret;
            }
            
            // Start DNS server for captive portal redirect
            ESP_LOGI(TAG, "Starting DNS server...");
            esp_err_t dns_ret = dns_server_start("192.168.4.1");
            if (dns_ret != ESP_OK) {
                ESP_LOGW(TAG, "DNS server failed to start");
            }
            
            ESP_LOGI(TAG, "====================================");
            ESP_LOGI(TAG, "  WiFi Setup Mode");
            ESP_LOGI(TAG, "  Connect to: %s", g_config.wifi_ssid);
            ESP_LOGI(TAG, "  (Open network - no password)");
            ESP_LOGI(TAG, "  Then open http://192.168.4.1/setup");
            ESP_LOGI(TAG, "  to configure WiFi credentials");
            ESP_LOGI(TAG, "====================================");
        }
    }
    
    // Initialize BLE if enabled
    // NOTE: BLE scanning interferes with WiFi softAP on ESP32-S3 (shared 2.4GHz radio)
    // Only initialize BLE if we're NOT in AP setup mode (i.e., we have WiFi credentials)
    if (g_config.ble_enabled && provisioned) {
        ESP_LOGI(TAG, "Initializing BLE FTMS...");
        ret = ble_ftms_init(g_config.device_name);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize BLE");
            return ret;
        }
        
        ESP_LOGI(TAG, "BLE device name: %s", g_config.device_name);
        
        // Initialize BLE HR client to receive heart rate from watches/monitors
#if BLE_HR_CLIENT_ENABLED
        ESP_LOGI(TAG, "Initializing BLE HR client...");
        ret = ble_hr_client_init();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to initialize BLE HR client");
            // Don't return error, HR client is optional
        } else {
            // Start scanning for heart rate monitors
            ESP_LOGI(TAG, "Starting BLE HR monitor scan...");
            ble_hr_client_start_scan();
        }
#endif
    } else if (g_config.ble_enabled && !provisioned) {
        ESP_LOGI(TAG, "BLE disabled during WiFi setup (WiFi/BLE coexistence issue)");
        ESP_LOGI(TAG, "BLE will start automatically after WiFi is configured");
    }
    
    // Session is NOT auto-started on boot - user must press Start button in web UI
    // session_manager_start_session() is called via /workout/start API endpoint
    
    return ESP_OK;
}

/**
 * Start application tasks
 */
static void start_tasks(void) {
    // Create metrics update task
    xTaskCreate(
        metrics_update_task,
        "metrics_task",
        METRICS_TASK_STACK_SIZE,
        NULL,
        METRICS_TASK_PRIORITY,
        &metrics_task_handle
    );
    
    // Create broadcast task
    xTaskCreate(
        broadcast_task,
        "broadcast_task",
        BLE_TASK_STACK_SIZE,
        NULL,
        BLE_TASK_PRIORITY,
        &broadcast_task_handle
    );
}

/**
 * Main application entry point
 */
void app_main(void) {
    print_banner();
    
    // Initialize all subsystems
    esp_err_t ret = init_subsystems();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize subsystems, restarting in 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        utils_restart();
        return;
    }
    
    // Start application tasks
    start_tasks();
    
    ESP_LOGI(TAG, "Rowing Monitor initialized successfully");
    ESP_LOGI(TAG, "Waiting for rowing activity...");
    
    // Main loop - periodic status logging
    uint32_t loop_counter = 0;
    while (g_running) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // Log every 10 seconds
        
        loop_counter++;
        
        // Update the optional status indicator LED based on current state.
        hardware_update_indicator(&g_metrics,
                                  /* wifi_connected   */ true,
                                  /* ble_advertising  */ ble_ftms_is_connected(),
                                  /* hr_connected     */ hr_receiver_get_current() > 0);
        
        // Log status
        if (g_metrics.is_active) {
            ESP_LOGI(TAG, "Active: %lu strokes, %.1fm, SPM=%.1f, Power=%.0fW",
                     (unsigned long)g_metrics.stroke_count,
                     g_metrics.total_distance_meters,
                     g_metrics.stroke_rate_spm,
                     g_metrics.instantaneous_power_watts);
        } else {
            ESP_LOGD(TAG, "Idle (heap: %lu, min: %lu)",
                     (unsigned long)utils_get_free_heap(),
                     (unsigned long)utils_get_min_free_heap());
        }
        

    }
    
    // Cleanup (if we ever exit the loop)
    ESP_LOGI(TAG, "Shutting down...");
    
    g_running = false;
    
    // End session
    session_manager_end_session(&g_metrics);
    
    // Stop tasks
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Cleanup subsystems
    sensor_manager_stop_task();
    web_server_stop();
    wifi_manager_deinit();
#if BLE_HR_CLIENT_ENABLED
    ble_hr_client_deinit();
#endif
    ble_ftms_deinit();
    sensor_manager_deinit();
    hr_receiver_deinit();
    hardware_deinit();
    
    ESP_LOGI(TAG, "Shutdown complete");
}
