/**
 * @file ble_ftms_server.c
 * @brief Bluetooth Low Energy Fitness Machine Service (FTMS) implementation
 * 
 * Specification: Bluetooth SIG FTMS 1.0
 * Service UUID: 0x1826 (Fitness Machine)
 * Rower Data Characteristic UUID: 0x2AD1
 * 
 * Compatible with apps: Kinomap, EXR, MyHomeFit, Concept2 ErgData, etc.
 * Compatible with ESP-IDF 6.0+
 */

#include "ble_ftms_server.h"
#include "app_config.h"

#include "esp_log.h"
#include "nvs_flash.h"

/* NimBLE includes */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <string.h>

static const char *TAG = "BLE_FTMS";

// ============================================================================
// FTMS Service and Characteristic UUIDs (16-bit standard UUIDs)
// ============================================================================
// Fitness Machine Service: 0x1826
// Rower Data Characteristic: 0x2AD1
// Fitness Machine Feature: 0x2ACC
// Fitness Machine Control Point: 0x2AD9
// Fitness Machine Status: 0x2ADA

#define FTMS_SERVICE_UUID               0x1826
#define FTMS_ROWER_DATA_UUID            0x2AD1
#define FTMS_FITNESS_MACHINE_FEATURE_UUID 0x2ACC
#define FTMS_CONTROL_POINT_UUID         0x2AD9
#define FTMS_STATUS_UUID                0x2ADA

// Device Information Service
#define DIS_SERVICE_UUID                0x180A
#define DIS_MANUFACTURER_NAME_UUID      0x2A29
#define DIS_MODEL_NUMBER_UUID           0x2A24
#define DIS_FIRMWARE_REV_UUID           0x2A26

// ============================================================================
// FTMS Feature Flags
// ============================================================================
// Rower Data Field Flags (per FTMS spec)
#define ROWER_MORE_DATA_FLAG            (1 << 0)  // More data follows
#define ROWER_AVG_STROKE_RATE_FLAG      (1 << 1)  // Average stroke rate present
#define ROWER_TOTAL_DISTANCE_FLAG       (1 << 2)  // Total distance present
#define ROWER_INST_PACE_FLAG            (1 << 3)  // Instantaneous pace present
#define ROWER_AVG_PACE_FLAG             (1 << 4)  // Average pace present
#define ROWER_INST_POWER_FLAG           (1 << 5)  // Instantaneous power present
#define ROWER_AVG_POWER_FLAG            (1 << 6)  // Average power present
#define ROWER_RESISTANCE_FLAG           (1 << 7)  // Resistance level present
#define ROWER_EXPENDED_ENERGY_FLAG      (1 << 8)  // Expended energy present
#define ROWER_HEART_RATE_FLAG           (1 << 9)  // Heart rate present
#define ROWER_METABOLIC_FLAG            (1 << 10) // Metabolic equivalent present
#define ROWER_ELAPSED_TIME_FLAG         (1 << 11) // Elapsed time present
#define ROWER_REMAINING_TIME_FLAG       (1 << 12) // Remaining time present

// ============================================================================
// Connection state
// ============================================================================
static uint16_t g_conn_handle = 0;
static bool g_connected = false;
static bool g_notify_enabled = false;
static uint16_t g_rower_data_attr_handle = 0;
static uint8_t g_own_addr_type;

// Device name
static char g_device_name[BLE_DEVICE_NAME_MAX_LEN] = BLE_DEVICE_NAME_DEFAULT;

// ============================================================================
// Forward declarations
// ============================================================================
static void ble_host_task(void *param);
static void ble_on_sync(void);
static void ble_on_reset(int reason);
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static int gatt_svr_chr_access_rower_data(uint16_t conn_handle, uint16_t attr_handle,
                                           struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_access_feature(uint16_t conn_handle, uint16_t attr_handle,
                                        struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_access_dis(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg);

// ============================================================================
// GATT Service Definition
// ============================================================================

// Fitness Machine Feature value (indicates supported features)
// Bits 0-3: Fitness Machine Features
// For rower: Rower Data supported (bit 0)
static const uint8_t fitness_machine_feature[] = {
    0x00, 0x00, 0x00, 0x00,  // Fitness Machine Features (32-bit)
    0x00, 0x00, 0x00, 0x00   // Target Setting Features (32-bit)
};

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    // Fitness Machine Service
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(FTMS_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            // Rower Data Characteristic (notify only)
            {
                .uuid = BLE_UUID16_DECLARE(FTMS_ROWER_DATA_UUID),
                .access_cb = gatt_svr_chr_access_rower_data,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &g_rower_data_attr_handle,
            },
            // Fitness Machine Feature (read only)
            {
                .uuid = BLE_UUID16_DECLARE(FTMS_FITNESS_MACHINE_FEATURE_UUID),
                .access_cb = gatt_svr_chr_access_feature,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                0, // Terminator
            },
        },
    },
    // Device Information Service
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(DIS_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            // Manufacturer Name
            {
                .uuid = BLE_UUID16_DECLARE(DIS_MANUFACTURER_NAME_UUID),
                .access_cb = gatt_svr_chr_access_dis,
                .flags = BLE_GATT_CHR_F_READ,
            },
            // Model Number
            {
                .uuid = BLE_UUID16_DECLARE(DIS_MODEL_NUMBER_UUID),
                .access_cb = gatt_svr_chr_access_dis,
                .flags = BLE_GATT_CHR_F_READ,
            },
            // Firmware Revision
            {
                .uuid = BLE_UUID16_DECLARE(DIS_FIRMWARE_REV_UUID),
                .access_cb = gatt_svr_chr_access_dis,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                0, // Terminator
            },
        },
    },
    {
        0, // Terminator
    },
};

// ============================================================================
// GATT Access Callbacks
// ============================================================================

static int gatt_svr_chr_access_rower_data(uint16_t conn_handle, uint16_t attr_handle,
                                           struct ble_gatt_access_ctxt *ctxt, void *arg) {
    // Rower data is notify-only, no read access needed
    return 0;
}

static int gatt_svr_chr_access_feature(uint16_t conn_handle, uint16_t attr_handle,
                                        struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        int rc = os_mbuf_append(ctxt->om, fitness_machine_feature, 
                                sizeof(fitness_machine_feature));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

static int gatt_svr_chr_access_dis(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg) {
    uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);
    const char *value = "";
    
    switch (uuid16) {
        case DIS_MANUFACTURER_NAME_UUID:
            value = "ESP32 Rowing Monitor";
            break;
        case DIS_MODEL_NUMBER_UUID:
            value = "CrivitRower-001";
            break;
        case DIS_FIRMWARE_REV_UUID:
            value = APP_VERSION_STRING;
            break;
        default:
            return BLE_ATT_ERR_UNLIKELY;
    }
    
    int rc = os_mbuf_append(ctxt->om, value, strlen(value));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// ============================================================================
// Build FTMS Rower Data Packet
// ============================================================================

/**
 * Build FTMS Rower Data packet according to Bluetooth SIG specification
 * 
 * Data format (per FTMS spec):
 * Byte 0-1: Flags (uint16) - indicates which fields are present
 * Following bytes depend on flags:
 * - Stroke Rate: uint8 (0.5 strokes/min resolution)
 * - Stroke Count: uint16
 * - Average Stroke Rate: uint8 (if flag set)
 * - Total Distance: uint24 (meters)
 * - Instantaneous Pace: uint16 (seconds per 500m, 0 = not available)
 * - Average Pace: uint16 (seconds per 500m)
 * - Instantaneous Power: sint16 (watts)
 * - Average Power: sint16 (watts)
 * - Resistance Level: sint16
 * - Total Energy: uint16 (kcal)
 * - Energy Per Hour: uint16 (kcal/h)
 * - Energy Per Minute: uint8 (kcal/min)
 * - Heart Rate: uint8 (bpm)
 * - Metabolic Equivalent: uint8 (0.1 resolution)
 * - Elapsed Time: uint16 (seconds)
 * - Remaining Time: uint16 (seconds)
 */
static size_t build_rower_data_packet(const rowing_metrics_t *metrics, uint8_t *packet) {
    size_t offset = 0;
    
    // Set flags for fields we're including
    uint16_t flags = 0;
    flags |= ROWER_TOTAL_DISTANCE_FLAG;
    flags |= ROWER_INST_PACE_FLAG;
    flags |= ROWER_AVG_PACE_FLAG;
    flags |= ROWER_INST_POWER_FLAG;
    flags |= ROWER_AVG_POWER_FLAG;
    flags |= ROWER_EXPENDED_ENERGY_FLAG;
    flags |= ROWER_ELAPSED_TIME_FLAG;
    
    // Write flags (little endian)
    packet[offset++] = (uint8_t)(flags & 0xFF);
    packet[offset++] = (uint8_t)((flags >> 8) & 0xFF);
    
    // Stroke Rate (uint8, 0.5 SPM resolution) - always present
    uint8_t stroke_rate = (uint8_t)(metrics->stroke_rate_spm * 2.0f);
    packet[offset++] = stroke_rate;
    
    // Stroke Count (uint16) - always present
    uint16_t stroke_count = (uint16_t)metrics->stroke_count;
    packet[offset++] = (uint8_t)(stroke_count & 0xFF);
    packet[offset++] = (uint8_t)((stroke_count >> 8) & 0xFF);
    
    // Total Distance (uint24, meters)
    uint32_t distance = (uint32_t)metrics->total_distance_meters;
    packet[offset++] = (uint8_t)(distance & 0xFF);
    packet[offset++] = (uint8_t)((distance >> 8) & 0xFF);
    packet[offset++] = (uint8_t)((distance >> 16) & 0xFF);
    
    // Instantaneous Pace (uint16, seconds per 500m)
    uint16_t pace = (uint16_t)metrics->instantaneous_pace_sec_500m;
    if (pace > 9999) pace = 0;  // 0 = not available
    packet[offset++] = (uint8_t)(pace & 0xFF);
    packet[offset++] = (uint8_t)((pace >> 8) & 0xFF);
    
    // Average Pace (uint16, seconds per 500m)
    uint16_t avg_pace = (uint16_t)metrics->average_pace_sec_500m;
    if (avg_pace > 9999) avg_pace = 0;
    packet[offset++] = (uint8_t)(avg_pace & 0xFF);
    packet[offset++] = (uint8_t)((avg_pace >> 8) & 0xFF);
    
    // Instantaneous Power (sint16, watts)
    int16_t power = (int16_t)metrics->instantaneous_power_watts;
    packet[offset++] = (uint8_t)(power & 0xFF);
    packet[offset++] = (uint8_t)((power >> 8) & 0xFF);
    
    // Average Power (sint16, watts)
    int16_t avg_power = (int16_t)metrics->average_power_watts;
    packet[offset++] = (uint8_t)(avg_power & 0xFF);
    packet[offset++] = (uint8_t)((avg_power >> 8) & 0xFF);
    
    // Expended Energy: Total Energy (uint16, kcal)
    uint16_t energy = (uint16_t)metrics->total_calories;
    packet[offset++] = (uint8_t)(energy & 0xFF);
    packet[offset++] = (uint8_t)((energy >> 8) & 0xFF);
    
    // Energy Per Hour (uint16, kcal/h)
    uint16_t energy_per_hour = (uint16_t)metrics->calories_per_hour;
    packet[offset++] = (uint8_t)(energy_per_hour & 0xFF);
    packet[offset++] = (uint8_t)((energy_per_hour >> 8) & 0xFF);
    
    // Energy Per Minute (uint8, kcal/min)
    uint8_t energy_per_min = (uint8_t)(metrics->calories_per_hour / 60.0f);
    packet[offset++] = energy_per_min;
    
    // Elapsed Time (uint16, seconds)
    uint16_t elapsed_s = (uint16_t)(metrics->elapsed_time_ms / 1000);
    packet[offset++] = (uint8_t)(elapsed_s & 0xFF);
    packet[offset++] = (uint8_t)((elapsed_s >> 8) & 0xFF);
    
    return offset;
}

// ============================================================================
// BLE Event Handlers
// ============================================================================

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "Connection %s; status=%d",
                     event->connect.status == 0 ? "established" : "failed",
                     event->connect.status);
            if (event->connect.status == 0) {
                g_conn_handle = event->connect.conn_handle;
                g_connected = true;
            } else {
                // Connection failed, resume advertising
                ble_ftms_start_advertising();
            }
            break;
            
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnect; reason=%d", event->disconnect.reason);
            g_connected = false;
            g_notify_enabled = false;
            g_conn_handle = 0;
            // Resume advertising
            ble_ftms_start_advertising();
            break;
            
        case BLE_GAP_EVENT_ADV_COMPLETE:
            ESP_LOGD(TAG, "Advertising complete");
            break;
            
        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "Subscribe event; cur_notify=%d",
                     event->subscribe.cur_notify);
            if (event->subscribe.attr_handle == g_rower_data_attr_handle) {
                g_notify_enabled = event->subscribe.cur_notify;
                ESP_LOGI(TAG, "Rower data notifications %s",
                         g_notify_enabled ? "enabled" : "disabled");
            }
            break;
            
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU update: conn_handle=%d, mtu=%d",
                     event->mtu.conn_handle, event->mtu.value);
            break;
            
        default:
            ESP_LOGD(TAG, "GAP event: %d", event->type);
            break;
    }
    
    return 0;
}

static void ble_on_sync(void) {
    int rc;
    
    // Determine address type
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to ensure address: %d", rc);
        return;
    }
    
    rc = ble_hs_id_infer_auto(0, &g_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to infer address type: %d", rc);
        return;
    }
    
    // Print address
    uint8_t addr_val[6] = {0};
    ble_hs_id_copy_addr(g_own_addr_type, addr_val, NULL);
    ESP_LOGI(TAG, "BLE address: %02x:%02x:%02x:%02x:%02x:%02x",
             addr_val[5], addr_val[4], addr_val[3],
             addr_val[2], addr_val[1], addr_val[0]);
    
    // Start advertising
    ble_ftms_start_advertising();
}

static void ble_on_reset(int reason) {
    ESP_LOGW(TAG, "BLE host reset; reason=%d", reason);
}

static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();  // This function will return only when nimble_port_stop() is called
    nimble_port_freertos_deinit();
}

// ============================================================================
// Public API
// ============================================================================

esp_err_t ble_ftms_init(const char *device_name) {
    int rc;
    
    // Store device name
    if (device_name != NULL && strlen(device_name) > 0) {
        strncpy(g_device_name, device_name, sizeof(g_device_name) - 1);
        g_device_name[sizeof(g_device_name) - 1] = '\0';
    }
    
    // Initialize NimBLE
    rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init nimble port: %d", rc);
        return ESP_FAIL;
    }
    
    // Configure NimBLE host
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;
    
    // Initialize services
    ble_svc_gap_init();
    ble_svc_gatt_init();
    
    // Set device name
    rc = ble_svc_gap_device_name_set(g_device_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to set device name: %d", rc);
    }
    
    // Register GATT services
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count GATT config: %d", rc);
        return ESP_FAIL;
    }
    
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add GATT services: %d", rc);
        return ESP_FAIL;
    }
    
    // Start NimBLE host task
    nimble_port_freertos_init(ble_host_task);
    
    ESP_LOGI(TAG, "BLE FTMS initialized: %s", g_device_name);
    
    return ESP_OK;
}

void ble_ftms_deinit(void) {
    int rc = nimble_port_stop();
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to stop nimble port: %d", rc);
    }
    
    rc = nimble_port_deinit();
    if (rc != ESP_OK) {
        ESP_LOGW(TAG, "Failed to deinit nimble port: %d", rc);
    }
    
    ESP_LOGI(TAG, "BLE FTMS deinitialized");
}

esp_err_t ble_ftms_start_advertising(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;
    
    memset(&fields, 0, sizeof(fields));
    
    // Flags: general discoverable, no BR/EDR
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    
    // Include device name
    fields.name = (uint8_t *)g_device_name;
    fields.name_len = strlen(g_device_name);
    fields.name_is_complete = 1;
    
    // Include FTMS service UUID
    static ble_uuid16_t ftms_uuid = BLE_UUID16_INIT(FTMS_SERVICE_UUID);
    fields.uuids16 = &ftms_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set advertising fields: %d", rc);
        return ESP_FAIL;
    }
    
    // Set advertising parameters
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  // Undirected connectable
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  // General discoverable
    adv_params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    adv_params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;
    
    rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising: %d", rc);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "BLE advertising started");
    return ESP_OK;
}

void ble_ftms_stop_advertising(void) {
    int rc = ble_gap_adv_stop();
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to stop advertising: %d", rc);
    }
}

esp_err_t ble_ftms_notify_metrics(const rowing_metrics_t *metrics) {
    if (!g_connected || !g_notify_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint8_t packet[32];
    size_t packet_len = build_rower_data_packet(metrics, packet);
    
    struct os_mbuf *om = ble_hs_mbuf_from_flat(packet, packet_len);
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf");
        return ESP_ERR_NO_MEM;
    }
    
    int rc = ble_gattc_notify_custom(g_conn_handle, g_rower_data_attr_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to send notification: %d", rc);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

bool ble_ftms_is_connected(void) {
    return g_connected;
}

uint16_t ble_ftms_get_conn_handle(void) {
    return g_conn_handle;
}
