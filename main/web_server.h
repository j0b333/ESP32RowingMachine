/**
 * @file web_server.h
 * @brief HTTP server with WebSocket for real-time data streaming
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_err.h"
#include "rowing_physics.h"

/**
 * Initialize and start HTTP server
 * @param metrics Pointer to metrics structure
 * @param config Pointer to configuration
 * @return ESP_OK on success
 */
esp_err_t web_server_start(rowing_metrics_t *metrics, config_t *config);

/**
 * Stop HTTP server
 */
void web_server_stop(void);

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

#endif // WEB_SERVER_H
