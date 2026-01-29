/**
 * @file session_manager.h
 * @brief Session tracking and history management
 */

#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include "esp_err.h"
#include "rowing_physics.h"

/**
 * Initialize session manager
 * @return ESP_OK on success
 */
esp_err_t session_manager_init(void);

/**
 * Start a new session
 * @param metrics Pointer to metrics structure
 * @return ESP_OK on success
 */
esp_err_t session_manager_start_session(rowing_metrics_t *metrics);

/**
 * End current session and save to history
 * @param metrics Pointer to metrics structure
 * @return ESP_OK on success
 */
esp_err_t session_manager_end_session(rowing_metrics_t *metrics);

/**
 * Get session record by ID
 * @param session_id Session ID to retrieve
 * @param record Pointer to record structure to populate
 * @return ESP_OK if found
 */
esp_err_t session_manager_get_session(uint32_t session_id, session_record_t *record);

/**
 * Get number of stored sessions
 * @return Number of sessions in history
 */
uint32_t session_manager_get_session_count(void);

/**
 * Clear all session history
 * @return ESP_OK on success
 */
esp_err_t session_manager_clear_history(void);

/**
 * Get current session ID
 * @return Current session ID or 0 if no active session
 */
uint32_t session_manager_get_current_session_id(void);

/**
 * Delete a specific session from history
 * @param session_id Session ID to delete
 * @return ESP_OK if deleted, ESP_ERR_NOT_FOUND if session doesn't exist
 */
esp_err_t session_manager_delete_session(uint32_t session_id);

#endif // SESSION_MANAGER_H
