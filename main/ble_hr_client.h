/**
 * @file ble_hr_client.h
 * @brief BLE Heart Rate Client for connecting to BLE heart rate monitors
 * 
 * Connects to standard BLE Heart Rate Service (0x180D) devices such as
 * "Heart for Bluetooth" Android watch app, chest straps, and other
 * BLE heart rate monitors.
 */

#ifndef BLE_HR_CLIENT_H
#define BLE_HR_CLIENT_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * BLE HR client connection state
 */
typedef enum {
    BLE_HR_STATE_IDLE = 0,      // Not scanning or connected
    BLE_HR_STATE_SCANNING,      // Scanning for HR monitors
    BLE_HR_STATE_CONNECTING,    // Connecting to discovered device
    BLE_HR_STATE_CONNECTED,     // Connected and subscribed
    BLE_HR_STATE_ERROR          // Error state
} ble_hr_state_t;

/**
 * Initialize BLE HR client
 * @return ESP_OK on success
 */
esp_err_t ble_hr_client_init(void);

/**
 * Deinitialize BLE HR client
 */
void ble_hr_client_deinit(void);

/**
 * Start scanning for BLE heart rate monitors
 * @return ESP_OK on success
 */
esp_err_t ble_hr_client_start_scan(void);

/**
 * Stop scanning
 */
void ble_hr_client_stop_scan(void);

/**
 * Check if connected to a heart rate monitor
 * @return true if connected and receiving HR data
 */
bool ble_hr_client_is_connected(void);

/**
 * Get current connection state
 * @return Current BLE HR client state
 */
ble_hr_state_t ble_hr_client_get_state(void);

/**
 * Disconnect from current heart rate monitor
 */
void ble_hr_client_disconnect(void);

#endif // BLE_HR_CLIENT_H
