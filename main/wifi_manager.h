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
 * Start WiFi in Station mode with custom timeout
 * Attempts to connect for up to timeout_sec seconds, then returns failure.
 * 
 * @param ssid Network SSID to connect to
 * @param password Network password
 * @param timeout_sec Maximum time to wait for connection (seconds)
 * @return true if connected, false if timeout or failure
 */
bool wifi_manager_connect_sta_with_timeout(const char *ssid, const char *password, uint32_t timeout_sec);

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
                                    uint32_t timeout_sec);

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
