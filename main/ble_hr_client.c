/**
 * @file ble_hr_client.c
 * @brief BLE Heart Rate Client implementation
 * 
 * Implements a BLE GATT client that connects to standard BLE Heart Rate
 * Service (0x180D) devices and subscribes to Heart Rate Measurement
 * characteristic (0x2A37) notifications.
 * 
 * Compatible with:
 * - "Heart for Bluetooth" Android watch app
 * - Standard BLE heart rate chest straps
 * - Any BLE device exposing Heart Rate Service
 * 
 * Compatible with ESP-IDF 6.0+
 */

#include "ble_hr_client.h"
#include "hr_receiver.h"
#include "app_config.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* NimBLE includes */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"

#include <string.h>

static const char *TAG = "BLE_HR_CLI";

// ============================================================================
// Standard BLE Heart Rate Service UUIDs
// ============================================================================
#define HRS_SERVICE_UUID                0x180D  // Heart Rate Service
#define HRS_HEART_RATE_MEASUREMENT_UUID 0x2A37  // Heart Rate Measurement Characteristic

// ============================================================================
// Client state
// ============================================================================
static ble_hr_state_t s_state = BLE_HR_STATE_IDLE;
static uint16_t s_conn_handle = 0;
static bool s_initialized = false;

// Discovered characteristic handles
static uint16_t s_hr_measurement_handle = 0;

// ============================================================================
// Forward declarations
// ============================================================================
static int ble_hr_gap_event(struct ble_gap_event *event, void *arg);
static int ble_hr_gatt_disc_svc_cb(uint16_t conn_handle,
                                    const struct ble_gatt_error *error,
                                    const struct ble_gatt_svc *service,
                                    void *arg);
static int ble_hr_gatt_disc_chr_cb(uint16_t conn_handle,
                                    const struct ble_gatt_error *error,
                                    const struct ble_gatt_chr *chr,
                                    void *arg);

// ============================================================================
// Helper functions
// ============================================================================

/**
 * Parse heart rate value from Heart Rate Measurement characteristic
 * Per BLE HR Service spec:
 * - First byte is flags
 * - If bit 0 of flags is 0: HR is uint8 in next byte
 * - If bit 0 of flags is 1: HR is uint16 in next two bytes
 */
static uint8_t parse_heart_rate(const uint8_t *data, uint16_t len) {
    if (data == NULL || len < 2) {
        return 0;
    }
    
    uint8_t flags = data[0];
    uint16_t hr = 0;
    
    if (flags & 0x01) {
        // Heart rate is uint16
        if (len >= 3) {
            hr = data[1] | (data[2] << 8);
        }
    } else {
        // Heart rate is uint8
        hr = data[1];
    }
    
    // Validate heart rate range (30-220 bpm considered valid)
    if (hr < 30 || hr > 220) {
        return 0;  // Invalid reading, likely sensor error
    }
    
    return (uint8_t)hr;
}

/**
 * Subscribe to heart rate notifications
 */
static void subscribe_to_hr_notifications(void) {
    if (s_hr_measurement_handle == 0) {
        ESP_LOGE(TAG, "HR measurement handle not discovered");
        s_state = BLE_HR_STATE_ERROR;
        return;
    }
    
    // The CCCD (Client Characteristic Configuration Descriptor) handle is typically
    // the characteristic value handle + 1
    // Note: This is a common convention but not guaranteed by BLE spec
    
    ESP_LOGI(TAG, "Subscribing to HR notifications on handle %d", s_hr_measurement_handle);
    
    // Enable notifications by writing to CCCD
    // CCCD handle is typically val_handle + 1
    uint16_t cccd_handle = s_hr_measurement_handle + 1;
    uint8_t cccd_value[2] = {0x01, 0x00};  // Enable notifications
    
    int rc = ble_gattc_write_flat(s_conn_handle, cccd_handle, 
                                   cccd_value, sizeof(cccd_value),
                                   NULL, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to subscribe to notifications: %d", rc);
        s_state = BLE_HR_STATE_ERROR;
    } else {
        ESP_LOGI(TAG, "Subscribed to HR notifications");
        s_state = BLE_HR_STATE_CONNECTED;
    }
}

// ============================================================================
// GATT Discovery Callbacks
// ============================================================================

static int ble_hr_gatt_disc_svc_cb(uint16_t conn_handle,
                                    const struct ble_gatt_error *error,
                                    const struct ble_gatt_svc *service,
                                    void *arg) {
    if (error->status == BLE_HS_EDONE) {
        // Service discovery complete, now discover characteristics
        ESP_LOGI(TAG, "Service discovery complete, discovering characteristics...");
        
        int rc = ble_gattc_disc_all_chrs(conn_handle, 1, 0xFFFF,
                                          ble_hr_gatt_disc_chr_cb, NULL);
        if (rc != 0) {
            ESP_LOGE(TAG, "Failed to start characteristic discovery: %d", rc);
        }
        return 0;
    }
    
    if (error->status != 0) {
        ESP_LOGE(TAG, "Service discovery error: %d", error->status);
        return 0;
    }
    
    // Check if this is the Heart Rate Service
    if (service != NULL) {
        if (ble_uuid_u16(&service->uuid.u) == HRS_SERVICE_UUID) {
            ESP_LOGI(TAG, "Found Heart Rate Service (start=%d, end=%d)",
                     service->start_handle, service->end_handle);
        }
    }
    
    return 0;
}

static int ble_hr_gatt_disc_chr_cb(uint16_t conn_handle,
                                    const struct ble_gatt_error *error,
                                    const struct ble_gatt_chr *chr,
                                    void *arg) {
    if (error->status == BLE_HS_EDONE) {
        // Characteristic discovery complete
        ESP_LOGI(TAG, "Characteristic discovery complete");
        
        if (s_hr_measurement_handle != 0) {
            // Subscribe to notifications
            subscribe_to_hr_notifications();
        } else {
            ESP_LOGW(TAG, "HR Measurement characteristic not found");
            s_state = BLE_HR_STATE_ERROR;
        }
        return 0;
    }
    
    if (error->status != 0) {
        ESP_LOGE(TAG, "Characteristic discovery error: %d", error->status);
        return 0;
    }
    
    // Check if this is the Heart Rate Measurement characteristic
    if (chr != NULL) {
        uint16_t uuid16 = ble_uuid_u16(&chr->uuid.u);
        if (uuid16 == HRS_HEART_RATE_MEASUREMENT_UUID) {
            ESP_LOGI(TAG, "Found HR Measurement characteristic (val_handle=%d)",
                     chr->val_handle);
            s_hr_measurement_handle = chr->val_handle;
        }
    }
    
    return 0;
}

// ============================================================================
// GAP Event Handler
// ============================================================================

static int ble_hr_gap_event(struct ble_gap_event *event, void *arg) {
    struct ble_gap_conn_desc desc;
    int rc;
    
    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            // Device discovered during scan
            if (event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
                event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) {
                
                // Check if device advertises Heart Rate Service
                struct ble_hs_adv_fields fields;
                rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                                  event->disc.length_data);
                if (rc != 0) {
                    return 0;
                }
                
                // Look for Heart Rate Service UUID in advertisement
                bool has_hr_service = false;
                for (int i = 0; i < fields.num_uuids16; i++) {
                    if (ble_uuid_u16(&fields.uuids16[i].u) == HRS_SERVICE_UUID) {
                        has_hr_service = true;
                        break;
                    }
                }
                
                if (has_hr_service) {
                    ESP_LOGI(TAG, "Found HR monitor, connecting...");
                    
                    // Stop scanning
                    ble_gap_disc_cancel();
                    
                    // Connect to device
                    s_state = BLE_HR_STATE_CONNECTING;
                    rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &event->disc.addr,
                                          BLE_HR_CONNECT_TIMEOUT_MS, NULL, ble_hr_gap_event, NULL);
                    if (rc != 0) {
                        ESP_LOGE(TAG, "Failed to connect: %d", rc);
                        s_state = BLE_HR_STATE_ERROR;
                    }
                }
            }
            break;
            
        case BLE_GAP_EVENT_DISC_COMPLETE:
            ESP_LOGI(TAG, "Scan complete (reason=%d)", event->disc_complete.reason);
            if (s_state == BLE_HR_STATE_SCANNING) {
                s_state = BLE_HR_STATE_IDLE;
            }
            break;
            
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "Connected to HR monitor");
                s_conn_handle = event->connect.conn_handle;
                
                // Get connection info
                rc = ble_gap_conn_find(s_conn_handle, &desc);
                if (rc == 0) {
                    ESP_LOGI(TAG, "Peer address: %02x:%02x:%02x:%02x:%02x:%02x",
                             desc.peer_ota_addr.val[5], desc.peer_ota_addr.val[4],
                             desc.peer_ota_addr.val[3], desc.peer_ota_addr.val[2],
                             desc.peer_ota_addr.val[1], desc.peer_ota_addr.val[0]);
                }
                
                // Start service discovery
                ESP_LOGI(TAG, "Discovering services...");
                rc = ble_gattc_disc_all_svcs(s_conn_handle, 
                                              ble_hr_gatt_disc_svc_cb, NULL);
                if (rc != 0) {
                    ESP_LOGE(TAG, "Failed to start service discovery: %d", rc);
                    s_state = BLE_HR_STATE_ERROR;
                }
            } else {
                ESP_LOGE(TAG, "Connection failed: %d", event->connect.status);
                s_state = BLE_HR_STATE_ERROR;
            }
            break;
            
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected from HR monitor (reason=%d)",
                     event->disconnect.reason);
            s_conn_handle = 0;
            s_hr_measurement_handle = 0;
            s_state = BLE_HR_STATE_IDLE;
            
            // Attempt to reconnect by starting scan again (only if still initialized)
            if (s_initialized) {
                ESP_LOGI(TAG, "Restarting scan for HR monitors...");
                ble_hr_client_start_scan();
            }
            break;
            
        case BLE_GAP_EVENT_NOTIFY_RX:
            // Received notification from HR monitor
            if (event->notify_rx.attr_handle == s_hr_measurement_handle) {
                uint8_t hr = parse_heart_rate(
                    event->notify_rx.om->om_data,
                    OS_MBUF_PKTLEN(event->notify_rx.om));
                if (hr > 0) {
                    ESP_LOGD(TAG, "Heart rate notification: %d bpm", hr);
                    hr_receiver_update(hr);
                }
            }
            break;
            
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU update: %d", event->mtu.value);
            break;
            
        default:
            ESP_LOGD(TAG, "GAP event: %d", event->type);
            break;
    }
    
    return 0;
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t ble_hr_client_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }
    
    s_state = BLE_HR_STATE_IDLE;
    s_conn_handle = 0;
    s_hr_measurement_handle = 0;
    s_initialized = true;
    
    ESP_LOGI(TAG, "BLE HR client initialized");
    return ESP_OK;
}

void ble_hr_client_deinit(void) {
    if (!s_initialized) {
        return;
    }
    
    ble_hr_client_disconnect();
    s_initialized = false;
    
    ESP_LOGI(TAG, "BLE HR client deinitialized");
}

esp_err_t ble_hr_client_start_scan(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_state == BLE_HR_STATE_CONNECTED || s_state == BLE_HR_STATE_CONNECTING) {
        ESP_LOGW(TAG, "Already connected or connecting");
        return ESP_OK;
    }
    
    struct ble_gap_disc_params disc_params = {
        .itvl = BLE_GAP_SCAN_FAST_INTERVAL_MIN,
        .window = BLE_GAP_SCAN_FAST_WINDOW,
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .limited = 0,
        .passive = 0,
        .filter_duplicates = 1,
    };
    
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &disc_params,
                          ble_hr_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start scan: %d", rc);
        return ESP_FAIL;
    }
    
    s_state = BLE_HR_STATE_SCANNING;
    ESP_LOGI(TAG, "Scanning for HR monitors...");
    
    return ESP_OK;
}

void ble_hr_client_stop_scan(void) {
    if (s_state == BLE_HR_STATE_SCANNING) {
        ble_gap_disc_cancel();
        s_state = BLE_HR_STATE_IDLE;
        ESP_LOGI(TAG, "Scan stopped");
    }
}

bool ble_hr_client_is_connected(void) {
    return s_state == BLE_HR_STATE_CONNECTED;
}

ble_hr_state_t ble_hr_client_get_state(void) {
    return s_state;
}

void ble_hr_client_disconnect(void) {
    if (s_conn_handle != 0) {
        int rc = ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0) {
            ESP_LOGW(TAG, "Failed to disconnect: %d", rc);
        }
    }
    
    ble_hr_client_stop_scan();
    
    s_conn_handle = 0;
    s_hr_measurement_handle = 0;
    s_state = BLE_HR_STATE_IDLE;
}
