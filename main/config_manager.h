/**
 * @file config_manager.h
 * @brief NVS persistent storage for configuration
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "esp_err.h"
#include "rowing_physics.h"

/**
 * Initialize NVS and config manager
 * @return ESP_OK on success
 */
esp_err_t config_manager_init(void);

/**
 * Load configuration from NVS
 * @param config Pointer to config structure to populate
 * @return ESP_OK on success
 */
esp_err_t config_manager_load(config_t *config);

/**
 * Save configuration to NVS
 * @param config Pointer to config structure to save
 * @return ESP_OK on success
 */
esp_err_t config_manager_save(const config_t *config);

/**
 * Reset configuration to defaults
 * @param config Pointer to config structure to reset
 * @return ESP_OK on success
 */
esp_err_t config_manager_reset_defaults(config_t *config);

/**
 * Get default configuration
 * @param config Pointer to config structure to populate
 */
void config_manager_get_defaults(config_t *config);

#endif // CONFIG_MANAGER_H
