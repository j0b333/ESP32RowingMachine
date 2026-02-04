/**
 * @file wifi_provisioning.h
 * @brief WiFi Provisioning using ESP-IDF v6.0 network_provisioning component
 * 
 * This module implements WiFi provisioning using the official ESP-IDF v6.0
 * network_provisioning component with softAP transport.
 */

#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <stdbool.h>
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi provisioning
 * 
 * Initializes the network_provisioning manager with softAP scheme.
 * Must be called before start_provisioning().
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_provisioning_init(void);

/**
 * @brief Check if device is already provisioned
 * 
 * @param provisioned Pointer to store result
 * @return ESP_OK on success
 */
esp_err_t wifi_provisioning_is_provisioned(bool *provisioned);

/**
 * @brief Start WiFi provisioning via softAP
 * 
 * Starts the provisioning process. The device will create a softAP
 * that clients can connect to for provisioning.
 * 
 * @param service_name The SSID for the softAP (e.g., "CrivitRower")
 * @param pop Proof of possession password (NULL for no security)
 * @param httpd_handle Optional existing HTTP server handle to share
 * @return ESP_OK on success
 */
esp_err_t wifi_provisioning_start(const char *service_name, const char *pop, 
                                   httpd_handle_t httpd_handle);

/**
 * @brief Stop WiFi provisioning
 * 
 * Stops provisioning and de-initializes the manager.
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_provisioning_stop(void);

/**
 * @brief Reset provisioned WiFi credentials
 * 
 * Clears stored WiFi credentials and resets to provisioning mode.
 * 
 * @return ESP_OK on success
 */
esp_err_t wifi_provisioning_reset(void);

/**
 * @brief Wait for provisioning to complete
 * 
 * Blocks until provisioning is complete and device has connected to WiFi.
 * 
 * @param timeout_ms Timeout in milliseconds (0 for infinite)
 * @return ESP_OK if connected, ESP_ERR_TIMEOUT if timed out
 */
esp_err_t wifi_provisioning_wait_for_connection(uint32_t timeout_ms);

/**
 * @brief Get the provisioning state
 * 
 * @return true if provisioning is currently active
 */
bool wifi_provisioning_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_PROVISIONING_H */
