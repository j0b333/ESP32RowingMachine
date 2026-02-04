/**
 * @file web_server.h
 * @brief HTTP server with WebSocket for real-time data streaming
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "rowing_physics.h"

/**
 * Initialize and start HTTP server
 * @param metrics Pointer to metrics structure
 * @param config Pointer to configuration
 * @return ESP_OK on success
 */
esp_err_t web_server_start(rowing_metrics_t *metrics, config_t *config);

/**
 * Start a minimal HTTP server for captive portal during provisioning
 * 
 * This creates a lightweight HTTP server with only captive portal handlers,
 * suitable for sharing with the provisioning manager (no wildcard URI matcher).
 * 
 * @return ESP_OK on success
 */
esp_err_t web_server_start_captive_portal(void);

/**
 * Stop HTTP server
 */
void web_server_stop(void);

/**
 * Get the HTTP server handle
 * Useful for sharing with provisioning manager
 * @return HTTP server handle or NULL if not started
 */
httpd_handle_t web_server_get_handle(void);

/**
 * Broadcast metrics to all connected WebSocket clients
 * @param metrics Pointer to current metrics
 * @return ESP_OK on success
 */
esp_err_t web_server_broadcast_metrics(const rowing_metrics_t *metrics);

/**
 * Check if any WebSocket clients are connected
 * @return true if at least one client is connected
 */
bool web_server_has_ws_clients(void);

/**
 * Get number of active connections
 * @return Number of active HTTP/WS connections
 */
int web_server_get_connection_count(void);

/**
 * Update inertia calibration with flywheel data
 * Call this from sensor task when processing flywheel pulses
 * @param angular_velocity Current angular velocity in rad/s
 * @param current_time_us Current timestamp in microseconds
 * @return true if calibration is active and was updated
 */
bool web_server_update_inertia_calibration(float angular_velocity, int64_t current_time_us);

/**
 * Check if inertia calibration is currently active
 * @return true if calibration is in progress
 */
bool web_server_is_calibrating_inertia(void);

#endif // WEB_SERVER_H
