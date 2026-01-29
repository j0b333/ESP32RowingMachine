/**
 * @file dns_server.h
 * @brief Simple DNS server for captive portal
 * 
 * Redirects all DNS queries to the ESP32's IP address
 * to enable captive portal functionality.
 */

#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include "esp_err.h"

/**
 * Start DNS server for captive portal
 * All DNS queries will resolve to the specified IP
 * 
 * @param ip_addr IP address to return for all queries (typically ESP32's AP IP)
 * @return ESP_OK on success
 */
esp_err_t dns_server_start(const char *ip_addr);

/**
 * Stop DNS server
 */
void dns_server_stop(void);

/**
 * Check if DNS server is running
 * @return true if running
 */
bool dns_server_is_running(void);

#endif // DNS_SERVER_H
