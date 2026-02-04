/**
 * @file wifi_provisioning.c
 * @brief WiFi Provisioning using ESP-IDF v6.0 network_provisioning component
 * 
 * This module implements WiFi provisioning using the official ESP-IDF v6.0
 * network_provisioning component with softAP transport.
 */

#include "wifi_provisioning.h"

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
            ESP_LOGI(TAG, "WiFi STA started, connecting...");
            esp_wifi_connect();
            break;
            
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "Disconnected, reconnecting...");
            esp_wifi_connect();
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
            ESP_LOGI(TAG, "SoftAP: Device disconnected (AID=%d, MAC=%02x:%02x:%02x:%02x:%02x:%02x)",
                     event->aid,
                     event->mac[0], event->mac[1], event->mac[2],
                     event->mac[3], event->mac[4], event->mac[5]);
            break;
        }
        
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected with IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_prov_event_group, WIFI_CONNECTED_BIT);
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
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                                &prov_event_handler, NULL));
    
    // Create default WiFi STA netif only
    // NOTE: Do NOT create AP netif here - network_prov_scheme_softap will create it
    esp_netif_create_default_wifi_sta();
    
    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Set WiFi country code for Netherlands (Europe)
    wifi_country_t country = {
        .cc = "NL",
        .schan = 1,
        .nchan = 13,
        .max_tx_power = 20,
        .policy = WIFI_COUNTRY_POLICY_AUTO,
    };
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));
    ESP_LOGI(TAG, "WiFi country set to NL (channels 1-13, 20dBm)");
    
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
 * Format: {"ver":"v1","name":"SSID","pop":"","transport":"softap"}
 */
static void print_qr_code(const char *service_name)
{
    // Format the provisioning payload for the ESP SoftAP Prov app
    // Using Security 0 (no pop), softap transport
    char payload[150];
    snprintf(payload, sizeof(payload),
             "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"\",\"transport\":\"softap\"}",
             service_name);
    
    ESP_LOGI(TAG, "Provisioning payload: %s", payload);
    
    // Generate and print QR code to console
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = esp_qrcode_print_console;
    cfg.max_qrcode_version = 10;
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    
    ESP_LOGI(TAG, "Scan this QR code with ESP SoftAP Prov app:");
    esp_qrcode_generate(&cfg, payload);
    
    ESP_LOGI(TAG, "Or manually connect to WiFi: %s", service_name);
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
    
    ESP_LOGI(TAG, "Starting provisioning with SSID: %s", service_name);
    
    ret = network_prov_mgr_start_provisioning(security, NULL, service_name, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_prov_active = true;
    
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "  Provisioning started!");
    ESP_LOGI(TAG, "  WiFi SSID: %s", service_name);
    ESP_LOGI(TAG, "  Security: None (open)");
    ESP_LOGI(TAG, "====================================");
    
    // Print QR code for ESP SoftAP Prov app
    print_qr_code(service_name);
    
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
