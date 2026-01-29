/**
 * @file wifi_manager.h
 * @brief WiFi AP/STA management for web interface access
 * 
 * Compatible with ESP-IDF 6.0+
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_wifi_types.h"
#include "rowing_physics.h"

/**
 * WiFi operating mode enumeration
 * Note: Using custom names to avoid conflict with ESP-IDF wifi_mode_t
 */
typedef enum {
    WIFI_OPERATING_MODE_AP,       // Access Point mode (default)
    WIFI_OPERATING_MODE_STA,      // Station mode (connect to existing network)
    WIFI_OPERATING_MODE_APSTA     // Both AP and STA
} wifi_operating_mode_t;

/**
 * Initialize WiFi subsystem
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_init(void);

/**
 * Deinitialize WiFi subsystem
 */
void wifi_manager_deinit(void);

/**
 * Start WiFi in Access Point mode
 * @param ssid AP SSID
 * @param password AP password (NULL for open network)
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_start_ap(const char *ssid, const char *password);

/**
 * Start WiFi in Station mode
 * @param ssid Network SSID to connect to
 * @param password Network password
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_start_sta(const char *ssid, const char *password);

/**
 * Stop WiFi
 */
void wifi_manager_stop(void);

/**
 * Get current IP address as string
 * @param buffer Output buffer
 * @param buf_len Buffer length
 * @return ESP_OK on success
 */
esp_err_t wifi_manager_get_ip_string(char *buffer, size_t buf_len);

/**
 * Check if WiFi is connected (in STA mode)
 * @return true if connected
 */
bool wifi_manager_is_connected(void);

/**
 * Get number of connected stations (in AP mode)
 * @return Number of connected clients
 */
int wifi_manager_get_station_count(void);

/**
 * Scan for available WiFi networks
 * @param ap_records Array to store scan results (must be pre-allocated)
 * @param max_records Maximum number of records to return
 * @return Number of networks found
 */
int wifi_manager_scan(wifi_ap_record_t *ap_records, uint16_t max_records);

/**
 * Get current operating mode
 * @return Current WiFi mode
 */
wifi_operating_mode_t wifi_manager_get_mode(void);

#endif // WIFI_MANAGER_H
