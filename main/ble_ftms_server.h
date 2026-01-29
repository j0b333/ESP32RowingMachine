/**
 * @file ble_ftms_server.h
 * @brief Bluetooth Low Energy Fitness Machine Service (FTMS) implementation
 */

#ifndef BLE_FTMS_SERVER_H
#define BLE_FTMS_SERVER_H

#include "esp_err.h"
#include "rowing_physics.h"

/**
 * Initialize BLE FTMS service
 * @param device_name BLE device name to advertise
 * @return ESP_OK on success
 */
esp_err_t ble_ftms_init(const char *device_name);

/**
 * Deinitialize BLE FTMS service
 */
void ble_ftms_deinit(void);

/**
 * Start BLE advertising
 * @return ESP_OK on success
 */
esp_err_t ble_ftms_start_advertising(void);

/**
 * Stop BLE advertising
 */
void ble_ftms_stop_advertising(void);

/**
 * Send BLE notification with current metrics
 * @param metrics Pointer to current metrics
 * @return ESP_OK on success
 */
esp_err_t ble_ftms_notify_metrics(const rowing_metrics_t *metrics);

/**
 * Check if BLE is connected
 * @return true if connected
 */
bool ble_ftms_is_connected(void);

/**
 * Get current connection handle
 * @return Connection handle or 0 if not connected
 */
uint16_t ble_ftms_get_conn_handle(void);

#endif // BLE_FTMS_SERVER_H
