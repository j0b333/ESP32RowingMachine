/**
 * @file web_server.c
 * @brief HTTP server with WebSocket for real-time metrics streaming
 * 
 * Features:
 * - Serves embedded HTML/CSS/JS files
 * - WebSocket for real-time metrics streaming
 * - REST API for configuration
 * - Session control endpoints
 * 
 * Compatible with ESP-IDF 6.0+
 * Thread-safe WebSocket client management with mutex
 */

#include "web_server.h"
#include "app_config.h"
#include "metrics_calculator.h"
#include "config_manager.h"
#include "hr_receiver.h"
#include "session_manager.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>

static const char *TAG = "WEB_SERVER";

// HTTP server handle
static httpd_handle_t g_server = NULL;

// WebSocket file descriptors for connected clients
#define MAX_WS_CLIENTS 4
static int g_ws_fds[MAX_WS_CLIENTS] = {-1, -1, -1, -1};

// Mutex for thread-safe WebSocket client list access
static SemaphoreHandle_t g_ws_mutex = NULL;

// Pointers to shared data
static rowing_metrics_t *g_metrics = NULL;
static config_t *g_config = NULL;

// Mutex helper macros
#define WS_MUTEX_TAKE() \
    do { \
        if (g_ws_mutex != NULL) { \
            xSemaphoreTake(g_ws_mutex, portMAX_DELAY); \
        } \
    } while(0)

#define WS_MUTEX_GIVE() \
    do { \
        if (g_ws_mutex != NULL) { \
            xSemaphoreGive(g_ws_mutex); \
        } \
    } while(0)

// ============================================================================
// Embedded Web Content Declarations
// ============================================================================

// These symbols are created by the linker from embedded files
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

extern const char style_css_start[] asm("_binary_style_css_start");
extern const char style_css_end[]   asm("_binary_style_css_end");

extern const char app_js_start[] asm("_binary_app_js_start");
extern const char app_js_end[]   asm("_binary_app_js_end");

extern const char favicon_ico_start[] asm("_binary_favicon_ico_start");
extern const char favicon_ico_end[]   asm("_binary_favicon_ico_end");

// ============================================================================
// URI Handlers
// ============================================================================

/**
 * Serve main HTML page
 */
static esp_err_t index_handler(httpd_req_t *req) {
    const size_t index_html_size = (index_html_end - index_html_start);
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_send(req, index_html_start, index_html_size);
    
    return ESP_OK;
}

/**
 * Serve CSS file
 */
static esp_err_t style_css_handler(httpd_req_t *req) {
    const size_t style_css_size = (style_css_end - style_css_start);
    
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    httpd_resp_send(req, style_css_start, style_css_size);
    
    return ESP_OK;
}

/**
 * Serve JavaScript file
 */
static esp_err_t app_js_handler(httpd_req_t *req) {
    const size_t app_js_size = (app_js_end - app_js_start);
    
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    httpd_resp_send(req, app_js_start, app_js_size);
    
    return ESP_OK;
}

/**
 * Serve favicon
 */
static esp_err_t favicon_handler(httpd_req_t *req) {
    const size_t favicon_size = (favicon_ico_end - favicon_ico_start);
    
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
    httpd_resp_send(req, favicon_ico_start, favicon_size);
    
    return ESP_OK;
}

/**
 * API endpoint: Get current metrics as JSON
 */
static esp_err_t api_metrics_handler(httpd_req_t *req) {
    if (g_metrics == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    char buffer[JSON_BUFFER_SIZE];
    metrics_calculator_to_json(g_metrics, buffer, sizeof(buffer));
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, buffer);
    
    return ESP_OK;
}

/**
 * API endpoint: Get device status
 */
static esp_err_t api_status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddStringToObject(root, "version", APP_VERSION_STRING);
    cJSON_AddStringToObject(root, "device", "Crivit Rowing Monitor");
    cJSON_AddBoolToObject(root, "bleConnected", false);  // TODO: Get from BLE module
    cJSON_AddNumberToObject(root, "wsClients", web_server_get_connection_count());
    cJSON_AddNumberToObject(root, "uptime", (double)(esp_timer_get_time() / 1000000));
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (json_string == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json_string);
    
    free(json_string);
    return ESP_OK;
}

/**
 * API endpoint: Reset session
 */
static esp_err_t api_reset_handler(httpd_req_t *req) {
    if (g_metrics == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    metrics_calculator_reset(g_metrics);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "Session reset");
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);
    
    free(json_string);
    
    ESP_LOGI(TAG, "Session reset via API");
    return ESP_OK;
}

/**
 * API endpoint: Get/Set configuration
 */
static esp_err_t api_config_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        // Return current configuration
        if (g_config == NULL) {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "userWeight", g_config->user_weight_kg);
        cJSON_AddNumberToObject(root, "momentOfInertia", g_config->moment_of_inertia);
        cJSON_AddNumberToObject(root, "distanceCalibration", g_config->distance_calibration_factor);
        cJSON_AddStringToObject(root, "units", g_config->units);
        cJSON_AddBoolToObject(root, "showPower", g_config->show_power);
        cJSON_AddBoolToObject(root, "showCalories", g_config->show_calories);
        
        char *json_string = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json_string);
        
        free(json_string);
        return ESP_OK;
    }
    
    // POST: Update configuration
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    cJSON *root = cJSON_Parse(content);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    
    // Update config values
    cJSON *item;
    if ((item = cJSON_GetObjectItem(root, "userWeight")) != NULL) {
        g_config->user_weight_kg = (float)cJSON_GetNumberValue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "units")) != NULL) {
        strncpy(g_config->units, cJSON_GetStringValue(item), sizeof(g_config->units) - 1);
    }
    if ((item = cJSON_GetObjectItem(root, "showPower")) != NULL) {
        g_config->show_power = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "showCalories")) != NULL) {
        g_config->show_calories = cJSON_IsTrue(item);
    }
    
    cJSON_Delete(root);
    
    // Save to NVS
    config_manager_save(g_config);
    
    // Send response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    
    ESP_LOGI(TAG, "Configuration updated via API");
    return ESP_OK;
}

// ============================================================================
// Heart Rate Endpoints (HeartRateToWeb Compatible)
// ============================================================================

/**
 * POST /hr - Receive heart rate from Galaxy Watch
 * Supports multiple formats:
 * - Raw body: "75"
 * - Query parameter: ?bpm=75 or ?hr=75
 */
static esp_err_t hr_post_handler(httpd_req_t *req) {
    char content[32];
    int hr = 0;
    
    // Check query parameters first
    size_t query_len = httpd_req_get_url_query_len(req);
    if (query_len > 0 && query_len < sizeof(content)) {
        char query[64];
        httpd_req_get_url_query_str(req, query, sizeof(query));
        
        char param_value[16];
        if (httpd_query_key_value(query, "bpm", param_value, sizeof(param_value)) == ESP_OK) {
            hr = atoi(param_value);
        } else if (httpd_query_key_value(query, "hr", param_value, sizeof(param_value)) == ESP_OK) {
            hr = atoi(param_value);
        }
    }
    
    // If no query param, try reading body
    if (hr == 0) {
        int ret = httpd_req_recv(req, content, sizeof(content) - 1);
        if (ret > 0) {
            content[ret] = '\0';
            // Trim whitespace
            char *p = content;
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
            hr = atoi(p);
        }
    }
    
    // Validate and store
    if (hr > 0 && hr <= 220) {
        esp_err_t err = hr_receiver_update((uint8_t)hr);
        if (err == ESP_OK) {
            httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
            httpd_resp_sendstr(req, "OK");
            return ESP_OK;
        }
    }
    
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid HR value");
    return ESP_FAIL;
}

/**
 * GET /hr - Get current heart rate
 * Returns "0" if data is stale
 */
static esp_err_t hr_get_handler(httpd_req_t *req) {
    char response[8];
    uint8_t hr = hr_receiver_get_current();
    snprintf(response, sizeof(response), "%d", hr);
    
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, response);
    
    return ESP_OK;
}

// ============================================================================
// Session Management Endpoints
// ============================================================================

// Maximum number of sessions to return per request
#define MAX_SESSIONS_PER_PAGE 20

/**
 * GET /api/sessions - List all stored sessions
 */
static esp_err_t api_sessions_list_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    cJSON *sessions = cJSON_CreateArray();
    
    uint32_t count = session_manager_get_session_count();
    
    // Get sessions in reverse order (newest first), limit to MAX_SESSIONS_PER_PAGE
    int start = (int)count;
    int end = (count > MAX_SESSIONS_PER_PAGE) ? (int)(count - MAX_SESSIONS_PER_PAGE) : 0;
    
    for (int i = start; i > end; i--) {
        session_record_t record;
        if (session_manager_get_session((uint32_t)i, &record) == ESP_OK) {
            cJSON *session = cJSON_CreateObject();
            cJSON_AddNumberToObject(session, "id", record.session_id);
            cJSON_AddNumberToObject(session, "startTime", (double)record.start_timestamp);
            cJSON_AddNumberToObject(session, "duration", record.duration_seconds);
            cJSON_AddNumberToObject(session, "distance", record.total_distance_meters);
            cJSON_AddNumberToObject(session, "strokes", record.stroke_count);
            cJSON_AddNumberToObject(session, "calories", record.total_calories);
            cJSON_AddNumberToObject(session, "avgPower", record.average_power_watts);
            cJSON_AddNumberToObject(session, "avgPace", record.average_pace_sec_500m);
            cJSON_AddNumberToObject(session, "dragFactor", record.drag_factor);
            cJSON_AddItemToArray(sessions, session);
        }
    }
    
    cJSON_AddItemToObject(root, "sessions", sessions);
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (json_string == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json_string);
    
    free(json_string);
    return ESP_OK;
}

/**
 * GET /api/sessions/{id} - Get session details
 * Note: Currently HR samples are stored in RAM during session and not persisted
 */
static esp_err_t api_session_detail_handler(httpd_req_t *req) {
    // Parse session ID from URI: /api/sessions/123
    const char *uri = req->uri;
    const char *id_start = strrchr(uri, '/');
    if (id_start == NULL || *(id_start + 1) == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid session ID");
        return ESP_FAIL;
    }
    
    // Check if the string after '/' contains only digits
    const char *id_str = id_start + 1;
    for (const char *p = id_str; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid session ID format");
            return ESP_FAIL;
        }
    }
    
    // Session IDs start from 1, not 0
    long session_id_long = strtol(id_str, NULL, 10);
    if (session_id_long <= 0 || session_id_long > UINT32_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid session ID");
        return ESP_FAIL;
    }
    uint32_t session_id = (uint32_t)session_id_long;
    
    session_record_t record;
    if (session_manager_get_session(session_id, &record) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Session not found");
        return ESP_FAIL;
    }
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "id", record.session_id);
    cJSON_AddNumberToObject(root, "startTime", (double)record.start_timestamp);
    cJSON_AddNumberToObject(root, "duration", record.duration_seconds);
    cJSON_AddNumberToObject(root, "distance", record.total_distance_meters);
    cJSON_AddNumberToObject(root, "strokes", record.stroke_count);
    cJSON_AddNumberToObject(root, "calories", record.total_calories);
    cJSON_AddNumberToObject(root, "avgPower", record.average_power_watts);
    cJSON_AddNumberToObject(root, "avgPace", record.average_pace_sec_500m);
    cJSON_AddNumberToObject(root, "dragFactor", record.drag_factor);
    
    // Note: HR samples are currently only available during active session
    cJSON *hrSamples = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "heartRateSamples", hrSamples);
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (json_string == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json_string);
    
    free(json_string);
    return ESP_OK;
}

// ============================================================================
// Workout Control Endpoints
// ============================================================================

/**
 * POST /workout/start - Start a new workout
 */
static esp_err_t workout_start_handler(httpd_req_t *req) {
    if (g_metrics == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    // Start HR recording
    hr_receiver_start_recording();
    
    // Reset metrics for new workout
    metrics_calculator_reset(g_metrics);
    
    // Start a new session
    session_manager_start_session(g_metrics);
    
    uint32_t session_id = session_manager_get_current_session_id();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "started");
    cJSON_AddNumberToObject(root, "sessionId", session_id);
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json_string);
    
    free(json_string);
    
    ESP_LOGI(TAG, "Workout started via API, session #%lu", (unsigned long)session_id);
    return ESP_OK;
}

/**
 * POST /workout/stop - Stop current workout
 */
static esp_err_t workout_stop_handler(httpd_req_t *req) {
    if (g_metrics == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    // Stop HR recording
    hr_receiver_stop_recording();
    
    uint32_t session_id = session_manager_get_current_session_id();
    
    // End the session and save
    session_manager_end_session(g_metrics);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "stopped");
    cJSON_AddNumberToObject(root, "sessionId", session_id);
    cJSON_AddNumberToObject(root, "distance", g_metrics->total_distance_meters);
    cJSON_AddNumberToObject(root, "strokes", g_metrics->stroke_count);
    cJSON_AddNumberToObject(root, "calories", g_metrics->total_calories);
    
    uint8_t avg_hr, max_hr;
    uint16_t hr_count;
    hr_receiver_get_stats(&avg_hr, &max_hr, &hr_count);
    cJSON_AddNumberToObject(root, "hrSamples", hr_count);
    cJSON_AddNumberToObject(root, "avgHeartRate", avg_hr);
    cJSON_AddNumberToObject(root, "maxHeartRate", max_hr);
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json_string);
    
    free(json_string);
    
    ESP_LOGI(TAG, "Workout stopped via API, session #%lu", (unsigned long)session_id);
    return ESP_OK;
}

/**
 * GET /live - Get live workout data
 */
static esp_err_t live_data_handler(httpd_req_t *req) {
    if (g_metrics == NULL || !g_metrics->is_active) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No workout in progress");
        return ESP_FAIL;
    }
    
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddNumberToObject(root, "sessionId", session_manager_get_current_session_id());
    cJSON_AddNumberToObject(root, "distance", g_metrics->total_distance_meters);
    cJSON_AddNumberToObject(root, "strokes", g_metrics->stroke_count);
    cJSON_AddNumberToObject(root, "duration", g_metrics->elapsed_time_ms / 1000);
    cJSON_AddNumberToObject(root, "power", g_metrics->instantaneous_power_watts);
    cJSON_AddNumberToObject(root, "pace", g_metrics->instantaneous_pace_sec_500m);
    cJSON_AddNumberToObject(root, "strokeRate", g_metrics->stroke_rate_spm);
    cJSON_AddNumberToObject(root, "heartRate", hr_receiver_get_current());
    
    const char *phase = "idle";
    if (g_metrics->current_phase == STROKE_PHASE_DRIVE) {
        phase = "drive";
    } else if (g_metrics->current_phase == STROKE_PHASE_RECOVERY) {
        phase = "recovery";
    }
    cJSON_AddStringToObject(root, "phase", phase);
    
    cJSON_AddNumberToObject(root, "avgPower", g_metrics->average_power_watts);
    cJSON_AddNumberToObject(root, "avgPace", g_metrics->average_pace_sec_500m);
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (json_string == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json_string);
    
    free(json_string);
    return ESP_OK;
}

// ============================================================================
// WebSocket Handler
// ============================================================================

/**
 * Add WebSocket client to list (thread-safe)
 */
static void ws_add_client(int fd) {
    WS_MUTEX_TAKE();
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (g_ws_fds[i] < 0) {
            g_ws_fds[i] = fd;
            ESP_LOGI(TAG, "WebSocket client added: fd=%d", fd);
            WS_MUTEX_GIVE();
            return;
        }
    }
    WS_MUTEX_GIVE();
    ESP_LOGW(TAG, "WebSocket client list full");
}

/**
 * Remove WebSocket client from list (thread-safe)
 */
static void ws_remove_client(int fd) {
    WS_MUTEX_TAKE();
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (g_ws_fds[i] == fd) {
            g_ws_fds[i] = -1;
            ESP_LOGI(TAG, "WebSocket client removed: fd=%d", fd);
            WS_MUTEX_GIVE();
            return;
        }
    }
    WS_MUTEX_GIVE();
}

/**
 * WebSocket handler
 */
static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        // WebSocket handshake - add client to tracking list
        int sock = httpd_req_to_sockfd(req);
        if (sock >= 0) {
            ws_add_client(sock);
        }
        ESP_LOGI(TAG, "WebSocket handshake completed for fd=%d", sock);
        return ESP_OK;
    }
    
    // Handle WebSocket frame
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    
    // First get the frame length
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %d", ret);
        return ret;
    }
    
    if (ws_pkt.len > 0) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate WS buffer");
            return ESP_ERR_NO_MEM;
        }
        
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed: %d", ret);
            free(buf);
            return ret;
        }
    }
    
    // Handle different frame types
    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
        ESP_LOGI(TAG, "Received WS text: %s", (char*)ws_pkt.payload);
        
        // Handle commands from client
        if (strstr((char*)ws_pkt.payload, "reset") != NULL) {
            if (g_metrics != NULL) {
                metrics_calculator_reset(g_metrics);
            }
        }
    } else if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        int sock = httpd_req_to_sockfd(req);
        ws_remove_client(sock);
    }
    
    if (buf != NULL) {
        free(buf);
    }
    
    return ESP_OK;
}

// ============================================================================
// URI Definitions
// ============================================================================

static const httpd_uri_t uri_index = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_style = {
    .uri = "/style.css",
    .method = HTTP_GET,
    .handler = style_css_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_app_js = {
    .uri = "/app.js",
    .method = HTTP_GET,
    .handler = app_js_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_favicon = {
    .uri = "/favicon.ico",
    .method = HTTP_GET,
    .handler = favicon_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_metrics = {
    .uri = "/api/metrics",
    .method = HTTP_GET,
    .handler = api_metrics_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_status = {
    .uri = "/api/status",
    .method = HTTP_GET,
    .handler = api_status_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_reset = {
    .uri = "/api/reset",
    .method = HTTP_POST,
    .handler = api_reset_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_config_get = {
    .uri = "/api/config",
    .method = HTTP_GET,
    .handler = api_config_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_config_post = {
    .uri = "/api/config",
    .method = HTTP_POST,
    .handler = api_config_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_ws = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .user_ctx = NULL,
    .is_websocket = true,
    .handle_ws_control_frames = true
};

// Heart Rate endpoints
static const httpd_uri_t uri_hr_post = {
    .uri = "/hr",
    .method = HTTP_POST,
    .handler = hr_post_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_hr_get = {
    .uri = "/hr",
    .method = HTTP_GET,
    .handler = hr_get_handler,
    .user_ctx = NULL
};

// Session endpoints
static const httpd_uri_t uri_api_sessions = {
    .uri = "/api/sessions",
    .method = HTTP_GET,
    .handler = api_sessions_list_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_session_detail = {
    .uri = "/api/sessions/*",
    .method = HTTP_GET,
    .handler = api_session_detail_handler,
    .user_ctx = NULL
};

// Workout control endpoints
static const httpd_uri_t uri_workout_start = {
    .uri = "/workout/start",
    .method = HTTP_POST,
    .handler = workout_start_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_workout_stop = {
    .uri = "/workout/stop",
    .method = HTTP_POST,
    .handler = workout_stop_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_live_data = {
    .uri = "/live",
    .method = HTTP_GET,
    .handler = live_data_handler,
    .user_ctx = NULL
};

// ============================================================================
// Open/Close Callbacks for connection tracking
// ============================================================================

/**
 * Called for ALL new HTTP connections (not just WebSocket)
 * Do NOT add to WebSocket client list here - that's done in ws_handler
 */
static esp_err_t ws_open_callback(httpd_handle_t hd, int sockfd) {
    ESP_LOGD(TAG, "New HTTP connection on fd %d", sockfd);
    return ESP_OK;
}

/**
 * Called when any connection closes
 * Clean up WebSocket client if it was one
 */
static void ws_close_callback(httpd_handle_t hd, int sockfd) {
    ESP_LOGD(TAG, "Connection closed on fd %d", sockfd);
    ws_remove_client(sockfd);  // Safe to call even if not a WS client
}

// ============================================================================
// Public API
// ============================================================================

/**
 * Start HTTP server
 */
esp_err_t web_server_start(rowing_metrics_t *metrics, config_t *config) {
    if (g_server != NULL) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }
    
    // Create mutex for WebSocket client list
    if (g_ws_mutex == NULL) {
        g_ws_mutex = xSemaphoreCreateMutex();
        if (g_ws_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create WebSocket mutex");
            return ESP_FAIL;
        }
    }
    
    g_metrics = metrics;
    g_config = config;
    
    // Reset WebSocket client list
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        g_ws_fds[i] = -1;
    }
    
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.server_port = WEB_SERVER_PORT;
    http_config.max_open_sockets = 7;
    http_config.lru_purge_enable = true;
    http_config.uri_match_fn = httpd_uri_match_wildcard;
    http_config.open_fn = ws_open_callback;
    http_config.close_fn = ws_close_callback;
    http_config.recv_wait_timeout = 10;  // 10 second timeout for receive
    http_config.send_wait_timeout = 10;  // 10 second timeout for send
    
    ESP_LOGI(TAG, "Starting web server on port %d", http_config.server_port);
    
    esp_err_t ret = httpd_start(&g_server, &http_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register URI handlers
    httpd_register_uri_handler(g_server, &uri_index);
    httpd_register_uri_handler(g_server, &uri_style);
    httpd_register_uri_handler(g_server, &uri_app_js);
    httpd_register_uri_handler(g_server, &uri_favicon);
    httpd_register_uri_handler(g_server, &uri_api_metrics);
    httpd_register_uri_handler(g_server, &uri_api_status);
    httpd_register_uri_handler(g_server, &uri_api_reset);
    httpd_register_uri_handler(g_server, &uri_api_config_get);
    httpd_register_uri_handler(g_server, &uri_api_config_post);
    httpd_register_uri_handler(g_server, &uri_ws);
    
    // Heart rate endpoints (HeartRateToWeb compatible)
    httpd_register_uri_handler(g_server, &uri_hr_post);
    httpd_register_uri_handler(g_server, &uri_hr_get);
    
    // Session management endpoints
    httpd_register_uri_handler(g_server, &uri_api_sessions);
    httpd_register_uri_handler(g_server, &uri_api_session_detail);
    
    // Workout control endpoints
    httpd_register_uri_handler(g_server, &uri_workout_start);
    httpd_register_uri_handler(g_server, &uri_workout_stop);
    httpd_register_uri_handler(g_server, &uri_live_data);
    
    ESP_LOGI(TAG, "Web server started successfully");
    return ESP_OK;
}

/**
 * Stop HTTP server
 */
void web_server_stop(void) {
    if (g_server != NULL) {
        httpd_stop(g_server);
        g_server = NULL;
        ESP_LOGI(TAG, "Web server stopped");
    }
    
    // Clean up WebSocket client list
    WS_MUTEX_TAKE();
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        g_ws_fds[i] = -1;
    }
    WS_MUTEX_GIVE();
    
    // Delete mutex
    if (g_ws_mutex != NULL) {
        vSemaphoreDelete(g_ws_mutex);
        g_ws_mutex = NULL;
    }
}

/**
 * Check if a socket is still valid and connected
 */
static bool is_socket_valid(httpd_handle_t hd, int fd) {
    if (hd == NULL || fd < 0) {
        return false;
    }
    
    // Try to get socket info - if it fails, socket is invalid
    struct sockaddr_in6 addr;
    socklen_t addr_len = sizeof(addr);
    
    // Use getpeername to check if socket is still connected
    if (getpeername(fd, (struct sockaddr *)&addr, &addr_len) < 0) {
        return false;
    }
    
    return true;
}

/**
 * Broadcast metrics to all connected WebSocket clients
 * Thread-safe with proper error handling
 */
esp_err_t web_server_broadcast_metrics(const rowing_metrics_t *metrics) {
    if (g_server == NULL || metrics == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Build JSON
    char buffer[JSON_BUFFER_SIZE];
    int len = metrics_calculator_to_json(metrics, buffer, sizeof(buffer));
    if (len <= 0) {
        return ESP_FAIL;
    }
    
    // Prepare WebSocket frame
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)buffer;
    ws_pkt.len = len;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.final = true;
    
    // Take a snapshot of current clients under mutex
    int fds_to_send[MAX_WS_CLIENTS];
    WS_MUTEX_TAKE();
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        fds_to_send[i] = g_ws_fds[i];
    }
    WS_MUTEX_GIVE();
    
    // Send to all clients (outside mutex to avoid blocking)
    int sent_count = 0;
    int dead_fds[MAX_WS_CLIENTS];
    int dead_count = 0;
    
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        int fd = fds_to_send[i];
        if (fd >= 0) {
            // First check if socket is still valid
            if (!is_socket_valid(g_server, fd)) {
                ESP_LOGD(TAG, "Socket fd %d is no longer valid", fd);
                dead_fds[dead_count++] = fd;
                continue;
            }
            
            // Try async send
            esp_err_t ret = httpd_ws_send_frame_async(g_server, fd, &ws_pkt);
            if (ret == ESP_OK) {
                sent_count++;
            } else if (ret == ESP_ERR_INVALID_ARG) {
                // Socket closed or invalid
                ESP_LOGD(TAG, "Socket fd %d invalid for async send", fd);
                dead_fds[dead_count++] = fd;
            } else {
                // Other error - log but don't remove yet (might be temporary)
                ESP_LOGD(TAG, "Failed to send to fd %d: %s (will retry)", fd, esp_err_to_name(ret));
            }
        }
    }
    
    // Remove dead clients under mutex
    if (dead_count > 0) {
        WS_MUTEX_TAKE();
        for (int d = 0; d < dead_count; d++) {
            for (int i = 0; i < MAX_WS_CLIENTS; i++) {
                if (g_ws_fds[i] == dead_fds[d]) {
                    g_ws_fds[i] = -1;
                    ESP_LOGI(TAG, "Removed dead WebSocket client: fd=%d", dead_fds[d]);
                    break;
                }
            }
        }
        WS_MUTEX_GIVE();
    }
    
    return (sent_count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

/**
 * Check if any WebSocket clients are connected (thread-safe)
 */
bool web_server_has_ws_clients(void) {
    bool has_clients = false;
    WS_MUTEX_TAKE();
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (g_ws_fds[i] >= 0) {
            has_clients = true;
            break;
        }
    }
    WS_MUTEX_GIVE();
    return has_clients;
}

/**
 * Get number of active connections (thread-safe)
 */
int web_server_get_connection_count(void) {
    int count = 0;
    WS_MUTEX_TAKE();
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (g_ws_fds[i] >= 0) {
            count++;
        }
    }
    WS_MUTEX_GIVE();
    return count;
}
