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
 * Print startup banner
 */
static void print_banner(void) {
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "  ESP32 Rowing Monitor v%s", APP_VERSION_STRING);
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)utils_get_free_heap());
}

/**
 * Metrics update task
 * Periodically updates derived metrics
 */
static void metrics_update_task(void *arg) {
    ESP_LOGI(TAG, "Metrics update task started");
    
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t update_period = pdMS_TO_TICKS(100);  // 10Hz update rate
    
    while (g_running) {
        // Update derived metrics
        metrics_calculator_update(&g_metrics, &g_config);
        
        // Calculate calories
        rowing_physics_calculate_calories(&g_metrics, g_config.user_weight_kg);
        
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
        
        // Send BLE notification
        if (ble_counter >= ble_divisor && g_config.ble_enabled) {
            ble_counter = 0;
            if (ble_ftms_is_connected()) {
                ble_ftms_notify_metrics(&g_metrics);
            }
        }
        
        // Send WebSocket broadcast
        if (ws_counter >= ws_divisor && g_config.wifi_enabled) {
            ws_counter = 0;
            if (web_server_has_ws_clients()) {
                web_server_broadcast_metrics(&g_metrics);
            }
        }
        
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
    
    // Enable debug logging for DNS server to help diagnose captive portal issues
    esp_log_level_set("DNS_SERVER", ESP_LOG_DEBUG);
    
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
    
    // Start sensor processing task
    ret = sensor_manager_start_task(&g_metrics, &g_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start sensor task");
        return ret;
    }
    
    // Initialize WiFi if enabled
    if (g_config.wifi_enabled) {
        ESP_LOGI(TAG, "Initializing WiFi manager...");
        ret = wifi_manager_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize WiFi");
            return ret;
        }
        
        // Check if STA credentials are configured
        bool sta_connected = false;
        if (g_config.sta_configured && strlen(g_config.sta_ssid) > 0) {
            ESP_LOGI(TAG, "Saved WiFi credentials found: %s", g_config.sta_ssid);
            ESP_LOGI(TAG, "Attempting to connect (60 second timeout)...");
            
            // Try to connect with 60 second timeout
            sta_connected = wifi_manager_connect_sta_with_timeout(
                g_config.sta_ssid, 
                g_config.sta_password, 
                60  // 60 second timeout
            );
            
            if (!sta_connected) {
                ESP_LOGW(TAG, "Could not connect to %s within 60 seconds", g_config.sta_ssid);
                ESP_LOGI(TAG, "Falling back to AP mode for WiFi provisioning");
            }
        } else {
            ESP_LOGI(TAG, "No saved WiFi credentials, starting in provisioning mode");
        }
        
        // Start WiFi in AP mode if STA not configured or connection failed
        if (!sta_connected) {
            ESP_LOGI(TAG, "Starting WiFi AP: %s", g_config.wifi_ssid);
            ret = wifi_manager_start_ap(g_config.wifi_ssid, g_config.wifi_password);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start WiFi AP");
                return ret;
            }
            
            // Start DNS server for captive portal (only in AP mode)
            ESP_LOGI(TAG, "Starting DNS server for captive portal...");
            esp_err_t dns_ret = dns_server_start("192.168.4.1");
            if (dns_ret != ESP_OK) {
                ESP_LOGW(TAG, "DNS server failed to start - captive portal may not work automatically");
            }
        }
        
        // Start web server
        ESP_LOGI(TAG, "Starting web server...");
        ret = web_server_start(&g_metrics, &g_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start web server");
            return ret;
        }
        
        // Print connection info
        char ip_str[16];
        wifi_manager_get_ip_string(ip_str, sizeof(ip_str));
        ESP_LOGI(TAG, "====================================");
        ESP_LOGI(TAG, "  Web interface: http://%s", ip_str);
        ESP_LOGI(TAG, "  Also available at: http://rower.local");
        if (sta_connected) {
            ESP_LOGI(TAG, "  Mode: Station (connected to %s)", g_config.sta_ssid);
        } else {
            ESP_LOGI(TAG, "  Mode: Access Point (Captive Portal)");
            ESP_LOGI(TAG, "  WiFi SSID: %s", g_config.wifi_ssid);
            ESP_LOGI(TAG, "  Direct access: http://192.168.4.1");
            ESP_LOGI(TAG, "  Setup page: http://%s/setup", ip_str);
        }
        ESP_LOGI(TAG, "====================================");
    }
    
    // Initialize BLE if enabled
    if (g_config.ble_enabled) {
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
        
        // Periodic memory check
        if (loop_counter % 6 == 0) {  // Every minute
            ESP_LOGI(TAG, "Memory: free=%lu, min=%lu",
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
    
    ESP_LOGI(TAG, "Shutdown complete");
}
