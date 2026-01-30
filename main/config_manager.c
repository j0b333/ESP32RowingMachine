/**
 * @file config_manager.c
 * @brief NVS persistent storage for configuration
 */

#include "config_manager.h"
#include "app_config.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "CONFIG";

/**
 * Initialize NVS and config manager
 */
esp_err_t config_manager_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs to be erased, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Config manager initialized");
    return ESP_OK;
}

/**
 * Get default configuration values
 */
void config_manager_get_defaults(config_t *config) {
    memset(config, 0, sizeof(config_t));
    
    // Physics parameters
    config->moment_of_inertia = DEFAULT_MOMENT_OF_INERTIA;
    config->initial_drag_coefficient = DEFAULT_DRAG_COEFFICIENT;
    config->distance_calibration_factor = DEFAULT_DISTANCE_PER_REV;
    
    // Calibration settings
    config->auto_calibrate_drag = true;
    config->calibration_row_count = 50;
    
    // User settings
    config->user_weight_kg = DEFAULT_USER_WEIGHT_KG;
    config->user_age = 30;
    
    // Detection thresholds
    config->drive_start_threshold_rad_s = DRIVE_START_VELOCITY_THRESHOLD;
    config->drive_accel_threshold_rad_s2 = DRIVE_ACCELERATION_THRESHOLD;
    config->recovery_threshold_rad_s = RECOVERY_VELOCITY_THRESHOLD;
    config->idle_timeout_ms = IDLE_TIMEOUT_MS;
    
    // Network settings - AP mode
    strncpy(config->wifi_ssid, WIFI_AP_SSID_DEFAULT, sizeof(config->wifi_ssid) - 1);
    strncpy(config->wifi_password, WIFI_AP_PASS_DEFAULT, sizeof(config->wifi_password) - 1);
    // STA mode - not configured by default
    config->sta_ssid[0] = '\0';
    config->sta_password[0] = '\0';
    config->sta_configured = false;
    strncpy(config->device_name, BLE_DEVICE_NAME_DEFAULT, sizeof(config->device_name) - 1);
    config->wifi_enabled = true;
    config->ble_enabled = true;
    
    // Display settings
    config->show_power = true;
    config->show_calories = true;
    strncpy(config->units, "metric", sizeof(config->units) - 1);
    
    // Auto-pause settings (default 5 seconds)
    config->auto_pause_seconds = 5;
}

/**
 * Load configuration from NVS
 */
esp_err_t config_manager_load(config_t *config) {
    nvs_handle_t handle;
    esp_err_t ret;
    
    // Start with defaults
    config_manager_get_defaults(config);
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved config found, using defaults");
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Read individual values, keeping defaults if not found
    size_t len;
    
    // Physics parameters
    nvs_get_u32(handle, "moi_u32", (uint32_t*)&config->moment_of_inertia);
    nvs_get_u32(handle, "drag_u32", (uint32_t*)&config->initial_drag_coefficient);
    nvs_get_u32(handle, "dist_cal", (uint32_t*)&config->distance_calibration_factor);
    
    // User settings
    nvs_get_u32(handle, "weight_u32", (uint32_t*)&config->user_weight_kg);
    nvs_get_u8(handle, "user_age", &config->user_age);
    
    // Network settings - AP mode
    len = sizeof(config->wifi_ssid);
    nvs_get_str(handle, "wifi_ssid", config->wifi_ssid, &len);
    
    len = sizeof(config->wifi_password);
    nvs_get_str(handle, "wifi_pass", config->wifi_password, &len);
    
    // Network settings - STA mode
    // First read the sta_cfg flag to determine if STA was configured
    uint8_t sta_cfg = 0;
    nvs_get_u8(handle, "sta_cfg", &sta_cfg);
    config->sta_configured = (sta_cfg != 0);
    
    // Then load the credentials if configured
    if (config->sta_configured) {
        len = sizeof(config->sta_ssid);
        nvs_get_str(handle, "sta_ssid", config->sta_ssid, &len);
        
        len = sizeof(config->sta_password);
        nvs_get_str(handle, "sta_pass", config->sta_password, &len);
        
        // Double-check: if SSID is empty, it's not really configured
        if (strlen(config->sta_ssid) == 0) {
            config->sta_configured = false;
        }
    }
    
    len = sizeof(config->device_name);
    nvs_get_str(handle, "dev_name", config->device_name, &len);
    
    uint8_t wifi_en = config->wifi_enabled ? 1 : 0;
    nvs_get_u8(handle, "wifi_en", &wifi_en);
    config->wifi_enabled = wifi_en != 0;
    
    uint8_t ble_en = config->ble_enabled ? 1 : 0;
    nvs_get_u8(handle, "ble_en", &ble_en);
    config->ble_enabled = ble_en != 0;
    
    // Display settings
    uint8_t show_power = config->show_power ? 1 : 0;
    nvs_get_u8(handle, "show_power", &show_power);
    config->show_power = show_power != 0;
    
    uint8_t show_cal = config->show_calories ? 1 : 0;
    nvs_get_u8(handle, "show_cal", &show_cal);
    config->show_calories = show_cal != 0;
    
    len = sizeof(config->units);
    nvs_get_str(handle, "units", config->units, &len);
    
    // Auto-pause settings
    nvs_get_u8(handle, "auto_pause", &config->auto_pause_seconds);
    
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Configuration loaded from NVS (STA configured: %s)", 
             config->sta_configured ? "yes" : "no");
    return ESP_OK;
}

/**
 * Save configuration to NVS
 */
esp_err_t config_manager_save(const config_t *config) {
    nvs_handle_t handle;
    esp_err_t ret;
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Save physics parameters (store floats as uint32_t using union for type safety)
    union { float f; uint32_t u; } conv;
    
    conv.f = config->moment_of_inertia;
    nvs_set_u32(handle, "moi_u32", conv.u);
    
    conv.f = config->initial_drag_coefficient;
    nvs_set_u32(handle, "drag_u32", conv.u);
    
    conv.f = config->distance_calibration_factor;
    nvs_set_u32(handle, "dist_cal", conv.u);
    
    // Save user settings
    conv.f = config->user_weight_kg;
    nvs_set_u32(handle, "weight_u32", conv.u);
    nvs_set_u8(handle, "user_age", config->user_age);
    
    // Save network settings - AP mode
    nvs_set_str(handle, "wifi_ssid", config->wifi_ssid);
    nvs_set_str(handle, "wifi_pass", config->wifi_password);
    
    // Save network settings - STA mode
    nvs_set_str(handle, "sta_ssid", config->sta_ssid);
    nvs_set_str(handle, "sta_pass", config->sta_password);
    nvs_set_u8(handle, "sta_cfg", config->sta_configured ? 1 : 0);
    
    nvs_set_str(handle, "dev_name", config->device_name);
    nvs_set_u8(handle, "wifi_en", config->wifi_enabled ? 1 : 0);
    nvs_set_u8(handle, "ble_en", config->ble_enabled ? 1 : 0);
    
    // Save display settings
    nvs_set_u8(handle, "show_power", config->show_power ? 1 : 0);
    nvs_set_u8(handle, "show_cal", config->show_calories ? 1 : 0);
    nvs_set_str(handle, "units", config->units);
    
    // Save auto-pause settings
    nvs_set_u8(handle, "auto_pause", config->auto_pause_seconds);
    
    // Commit changes
    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
    }
    
    nvs_close(handle);
    
    ESP_LOGI(TAG, "Configuration saved to NVS");
    return ret;
}

/**
 * Reset configuration to defaults
 */
esp_err_t config_manager_reset_defaults(config_t *config) {
    // Get defaults
    config_manager_get_defaults(config);
    
    // Erase NVS namespace
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }
    
    ESP_LOGI(TAG, "Configuration reset to defaults");
    return ESP_OK;
}
