/**
 * @file wifi_manager.c
 * @brief WiFi AP/STA management for web interface access
 * 
 * Compatible with ESP-IDF 6.0+
 * Thread-safe: Uses mutex to prevent race conditions
 */

#include "wifi_manager.h"
#include "app_config.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "mdns.h"
#include "lwip/inet.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "WIFI";

// mDNS hostname (rower.local)
#define MDNS_HOSTNAME "rower"

// Event bits for WiFi events
#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1
#define WIFI_STARTED_BIT        BIT2

// Event group handle
static EventGroupHandle_t s_wifi_event_group = NULL;

// Mutex for thread-safe operations
static SemaphoreHandle_t s_wifi_mutex = NULL;

// Network interfaces
static esp_netif_t *s_netif_ap = NULL;
static esp_netif_t *s_netif_sta = NULL;

// Current mode and state
static wifi_operating_mode_t s_current_mode = WIFI_OPERATING_MODE_AP;
static bool s_wifi_initialized = false;
static bool s_mdns_initialized = false;
static bool s_sntp_initialized = false;
static bool s_time_synced = false;
static int s_retry_count = 0;
static const int MAX_RETRY = 5;

// IP address storage
static esp_netif_ip_info_t s_ip_info;

// Mutex helper macros
#define WIFI_MUTEX_TAKE() \
    do { \
        if (s_wifi_mutex != NULL) { \
            xSemaphoreTake(s_wifi_mutex, portMAX_DELAY); \
        } \
    } while(0)

#define WIFI_MUTEX_GIVE() \
    do { \
        if (s_wifi_mutex != NULL) { \
            xSemaphoreGive(s_wifi_mutex); \
        } \
    } while(0)

/**
 * WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started");
                esp_wifi_connect();
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGW(TAG, "WiFi disconnected");
                if (s_retry_count < MAX_RETRY) {
                    esp_wifi_connect();
                    s_retry_count++;
                    ESP_LOGI(TAG, "Retrying connection... (%d/%d)", s_retry_count, MAX_RETRY);
                } else {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    ESP_LOGE(TAG, "Failed to connect after %d attempts", MAX_RETRY);
                }
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi connected to AP");
                s_retry_count = 0;
                break;
                
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "WiFi AP started");
                xEventGroupSetBits(s_wifi_event_group, WIFI_STARTED_BIT);
                break;
                
            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "WiFi AP stopped");
                break;
                
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG, "Station connected: %02x:%02x:%02x:%02x:%02x:%02x, AID=%d",
                         event->mac[0], event->mac[1], event->mac[2],
                         event->mac[3], event->mac[4], event->mac[5], event->aid);
                break;
            }
            
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
                ESP_LOGI(TAG, "Station disconnected: %02x:%02x:%02x:%02x:%02x:%02x, AID=%d, reason=%d",
                         event->mac[0], event->mac[1], event->mac[2],
                         event->mac[3], event->mac[4], event->mac[5], 
                         event->aid, event->reason);
                break;
            }
            
            default:
                ESP_LOGD(TAG, "WiFi event: %" PRId32, event_id);
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
                memcpy(&s_ip_info, &event->ip_info, sizeof(esp_netif_ip_info_t));
                ESP_LOGI(TAG, "Got IP: %d.%d.%d.%d", 
                         IP2STR(&event->ip_info.ip));
                xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                
                // Initialize SNTP time synchronization now that we have network access
                wifi_manager_init_sntp();
                break;
            }
            
            case IP_EVENT_ASSIGNED_IP_TO_CLIENT: {
                ip_event_assigned_ip_to_client_t *event = (ip_event_assigned_ip_to_client_t *)event_data;
                ESP_LOGI(TAG, "Assigned IP to station: %d.%d.%d.%d", 
                         IP2STR(&event->ip));
                break;
            }
            
            default:
                ESP_LOGD(TAG, "IP event: %" PRId32, event_id);
                break;
        }
    }
}

/**
 * Initialize WiFi subsystem
 */
esp_err_t wifi_manager_init(void) {
    // Create mutex first (before checking initialized flag)
    if (s_wifi_mutex == NULL) {
        s_wifi_mutex = xSemaphoreCreateMutex();
        if (s_wifi_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create WiFi mutex");
            return ESP_FAIL;
        }
    }
    
    WIFI_MUTEX_TAKE();
    
    if (s_wifi_initialized) {
        ESP_LOGW(TAG, "WiFi already initialized");
        WIFI_MUTEX_GIVE();
        return ESP_OK;
    }
    
    esp_err_t ret;
    
    // Create event group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        WIFI_MUTEX_GIVE();
        return ESP_FAIL;
    }
    
    // Initialize TCP/IP stack
    ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init netif: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    // Create default event loop if not already created
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    // Create default WiFi AP and STA
    s_netif_ap = esp_netif_create_default_wifi_ap();
    s_netif_sta = esp_netif_create_default_wifi_sta();
    
    if (s_netif_ap == NULL || s_netif_sta == NULL) {
        ESP_LOGE(TAG, "Failed to create netif");
        WIFI_MUTEX_GIVE();
        return ESP_FAIL;
    }
    
    // Initialize WiFi with default config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    // Register event handlers
    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    ret = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    s_wifi_initialized = true;
    WIFI_MUTEX_GIVE();
    
    ESP_LOGI(TAG, "WiFi manager initialized");
    
    return ESP_OK;
}

/**
 * Initialize mDNS service
 */
static esp_err_t init_mdns(void) {
    // Prevent double initialization
    if (s_mdns_initialized) {
        ESP_LOGD(TAG, "mDNS already initialized");
        return ESP_OK;
    }
    
    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set hostname: rower.local
    ret = mdns_hostname_set(MDNS_HOSTNAME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mDNS hostname set failed: %s", esp_err_to_name(ret));
        mdns_free();
        return ret;
    }
    
    // Set instance name (log but don't fail on error)
    ret = mdns_instance_name_set("Crivit Rowing Monitor");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mDNS instance name set failed: %s", esp_err_to_name(ret));
    }
    
    // Add HTTP service (log but don't fail on error)
    ret = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "mDNS service add failed: %s", esp_err_to_name(ret));
    }
    
    s_mdns_initialized = true;
    ESP_LOGI(TAG, "mDNS started: %s.local", MDNS_HOSTNAME);
    
    return ESP_OK;
}

/**
 * Deinitialize WiFi subsystem
 */
void wifi_manager_deinit(void) {
    WIFI_MUTEX_TAKE();
    
    if (!s_wifi_initialized) {
        WIFI_MUTEX_GIVE();
        return;
    }
    
    // Stop mDNS first
    if (s_mdns_initialized) {
        mdns_free();
        s_mdns_initialized = false;
    }
    
    wifi_manager_stop();
    esp_wifi_deinit();
    
    if (s_wifi_event_group != NULL) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }
    
    s_wifi_initialized = false;
    WIFI_MUTEX_GIVE();
    
    // Delete mutex last (after releasing it)
    if (s_wifi_mutex != NULL) {
        vSemaphoreDelete(s_wifi_mutex);
        s_wifi_mutex = NULL;
    }
    
    ESP_LOGI(TAG, "WiFi manager deinitialized");
}

/**
 * Start WiFi in Access Point mode
 */
esp_err_t wifi_manager_start_ap(const char *ssid, const char *password) {
    WIFI_MUTEX_TAKE();
    
    if (!s_wifi_initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        WIFI_MUTEX_GIVE();
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret;
    
    // Clear previous event bits
    xEventGroupClearBits(s_wifi_event_group, WIFI_STARTED_BIT | WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    
    // Set mode to AP
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP mode: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    // Configure AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(ssid),
            .channel = WIFI_AP_CHANNEL,
            .max_connection = WIFI_AP_MAX_CONNECTIONS,
            .authmode = (password != NULL && strlen(password) >= 8) ? 
                        WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    
    strncpy((char *)wifi_config.ap.ssid, ssid, sizeof(wifi_config.ap.ssid) - 1);
    if (password != NULL && strlen(password) >= 8) {
        strncpy((char *)wifi_config.ap.password, password, sizeof(wifi_config.ap.password) - 1);
    }
    
    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    // Set AP bandwidth to HT20 for better client compatibility
    // ESP32-S3 defaults to HT40 which can cause connection issues with some devices
    ret = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW20);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set AP bandwidth to HT20: %s", esp_err_to_name(ret));
        // Continue anyway, this is not critical
    }
    
    // Release mutex before waiting (to prevent deadlock)
    WIFI_MUTEX_GIVE();
    
    // Wait for AP to start
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_STARTED_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(5000));
    
    if (!(bits & WIFI_STARTED_BIT)) {
        ESP_LOGW(TAG, "AP start timeout, but continuing...");
    }
    
    // Get AP IP address
    esp_netif_get_ip_info(s_netif_ap, &s_ip_info);
    
    // Initialize mDNS for rower.local
    init_mdns();
    
    s_current_mode = WIFI_OPERATING_MODE_AP;
    ESP_LOGI(TAG, "WiFi AP started: SSID=%s, IP=" IPSTR, ssid, IP2STR(&s_ip_info.ip));
    
    return ESP_OK;
}

/**
 * Start WiFi in Station mode
 */
esp_err_t wifi_manager_start_sta(const char *ssid, const char *password) {
    WIFI_MUTEX_TAKE();
    
    if (!s_wifi_initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        WIFI_MUTEX_GIVE();
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret;
    
    // Reset retry counter and clear event bits
    s_retry_count = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_STARTED_BIT | WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    
    // Set mode to STA
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA mode: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    // Configure STA
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };
    
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA config: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    // Release mutex before waiting (to prevent deadlock)
    WIFI_MUTEX_GIVE();
    
    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(30000));
    
    if (bits & WIFI_CONNECTED_BIT) {
        // Initialize mDNS for rower.local
        init_mdns();
        
        s_current_mode = WIFI_OPERATING_MODE_STA;
        ESP_LOGI(TAG, "Connected to WiFi: %s", ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", ssid);
        return ESP_FAIL;
    }
    
    ESP_LOGE(TAG, "WiFi connection timeout");
    return ESP_ERR_TIMEOUT;
}

/**
 * Start WiFi in Station mode with custom timeout
 * Attempts to connect for up to timeout_sec seconds, then returns failure.
 */
bool wifi_manager_connect_sta_with_timeout(const char *ssid, const char *password, uint32_t timeout_sec) {
    WIFI_MUTEX_TAKE();
    
    if (!s_wifi_initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        WIFI_MUTEX_GIVE();
        return false;
    }
    
    if (ssid == NULL || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Invalid SSID");
        WIFI_MUTEX_GIVE();
        return false;
    }
    
    esp_err_t ret;
    
    // Reset retry counter and clear event bits
    s_retry_count = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_STARTED_BIT | WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    
    // Set mode to STA
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA mode: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return false;
    }
    
    // Configure STA
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA_PSK,  // Allow WPA and WPA2
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };
    
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    
    if (password != NULL) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';
    }
    
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA config: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return false;
    }
    
    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return false;
    }
    
    // Release mutex before waiting (to prevent deadlock)
    WIFI_MUTEX_GIVE();
    
    ESP_LOGI(TAG, "Waiting for WiFi connection (up to %lu seconds)...", (unsigned long)timeout_sec);
    
    // Wait in 10-second intervals for better progress feedback
    uint32_t elapsed = 0;
    const uint32_t check_interval = 10; // seconds
    
    while (elapsed < timeout_sec) {
        uint32_t remaining = timeout_sec - elapsed;
        uint32_t wait_time = (remaining < check_interval) ? remaining : check_interval;
        
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                pdFALSE, pdFALSE,
                                                pdMS_TO_TICKS(wait_time * 1000));
        
        if (bits & WIFI_CONNECTED_BIT) {
            // Initialize mDNS for rower.local
            init_mdns();
            
            s_current_mode = WIFI_OPERATING_MODE_STA;
            ESP_LOGI(TAG, "Successfully connected to WiFi: %s", ssid);
            return true;
        }
        
        if (bits & WIFI_FAIL_BIT) {
            ESP_LOGW(TAG, "Connection failed after retries");
            // Stop WiFi to clean up
            esp_wifi_stop();
            return false;
        }
        
        elapsed += wait_time;
        
        if (elapsed < timeout_sec) {
            ESP_LOGI(TAG, "Still trying to connect... (%lu seconds remaining)", 
                     (unsigned long)(timeout_sec - elapsed));
        }
    }
    
    // Timeout reached
    ESP_LOGW(TAG, "Failed to connect to %s within %lu seconds", ssid, (unsigned long)timeout_sec);
    
    // Stop WiFi to clean up before fallback
    esp_wifi_stop();
    
    return false;
}

/**
 * Start WiFi in AP+STA mode (both simultaneously)
 * This allows devices to connect directly to the ESP32's AP while also
 * being connected to a home router. Useful for multi-device access.
 * 
 * @param ap_ssid AP SSID
 * @param ap_password AP password (NULL for open network)
 * @param sta_ssid Router SSID to connect to
 * @param sta_password Router password
 * @param timeout_sec Timeout for STA connection (0 for no timeout)
 * @return ESP_OK if both AP started and STA connected
 */
esp_err_t wifi_manager_start_apsta(const char *ap_ssid, const char *ap_password,
                                    const char *sta_ssid, const char *sta_password,
                                    uint32_t timeout_sec) {
    WIFI_MUTEX_TAKE();
    
    if (!s_wifi_initialized) {
        ESP_LOGE(TAG, "WiFi not initialized");
        WIFI_MUTEX_GIVE();
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret;
    
    // Reset retry counter and clear event bits
    s_retry_count = 0;
    xEventGroupClearBits(s_wifi_event_group, WIFI_STARTED_BIT | WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    
    // Set mode to APSTA (both AP and STA)
    ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set APSTA mode: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    // Configure AP
    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = strlen(ap_ssid),
            .channel = WIFI_AP_CHANNEL,
            .max_connection = WIFI_AP_MAX_CONNECTIONS,
            .authmode = (ap_password != NULL && strlen(ap_password) >= 8) ? 
                        WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    
    strncpy((char *)ap_config.ap.ssid, ap_ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid[sizeof(ap_config.ap.ssid) - 1] = '\0';
    if (ap_password != NULL && strlen(ap_password) >= 8) {
        strncpy((char *)ap_config.ap.password, ap_password, sizeof(ap_config.ap.password) - 1);
        ap_config.ap.password[sizeof(ap_config.ap.password) - 1] = '\0';
    }
    
    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    // Configure STA
    wifi_config_t sta_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA_PSK,  // Allow WPA and WPA2
            .pmf_cfg = {
                .capable = true,
                .required = false,
            },
        },
    };
    
    strncpy((char *)sta_config.sta.ssid, sta_ssid, sizeof(sta_config.sta.ssid) - 1);
    sta_config.sta.ssid[sizeof(sta_config.sta.ssid) - 1] = '\0';
    
    if (sta_password != NULL) {
        strncpy((char *)sta_config.sta.password, sta_password, sizeof(sta_config.sta.password) - 1);
        sta_config.sta.password[sizeof(sta_config.sta.password) - 1] = '\0';
    }
    
    ret = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA config: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        WIFI_MUTEX_GIVE();
        return ret;
    }
    
    // Set AP bandwidth to HT20 for better client compatibility
    // ESP32-S3 defaults to HT40 which can cause connection issues with some devices
    ret = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW20);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set AP bandwidth to HT20: %s", esp_err_to_name(ret));
        // Continue anyway, this is not critical
    }
    
    // Release mutex before waiting (to prevent deadlock)
    WIFI_MUTEX_GIVE();
    
    ESP_LOGI(TAG, "APSTA mode started. AP: %s, Connecting to: %s", ap_ssid, sta_ssid);
    
    if (timeout_sec > 0) {
        // Wait for STA connection
        ESP_LOGI(TAG, "Waiting for STA connection (up to %lu seconds)...", (unsigned long)timeout_sec);
        
        uint32_t elapsed = 0;
        const uint32_t check_interval = 10; // seconds
        
        while (elapsed < timeout_sec) {
            uint32_t remaining = timeout_sec - elapsed;
            uint32_t wait_time = (remaining < check_interval) ? remaining : check_interval;
            
            EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                                    WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                    pdFALSE, pdFALSE,
                                                    pdMS_TO_TICKS(wait_time * 1000));
            
            if (bits & WIFI_CONNECTED_BIT) {
                // Get STA IP address
                esp_netif_get_ip_info(s_netif_sta, &s_ip_info);
                
                // Initialize mDNS for rower.local
                init_mdns();
                
                s_current_mode = WIFI_OPERATING_MODE_APSTA;
                ESP_LOGI(TAG, "APSTA: Connected to %s, STA IP=" IPSTR ", AP IP=192.168.4.1", 
                         sta_ssid, IP2STR(&s_ip_info.ip));
                return ESP_OK;
            }
            
            if (bits & WIFI_FAIL_BIT) {
                ESP_LOGW(TAG, "STA connection failed, but AP is still running");
                break;
            }
            
            elapsed += wait_time;
            
            if (elapsed < timeout_sec) {
                ESP_LOGI(TAG, "Still trying to connect... (%lu seconds remaining)", 
                         (unsigned long)(timeout_sec - elapsed));
            }
        }
        
        // STA connection failed or timed out, but AP is still running
        // Get AP IP address so clients can still connect via AP
        esp_netif_get_ip_info(s_netif_ap, &s_ip_info);
        init_mdns();
        
        ESP_LOGW(TAG, "STA connection failed/timed out, AP is still running at " IPSTR, 
                 IP2STR(&s_ip_info.ip));
        // Note: Hardware is in APSTA mode, but we track as AP since STA is not connected
        s_current_mode = WIFI_OPERATING_MODE_APSTA;
        return ESP_ERR_TIMEOUT;
    }
    
    // No timeout specified, just start APSTA mode without waiting
    // Caller should poll wifi_manager_is_connected() to check STA status
    esp_netif_get_ip_info(s_netif_ap, &s_ip_info);
    init_mdns();
    s_current_mode = WIFI_OPERATING_MODE_APSTA;
    return ESP_OK;
}

/**
 * Stop WiFi
 */
void wifi_manager_stop(void) {
    WIFI_MUTEX_TAKE();
    esp_wifi_stop();
    WIFI_MUTEX_GIVE();
    ESP_LOGI(TAG, "WiFi stopped");
}

/**
 * Get current IP address as string
 */
esp_err_t wifi_manager_get_ip_string(char *buffer, size_t buf_len) {
    if (buffer == NULL || buf_len < 16) {
        return ESP_ERR_INVALID_ARG;
    }
    
    snprintf(buffer, buf_len, IPSTR, IP2STR(&s_ip_info.ip));
    return ESP_OK;
}

/**
 * Check if WiFi is connected (in STA mode)
 */
bool wifi_manager_is_connected(void) {
    if (s_wifi_event_group == NULL) {
        return false;
    }
    
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

/**
 * Get number of connected stations (in AP mode)
 */
int wifi_manager_get_station_count(void) {
    wifi_sta_list_t sta_list;
    esp_err_t ret = esp_wifi_ap_get_sta_list(&sta_list);
    if (ret != ESP_OK) {
        return 0;
    }
    return sta_list.num;
}

/**
 * Scan for available WiFi networks
 * @param ap_records Array to store scan results (must be pre-allocated)
 * @param max_records Maximum number of records to return
 * @return Number of networks found
 */
int wifi_manager_scan(wifi_ap_record_t *ap_records, uint16_t max_records) {
    if (ap_records == NULL || max_records == 0) {
        return 0;
    }
    
    esp_err_t ret;
    wifi_mode_t original_mode;
    
    // Get current WiFi mode
    ret = esp_wifi_get_mode(&original_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi mode: %s", esp_err_to_name(ret));
        return 0;
    }
    
    // If in pure AP mode, we need to switch to APSTA mode to scan
    // Scanning requires the STA interface to be active
    bool switched_mode = false;
    if (original_mode == WIFI_MODE_AP) {
        ESP_LOGI(TAG, "Switching to APSTA mode for scanning...");
        
        // Create STA netif if not exists
        if (s_netif_sta == NULL) {
            s_netif_sta = esp_netif_create_default_wifi_sta();
        }
        
        ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set APSTA mode: %s", esp_err_to_name(ret));
            return 0;
        }
        switched_mode = true;
        
        // Small delay to let mode switch take effect
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Configure scan - use passive scan which is more reliable
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_PASSIVE,
        .scan_time.passive = 0,  // Use ESP-IDF default (required when BT is enabled)
    };
    
    ESP_LOGI(TAG, "Starting WiFi scan...");
    
    // Start blocking scan
    ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        // Restore original mode if we switched
        if (switched_mode) {
            esp_wifi_set_mode(original_mode);
        }
        return 0;
    }
    
    // Get number of APs found
    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(ret));
        if (switched_mode) {
            esp_wifi_set_mode(original_mode);
        }
        return 0;
    }
    
    ESP_LOGI(TAG, "Found %d access points", ap_count);
    
    // Limit to max records
    uint16_t records_to_get = (ap_count < max_records) ? ap_count : max_records;
    
    // Get AP records
    ret = esp_wifi_scan_get_ap_records(&records_to_get, ap_records);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(ret));
        if (switched_mode) {
            esp_wifi_set_mode(original_mode);
        }
        return 0;
    }
    
    // Restore original mode if we switched (keep AP running for provisioning)
    if (switched_mode) {
        ESP_LOGI(TAG, "Restoring AP mode after scan");
        esp_wifi_set_mode(original_mode);
    }
    
    return records_to_get;
}

/**
 * Get current operating mode
 */
wifi_operating_mode_t wifi_manager_get_mode(void) {
    return s_current_mode;
}

/**
 * SNTP time sync callback
 */
static void sntp_sync_callback(struct timeval *tv) {
    ESP_LOGI(TAG, "SNTP time synchronized: %lld.%06ld", (long long)tv->tv_sec, (long)tv->tv_usec);
    s_time_synced = true;
}

/**
 * Initialize SNTP time synchronization
 * Call this after successful WiFi STA connection to synchronize time
 */
void wifi_manager_init_sntp(void) {
    if (s_sntp_initialized) {
        ESP_LOGD(TAG, "SNTP already initialized");
        return;
    }
    
    ESP_LOGI(TAG, "Initializing SNTP time synchronization");
    
    // Set timezone to UTC (the app handles timezone conversion)
    setenv("TZ", "UTC0", 1);
    tzset();
    
    // Configure SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_callback);
    
    // Initialize SNTP
    esp_sntp_init();
    
    s_sntp_initialized = true;
    
    ESP_LOGI(TAG, "SNTP initialized, waiting for time sync...");
}

// Unix timestamp for January 1, 2020 00:00:00 UTC
// Used to validate that SNTP time sync has completed successfully
#define UNIX_TIMESTAMP_YEAR_2020 1577836800

/**
 * Check if time has been synchronized via SNTP
 * @return true if time is synchronized and valid
 */
bool wifi_manager_is_time_synced(void) {
    // Also verify that time() returns a reasonable value (after year 2020)
    if (s_time_synced) {
        time_t now = time(NULL);
        return now > UNIX_TIMESTAMP_YEAR_2020;
    }
    return false;
}

/**
 * Get current Unix time in milliseconds
 * @return Unix timestamp in milliseconds, or 0 if time not synced
 */
int64_t wifi_manager_get_unix_time_ms(void) {
    if (!wifi_manager_is_time_synced()) {
        return 0;
    }
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    return (int64_t)tv.tv_sec * 1000LL + (int64_t)tv.tv_usec / 1000LL;
}
