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
#include "lwip/inet.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

#include <string.h>
#include <inttypes.h>

static const char *TAG = "WIFI";

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
 * Deinitialize WiFi subsystem
 */
void wifi_manager_deinit(void) {
    WIFI_MUTEX_TAKE();
    
    if (!s_wifi_initialized) {
        WIFI_MUTEX_GIVE();
        return;
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
