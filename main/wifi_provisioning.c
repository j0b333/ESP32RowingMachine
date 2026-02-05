/**
 * @file wifi_provisioning.c
 * @brief WiFi Provisioning using ESP-IDF v6.0 network_provisioning component
 * 
 * This module implements WiFi provisioning using the official ESP-IDF v6.0
 * network_provisioning component with softAP transport.
 * 
 * CRITICAL: SoftAP Configuration for AUTH_EXPIRE Fix
 * ==================================================
 * Mobile devices connecting to ESP32 SoftAP often disconnect with reason=2
 * (AUTH_EXPIRE) when the AP is not configured properly. Key fixes:
 * 
 * 1. Configure SoftAP BEFORE starting provisioning (not after)
 * 2. Use fixed channel (1) to avoid channel switching issues
 * 3. Use HT20 bandwidth (20MHz) for better device compatibility
 * 4. Disable PMF (Protected Management Frames) for OPEN networks
 * 5. Explicitly set authmode to WIFI_AUTH_OPEN
 * 
 * The provisioning manager will use the pre-configured WiFi settings
 * when starting, avoiding the need to modify settings after start
 * (which causes SoftAP restarts and client disconnections).
 * 
 * References:
 * - https://docs.espressif.com/projects/esp-idf/en/release-v6.0/esp32/api-guides/wifi-driver/wifi-modes.html
 * - https://github.com/espressif/esp-idf/tree/master/examples/wifi/getting_started/softAP
 */

#include "wifi_provisioning.h"
#include "app_config.h"

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>

#include <network_provisioning/manager.h>
#include <network_provisioning/scheme_softap.h>

#include "qrcode.h"

static const char *TAG = "WIFI_PROV";

/* Event group bits for WiFi connection status */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define PROV_END_BIT        BIT2

static EventGroupHandle_t s_prov_event_group = NULL;
static bool s_prov_initialized = false;
static bool s_prov_active = false;

/**
 * Event handler for provisioning and WiFi events
 */
static void prov_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    // Debug: log all events (event_base is a const char*)
    if (event_base) {
        ESP_LOGD(TAG, "Event received: base=%s, id=%ld", (const char *)event_base, (long)event_id);
    }
    
    if (event_base == NETWORK_PROV_EVENT) {
        switch (event_id) {
        case NETWORK_PROV_START:
            ESP_LOGI(TAG, "Provisioning started");
            s_prov_active = true;
            break;
            
        case NETWORK_PROV_WIFI_CRED_RECV: {
            wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "Received WiFi credentials: SSID=%s", 
                     (const char *)wifi_sta_cfg->ssid);
            // NOTE: Do NOT manually switch WiFi modes here
            // The provisioning manager handles mode transitions internally
            break;
        }
        
        case NETWORK_PROV_WIFI_CRED_FAIL: {
            network_prov_wifi_sta_fail_reason_t *reason = 
                (network_prov_wifi_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "Provisioning failed! Reason: %s",
                     (*reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR) ?
                     "WiFi auth failed" : "AP not found");
            xEventGroupSetBits(s_prov_event_group, WIFI_FAIL_BIT);
            break;
        }
        
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning successful - credentials saved");
            break;
            
        case NETWORK_PROV_END:
            ESP_LOGI(TAG, "Provisioning ended");
            s_prov_active = false;
            xEventGroupSetBits(s_prov_event_group, PROV_END_BIT);
            break;
            
        default:
            break;
        }
    } else if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            // When using the provisioning manager, DO NOT call esp_wifi_connect() here!
            // The provisioning manager handles WiFi connection after credentials are received.
            // Calling esp_wifi_connect() manually interferes with the provisioning flow
            // and causes "sta_connect: invalid ssid" errors when no credentials exist.
            ESP_LOGD(TAG, "WiFi STA started (provisioning manager handles connection)");
            break;
            
        case WIFI_EVENT_STA_DISCONNECTED:
            // Handle reconnection after credentials have been provisioned
            // During active provisioning, the provisioning manager handles everything
            // After provisioning completes (s_prov_active=false), we should reconnect
            if (!s_prov_active) {
                ESP_LOGI(TAG, "Disconnected, reconnecting...");
                esp_wifi_connect();
            } else {
                ESP_LOGD(TAG, "WiFi STA disconnected (provisioning active - manager handles reconnection)");
            }
            break;
            
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "SoftAP started - ready for client connections");
            break;
            
        case WIFI_EVENT_AP_STOP:
            ESP_LOGW(TAG, "SoftAP stopped!");
            break;
            
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *event = 
                (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "SoftAP: Device connected (AID=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x)",
                     event->aid,
                     event->mac[0], event->mac[1], event->mac[2],
                     event->mac[3], event->mac[4], event->mac[5]);
            break;
        }
        
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *event = 
                (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGW(TAG, "SoftAP: Device disconnected (AID=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x, reason=%d)",
                     event->aid,
                     event->mac[0], event->mac[1], event->mac[2],
                     event->mac[3], event->mac[4], event->mac[5],
                     event->reason);
            break;
        }
        
        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Connected with IP: " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(s_prov_event_group, WIFI_CONNECTED_BIT);
        } else if (event_id == IP_EVENT_ASSIGNED_IP_TO_CLIENT) {
            ip_event_assigned_ip_to_client_t *event = (ip_event_assigned_ip_to_client_t *)event_data;
            ESP_LOGI(TAG, "SoftAP: Client assigned IP " IPSTR " (MAC=%02x:%02x:%02x:%02x:%02x:%02x)",
                     IP2STR(&event->ip),
                     event->mac[0], event->mac[1], event->mac[2],
                     event->mac[3], event->mac[4], event->mac[5]);
        }
    }
}

esp_err_t wifi_provisioning_init(void)
{
    if (s_prov_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing WiFi provisioning (ESP-IDF v6.0 network_provisioning)");
    
    // Create event group
    s_prov_event_group = xEventGroupCreate();
    if (s_prov_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    
    // Create default event loop (required before registering handlers)
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means it's already created, which is fine
        ESP_ERROR_CHECK(ret);
    }
    
    // Register event handlers (now the event loop exists)
    ESP_ERROR_CHECK(esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, 
                                                &prov_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                                &prov_event_handler, NULL));
    // Register for IP events - both STA (our connection) and AP (client DHCP assignments)
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, 
                                                &prov_event_handler, NULL));
    
    // Create default WiFi network interfaces
    // IMPORTANT: Both STA and AP interfaces MUST be created BEFORE WiFi init and provisioning
    // The provisioning manager uses the existing AP interface with its DHCP server
    // Without the AP interface, DHCP server won't work and clients can't get IP addresses
    ESP_LOGI(TAG, "Creating WiFi network interfaces...");
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    
    if (sta_netif == NULL || ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi network interfaces");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "WiFi interfaces created (STA + AP with DHCP server)");
    
    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // NOTE: WiFi country code is not set manually here.
    // The provisioning manager and WiFi driver handle region settings automatically.
    // Manual country configuration can cause "ap start fail" errors in some cases.
    
    // Configure provisioning manager with softAP scheme
    network_prov_mgr_config_t prov_config = {
        .scheme = network_prov_scheme_softap,
        .scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
    };
    
    ret = network_prov_mgr_init(prov_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize provisioning manager: %s", 
                 esp_err_to_name(ret));
        return ret;
    }
    
    s_prov_initialized = true;
    ESP_LOGI(TAG, "WiFi provisioning initialized successfully");
    
    return ESP_OK;
}

esp_err_t wifi_provisioning_is_provisioned(bool *provisioned)
{
    if (!s_prov_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    return network_prov_mgr_is_wifi_provisioned(provisioned);
}

/**
 * Print QR code for ESP SoftAP Provisioning app
 * Format: {"ver":"v1","name":"SSID","pop":"","transport":"softap","security":"0","password":"xxx"}
 */
static void print_qr_code(const char *service_name, const char *password)
{
    // Format the provisioning payload for the ESP SoftAP Prov app
    // Using Security 0 (no encryption), softap transport with WPA2 password
    // IMPORTANT: The "security":"0" field is required to match NETWORK_PROV_SECURITY_0
    // Without this field, the app defaults to security version 2 causing a mismatch error
    char payload[200];
    if (password && strlen(password) >= 8) {
        // WPA2 protected network
        snprintf(payload, sizeof(payload),
                 "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"\",\"transport\":\"softap\",\"security\":\"0\",\"password\":\"%s\"}",
                 service_name, password);
    } else {
        // Open network
        snprintf(payload, sizeof(payload),
                 "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"\",\"transport\":\"softap\",\"security\":\"0\"}",
                 service_name);
    }
    
    ESP_LOGI(TAG, "Provisioning payload: %s", payload);
    
    // Generate and print QR code to console
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = esp_qrcode_print_console;
    cfg.max_qrcode_version = 10;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    
    ESP_LOGI(TAG, "Scan this QR code with ESP SoftAP Prov app:");
    esp_qrcode_generate(&cfg, payload);
    
    ESP_LOGI(TAG, "Or manually connect to WiFi: %s", service_name);
    if (password && strlen(password) >= 8) {
        ESP_LOGI(TAG, "WiFi Password: %s", password);
    }
    ESP_LOGI(TAG, "Then open http://192.168.4.1 in browser (or use app)");
}

esp_err_t wifi_provisioning_start(const char *service_name, const char *pop,
                                   httpd_handle_t httpd_handle)
{
    if (!s_prov_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret;
    
    // If an HTTP server handle is provided, share it with the provisioning manager
    // Otherwise, provisioning will create its own HTTP server
    if (httpd_handle != NULL) {
        ESP_LOGI(TAG, "Sharing existing HTTP server with provisioning manager");
        network_prov_scheme_softap_set_httpd_handle(httpd_handle);
    } else {
        ESP_LOGI(TAG, "Provisioning will create its own HTTP server");
    }
    
    // Configure security
    // Using Security 0 (no encryption) for simplicity - compatible with ESP SoftAP Prov app
    // For production, use Security 1 or 2 with proof-of-possession
    network_prov_security_t security = NETWORK_PROV_SECURITY_0;
    
    // Clear event bits before starting
    xEventGroupClearBits(s_prov_event_group, 
                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | PROV_END_BIT);
    
    // =============================================================================
    // CRITICAL: Configure SoftAP settings BEFORE starting provisioning
    // =============================================================================
    // The provisioning manager will use these WiFi settings when it starts the AP.
    // Key settings to prevent AUTH_EXPIRE (reason=2) disconnects:
    // 1. Fixed channel (1) - avoids channel switching issues
    // 2. HT20 bandwidth (20MHz) - better compatibility with all devices
    // 3. PMF disabled - Required for OPEN networks to work properly
    // 4. Explicit WIFI_AUTH_OPEN - no authentication required
    //
    // Reference: https://docs.espressif.com/projects/esp-idf/en/release-v6.0/esp32/api-guides/wifi-driver/wifi-modes.html
    // =============================================================================
    
    // First, set WiFi mode to APSTA (provisioning manager expects this)
    // NOTE: This is done BEFORE provisioning starts, not after
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    
    // Configure SoftAP with optimal settings for open network provisioning
    wifi_config_t ap_config = {
        .ap = {
            .ssid = {0},
            .ssid_len = 0,
            .channel = 1,                    // Fixed channel for stability
            .password = {0},                 // Empty for open network
            .max_connection = 4,             // Up to 4 clients
            .authmode = WIFI_AUTH_OPEN,      // Open network
            .ssid_hidden = 0,                // Broadcast SSID
            .beacon_interval = 100,          // Default beacon interval (ms)
            .pairwise_cipher = WIFI_CIPHER_TYPE_NONE,  // No encryption for open network
            .pmf_cfg = {
                .capable = false,            // PMF not supported (required for OPEN networks)
                .required = false,           // PMF not required
            },
        },
    };
    
    // Copy SSID to config
    size_t ssid_len = strlen(service_name);
    if (ssid_len > sizeof(ap_config.ap.ssid) - 1) {
        ssid_len = sizeof(ap_config.ap.ssid) - 1;
    }
    memcpy(ap_config.ap.ssid, service_name, ssid_len);
    ap_config.ap.ssid_len = ssid_len;
    
    // Apply the SoftAP configuration
    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set bandwidth to HT20 (20MHz) for better compatibility
    // HT40 (40MHz) can cause issues with some mobile devices
    ret = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set HT20 bandwidth: %s (continuing)", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "SoftAP pre-configured: SSID=%s, channel=%d, auth=OPEN, PMF=disabled, BW=HT20",
             service_name, ap_config.ap.channel);
    
    // Start WiFi (this applies our configuration)
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Small delay to let WiFi stabilize after starting
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Now start provisioning - it will use the SoftAP we already configured
    // service_key = NULL means open network (no WiFi password)
    ESP_LOGI(TAG, "Starting provisioning with SSID: %s (OPEN network)", service_name);
    
    ret = network_prov_mgr_start_provisioning(security, NULL, service_name, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_prov_active = true;
    
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "  Provisioning started!");
    ESP_LOGI(TAG, "  WiFi SSID: %s", service_name);
    ESP_LOGI(TAG, "  WiFi Password: (none - open network)");
    ESP_LOGI(TAG, "  Channel: 1 (fixed)");
    ESP_LOGI(TAG, "  Bandwidth: 20MHz (HT20)");
    ESP_LOGI(TAG, "  Use: ESP SoftAP Provisioning app");
    ESP_LOGI(TAG, "====================================");
    
    // Print QR code for ESP SoftAP Prov app (no password for open network)
    print_qr_code(service_name, NULL);
    
    return ESP_OK;
}

esp_err_t wifi_provisioning_stop(void)
{
    if (!s_prov_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping provisioning");
    network_prov_mgr_stop_provisioning();
    network_prov_mgr_deinit();
    
    s_prov_active = false;
    s_prov_initialized = false;
    
    return ESP_OK;
}

esp_err_t wifi_provisioning_reset(void)
{
    if (!s_prov_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Resetting provisioned credentials");
    return network_prov_mgr_reset_wifi_provisioning();
}

esp_err_t wifi_provisioning_wait_for_connection(uint32_t timeout_ms)
{
    if (!s_prov_initialized || s_prov_event_group == NULL) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    TickType_t wait_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    
    EventBits_t bits = xEventGroupWaitBits(s_prov_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE,
                                            wait_ticks);
    
    if (bits & WIFI_CONNECTED_BIT) {
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    
    return ESP_ERR_TIMEOUT;
}

bool wifi_provisioning_is_active(void)
{
    return s_prov_active;
}
