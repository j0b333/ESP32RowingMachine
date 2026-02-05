/**
 * @file web_server.c
 * @brief HTTP server with SSE and WebSocket for real-time metrics streaming
 * 
 * Features:
 * - Serves embedded HTML/CSS/JS files
 * - Server-Sent Events (SSE) for real-time metrics streaming (preferred)
 * - WebSocket as fallback for real-time metrics
 * - REST API for configuration
 * - Session control endpoints
 * 
 * Compatible with ESP-IDF 6.0+
 * Thread-safe client management with mutex
 */

#include "web_server.h"
#include "app_config.h"
#include "metrics_calculator.h"
#include "config_manager.h"
#include "hr_receiver.h"
#include "session_manager.h"
#include "wifi_manager.h"

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
#include <netinet/tcp.h>
#include <errno.h>

static const char *TAG = "WEB_SERVER";

// HTTP server handle
static httpd_handle_t g_server = NULL;

// Maximum number of streaming clients (shared for both WebSocket and SSE)
#define MAX_STREAMING_CLIENTS 8

// WebSocket file descriptors for connected clients
#define MAX_WS_CLIENTS MAX_STREAMING_CLIENTS
static int g_ws_fds[MAX_WS_CLIENTS] = {-1, -1, -1, -1, -1, -1, -1, -1};

// Mutex for thread-safe WebSocket client list access
static SemaphoreHandle_t g_ws_mutex = NULL;

// Pointers to shared data
static rowing_metrics_t *g_metrics = NULL;
static config_t *g_config = NULL;

// Inertia calibration state
static inertia_calibration_t g_inertia_calibration = {0};

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

// Helper to set Connection: close header to free sockets faster
#define HTTPD_RESP_SET_CLOSE(req) httpd_resp_set_hdr(req, "Connection", "close")

// ============================================================================
// Embedded Web Content Declarations
// ============================================================================

// These symbols are created by the linker from embedded files
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

extern const char setup_html_start[] asm("_binary_setup_html_start");
extern const char setup_html_end[]   asm("_binary_setup_html_end");

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
 * Serve main HTML page (rowing monitor) or redirect to setup in AP mode
 * 
 * In AP mode (WiFi setup):
 * - First-time visitors are redirected to /setup for captive portal
 * - Users who click "Skip" from setup page (have Referer: /setup) get the monitor
 * - Users with ?skip=1 query parameter get the monitor
 */
static esp_err_t index_handler(httpd_req_t *req) {
    // In AP mode, check if user explicitly wants to skip setup
    if (wifi_manager_get_mode() == WIFI_OPERATING_MODE_AP) {
        bool skip_setup = false;
        
        // Check for ?skip=1 query parameter
        size_t buf_len = httpd_req_get_url_query_len(req) + 1;
        if (buf_len > 1) {
            char *buf = malloc(buf_len);
            if (buf && httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
                char param[8];
                if (httpd_query_key_value(buf, "skip", param, sizeof(param)) == ESP_OK) {
                    if (strcmp(param, "1") == 0) {
                        skip_setup = true;
                    }
                }
            }
            free(buf);
        }
        
        // Check for Referer from setup page (user clicked "Skip" link)
        if (!skip_setup) {
            char referer[128] = {0};
            if (httpd_req_get_hdr_value_str(req, "Referer", referer, sizeof(referer)) == ESP_OK) {
                if (strstr(referer, "/setup") != NULL) {
                    skip_setup = true;
                }
            }
        }
        
        if (!skip_setup) {
            // First-time visitor - redirect to setup for captive portal
            ESP_LOGI(TAG, "AP mode: redirecting / to /setup");
            
            // Return a simple HTML page that auto-redirects to setup
            // This triggers captive portal detection on phones
            static const char captive_response[] = 
                "<!DOCTYPE html>"
                "<html><head>"
                "<meta http-equiv=\"refresh\" content=\"0; url=/setup\">"
                "<title>WiFi Setup</title>"
                "</head><body>"
                "<h1>Redirecting to WiFi Setup...</h1>"
                "<p><a href=\"/setup\">Click here if not redirected</a></p>"
                "</body></html>";
            
            httpd_resp_set_type(req, "text/html");
            httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
            httpd_resp_set_hdr(req, "Pragma", "no-cache");
            httpd_resp_set_hdr(req, "Expires", "0");
            HTTPD_RESP_SET_CLOSE(req);
            httpd_resp_send(req, captive_response, sizeof(captive_response) - 1);
            
            return ESP_OK;
        }
        
        // User explicitly skipped setup - show the rowing monitor
        ESP_LOGI(TAG, "AP mode: user skipped setup, showing monitor");
    }
    
    // Serve the rowing monitor
    const size_t index_html_size = (index_html_end - index_html_start);
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    HTTPD_RESP_SET_CLOSE(req);
    httpd_resp_send(req, index_html_start, index_html_size);
    
    return ESP_OK;
}

/**
 * Serve setup/provisioning page
 */
static esp_err_t setup_handler(httpd_req_t *req) {
    const size_t setup_html_size = (setup_html_end - setup_html_start);
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    HTTPD_RESP_SET_CLOSE(req);
    httpd_resp_send(req, setup_html_start, setup_html_size);
    
    return ESP_OK;
}

/**
 * Captive portal redirect handler
 * Handles common captive portal detection URLs from various OS/browsers
 * 
 * Note: Some devices expect specific responses to trigger captive portal:
 * - Android: Expects 204 for "online", anything else triggers portal
 * - iOS: Expects specific "Success" text for "online", anything else triggers portal
 * 
 * We return a simple HTML page that redirects to /setup
 */
static esp_err_t captive_portal_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Captive portal detection: %s", req->uri);
    
    // Return a simple HTML page that auto-redirects to setup
    // This works better than 302 for captive portal detection on some devices
    static const char captive_response[] = 
        "<!DOCTYPE html>"
        "<html><head>"
        "<meta http-equiv=\"refresh\" content=\"0; url=/setup\">"
        "<title>WiFi Setup</title>"
        "</head><body>"
        "<h1>Redirecting to WiFi Setup...</h1>"
        "<p><a href=\"/setup\">Click here if not redirected</a></p>"
        "</body></html>";
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    HTTPD_RESP_SET_CLOSE(req);
    httpd_resp_send(req, captive_response, sizeof(captive_response) - 1);
    
    return ESP_OK;
}

/**
 * Serve CSS file
 */
static esp_err_t style_css_handler(httpd_req_t *req) {
    const size_t style_css_size = (style_css_end - style_css_start);
    
    httpd_resp_set_type(req, "text/css");
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
    HTTPD_RESP_SET_CLOSE(req);
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
    HTTPD_RESP_SET_CLOSE(req);
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
    HTTPD_RESP_SET_CLOSE(req);
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

// ============================================================================
// Inertia Calibration Endpoints
// ============================================================================

/**
 * API endpoint: Start inertia calibration
 * POST /api/calibrate/inertia/start
 */
static esp_err_t api_calibrate_inertia_start_handler(httpd_req_t *req) {
    if (g_metrics == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    // Use default drag coefficient if not yet calibrated
    // This allows inertia calibration before rowing
    if (g_metrics->drag_calibration_samples < 10) {
        g_metrics->drag_coefficient = g_config->initial_drag_coefficient;
        ESP_LOGI(TAG, "Using default drag coefficient %.6f for inertia calibration", 
                 g_metrics->drag_coefficient);
    }
    
    rowing_physics_start_inertia_calibration(&g_inertia_calibration, g_metrics);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", g_inertia_calibration.status_message);
    cJSON_AddStringToObject(root, "state", "waiting");
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);
    free(json_string);
    
    ESP_LOGI(TAG, "Inertia calibration started via API");
    return ESP_OK;
}

/**
 * API endpoint: Get inertia calibration status
 * GET /api/calibrate/inertia/status
 */
static esp_err_t api_calibrate_inertia_status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    
    const char *state_str;
    switch (g_inertia_calibration.state) {
        case CALIBRATION_WAITING:   state_str = "waiting"; break;
        case CALIBRATION_SPINUP:    state_str = "spinup"; break;
        case CALIBRATION_SPINDOWN:  state_str = "spindown"; break;
        case CALIBRATION_COMPLETE:  state_str = "complete"; break;
        case CALIBRATION_FAILED:    state_str = "failed"; break;
        default:                    state_str = "idle"; break;
    }
    
    cJSON_AddStringToObject(root, "state", state_str);
    cJSON_AddStringToObject(root, "message", g_inertia_calibration.status_message);
    cJSON_AddNumberToObject(root, "peakVelocity", g_inertia_calibration.peak_velocity_rad_s);
    cJSON_AddNumberToObject(root, "sampleCount", g_inertia_calibration.sample_count);
    
    if (g_inertia_calibration.state == CALIBRATION_COMPLETE) {
        cJSON_AddNumberToObject(root, "calculatedInertia", g_inertia_calibration.calculated_inertia);
    }
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);
    free(json_string);
    
    return ESP_OK;
}

/**
 * API endpoint: Cancel inertia calibration
 * POST /api/calibrate/inertia/cancel
 */
static esp_err_t api_calibrate_inertia_cancel_handler(httpd_req_t *req) {
    rowing_physics_cancel_inertia_calibration(&g_inertia_calibration);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "Calibration cancelled");
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);
    free(json_string);
    
    ESP_LOGI(TAG, "Inertia calibration cancelled via API");
    return ESP_OK;
}

/**
 * API endpoint: Apply calibrated inertia value
 * POST /api/calibrate/inertia/apply
 */
static esp_err_t api_calibrate_inertia_apply_handler(httpd_req_t *req) {
    if (g_inertia_calibration.state != CALIBRATION_COMPLETE) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "success", false);
        cJSON_AddStringToObject(root, "error", "No calibration result to apply");
        
        char *json_string = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, json_string);
        free(json_string);
        return ESP_OK;
    }
    
    // Apply the calibrated value
    float new_inertia = g_inertia_calibration.calculated_inertia;
    g_config->moment_of_inertia = new_inertia;
    g_metrics->moment_of_inertia = new_inertia;
    
    // Save to NVS
    config_manager_save(g_config);
    
    // Reset calibration state
    g_inertia_calibration.state = CALIBRATION_IDLE;
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddNumberToObject(root, "momentOfInertia", new_inertia);
    cJSON_AddStringToObject(root, "message", "Calibrated inertia value saved");
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_string);
    free(json_string);
    
    ESP_LOGI(TAG, "Calibrated inertia %.4f applied and saved", new_inertia);
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
        cJSON_AddNumberToObject(root, "autoPauseSeconds", g_config->auto_pause_seconds);
        cJSON_AddNumberToObject(root, "maxHeartRate", g_config->max_heart_rate);
        
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
    if ((item = cJSON_GetObjectItem(root, "momentOfInertia")) != NULL) {
        float val = (float)cJSON_GetNumberValue(item);
        g_config->moment_of_inertia = (val >= 0.01f && val <= 1.0f) ? val : 0.101f;
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
    if ((item = cJSON_GetObjectItem(root, "autoPauseSeconds")) != NULL) {
        int val = (int)cJSON_GetNumberValue(item);
        g_config->auto_pause_seconds = (val >= 0 && val <= 60) ? (uint8_t)val : 5;
    }
    if ((item = cJSON_GetObjectItem(root, "maxHeartRate")) != NULL) {
        int val = (int)cJSON_GetNumberValue(item);
        g_config->max_heart_rate = (val >= 100 && val <= 220) ? (uint8_t)val : 190;
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
            cJSON_AddNumberToObject(session, "avgHeartRate", record.average_heart_rate);
            cJSON_AddNumberToObject(session, "maxHeartRate", record.max_heart_rate);
            cJSON_AddBoolToObject(session, "synced", record.synced);
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
 * Returns data in Health Connect compatible format:
 * - heartRateSamples: [{time, bpm}]
 * - powerSamples: [{time, watts}]
 * - speedSamples: [{time, metersPerSecond}]
 * Also provides legacy format for internal UI: paceSamples[], hrSamples[], powerSamplesArray[]
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
    cJSON_AddNumberToObject(root, "avgHeartRate", record.average_heart_rate);
    cJSON_AddNumberToObject(root, "maxHeartRate", record.max_heart_rate);
    cJSON_AddBoolToObject(root, "synced", record.synced);
    
    // Sample arrays for companion app (Health Connect format with time/value objects)
    cJSON *heartRateSamples = cJSON_CreateArray();
    cJSON *powerSamples = cJSON_CreateArray();
    cJSON *speedSamples = cJSON_CreateArray();
    
    // Load per-second sample data if available
    if (record.sample_count > 0) {
        // Allocate buffer for samples (limit to avoid memory issues)
        uint32_t max_samples = record.sample_count;
        if (max_samples > 3600) max_samples = 3600;  // Limit to 1 hour for JSON response
        
        sample_data_t *samples = malloc(max_samples * sizeof(sample_data_t));
        if (samples != NULL) {
            uint32_t actual_count = 0;
            if (session_manager_get_samples(session_id, samples, max_samples, &actual_count) == ESP_OK && actual_count > 0) {
                // start_timestamp is now Unix epoch milliseconds (when SNTP is synced)
                // or milliseconds since boot (fallback when SNTP not available)
                // The companion app receives this directly as the base time
                int64_t base_time_ms = record.start_timestamp;  // Already in milliseconds
                
                for (uint32_t i = 0; i < actual_count; i++) {
                    int64_t sample_time_ms = base_time_ms + (i * 1000);  // 1 second per sample
                    
                    // Convert velocity from cm/s to m/s
                    float velocity_m_s = samples[i].velocity_cm_s / 100.0f;
                    
                    // Heart rate samples (Health Connect format)
                    if (samples[i].heart_rate > 0) {
                        cJSON *hrSample = cJSON_CreateObject();
                        cJSON_AddNumberToObject(hrSample, "time", (double)sample_time_ms);
                        cJSON_AddNumberToObject(hrSample, "bpm", samples[i].heart_rate);
                        cJSON_AddItemToArray(heartRateSamples, hrSample);
                    }
                    
                    // Power samples (Health Connect format)
                    {
                        cJSON *pwrSample = cJSON_CreateObject();
                        cJSON_AddNumberToObject(pwrSample, "time", (double)sample_time_ms);
                        cJSON_AddNumberToObject(pwrSample, "watts", samples[i].power_watts);
                        cJSON_AddItemToArray(powerSamples, pwrSample);
                    }
                    
                    // Speed samples (Health Connect format)
                    {
                        cJSON *spdSample = cJSON_CreateObject();
                        cJSON_AddNumberToObject(spdSample, "time", (double)sample_time_ms);
                        cJSON_AddNumberToObject(spdSample, "metersPerSecond", velocity_m_s);
                        cJSON_AddItemToArray(speedSamples, spdSample);
                    }
                }
            }
            free(samples);
        }
    }
    
    // Add sample arrays (Health Connect format)
    cJSON_AddItemToObject(root, "heartRateSamples", heartRateSamples);
    cJSON_AddItemToObject(root, "powerSamples", powerSamples);
    cJSON_AddItemToObject(root, "speedSamples", speedSamples);
    
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
 * DELETE /api/sessions/{id} - Delete a specific session
 */
static esp_err_t api_session_delete_handler(httpd_req_t *req) {
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
    
    esp_err_t result = session_manager_delete_session(session_id);
    if (result != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Session not found");
        return ESP_FAIL;
    }
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddNumberToObject(root, "deletedId", session_id);
    
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
    
    ESP_LOGI(TAG, "Session #%lu deleted via API", (unsigned long)session_id);
    return ESP_OK;
}

/**
 * POST /api/sessions/{id}/synced - Mark a session as synced
 */
static esp_err_t api_session_synced_handler(httpd_req_t *req) {
    // Parse session ID from URI: /api/sessions/123/synced
    const char *uri = req->uri;
    
    // Find the session ID - it's between the last two '/' characters
    const char *synced_pos = strstr(uri, "/synced");
    if (synced_pos == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid endpoint");
        return ESP_FAIL;
    }
    
    // Work backwards to find the session ID
    const char *id_end = synced_pos;
    const char *id_start = id_end - 1;
    while (id_start > uri && *id_start != '/') {
        id_start--;
    }
    id_start++;  // Move past the '/'
    
    if (id_start >= id_end) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid session ID");
        return ESP_FAIL;
    }
    
    // Extract session ID
    char id_str[16];
    size_t id_len = id_end - id_start;
    if (id_len >= sizeof(id_str)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Session ID too long");
        return ESP_FAIL;
    }
    memcpy(id_str, id_start, id_len);
    id_str[id_len] = '\0';
    
    long session_id_long = strtol(id_str, NULL, 10);
    if (session_id_long <= 0 || session_id_long > UINT32_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid session ID");
        return ESP_FAIL;
    }
    uint32_t session_id = (uint32_t)session_id_long;
    
    esp_err_t result = session_manager_set_synced(session_id);
    if (result != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Session not found");
        return ESP_FAIL;
    }
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddNumberToObject(root, "sessionId", session_id);
    
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
    
    ESP_LOGI(TAG, "Session #%lu marked as synced via API", (unsigned long)session_id);
    return ESP_OK;
}

/**
 * DELETE /api/sessions/synced - Delete all synced sessions
 */
static esp_err_t api_sessions_delete_synced_handler(httpd_req_t *req) {
    esp_err_t result = session_manager_delete_synced();
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", result == ESP_OK);
    
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
    
    ESP_LOGI(TAG, "Synced sessions deleted via API");
    return ESP_OK;
}

// ============================================================================
// Workout Control Endpoints
// ============================================================================

/**
 * POST /workout/start - Start a new workout or resume if paused
 */
static esp_err_t workout_start_handler(httpd_req_t *req) {
    if (g_metrics == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    uint32_t session_id = session_manager_get_current_session_id();
    
    // Check if we have an existing session that's paused - resume it instead of starting new
    if (session_id > 0 && g_metrics->is_paused) {
        // Resume existing paused session
        int64_t now = esp_timer_get_time();
        
        // Calculate pause duration and add to total
        if (g_metrics->pause_start_time_us > 0) {
            int64_t paused_duration_us = now - g_metrics->pause_start_time_us;
            if (paused_duration_us > 0) {
                g_metrics->total_paused_time_ms += (uint32_t)(paused_duration_us / 1000);
            }
        }
        
        // If session_start_time was 0 (reset state), set it now
        if (g_metrics->session_start_time_us == 0) {
            g_metrics->session_start_time_us = now;
        }
        
        g_metrics->is_paused = false;
        g_metrics->pause_start_time_us = 0;
        g_metrics->last_resume_time_us = now;  // Track when we resumed for auto-pause logic
        
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "status", "resumed");
        cJSON_AddNumberToObject(root, "sessionId", session_id);
        
        char *json_string = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        httpd_resp_sendstr(req, json_string);
        
        free(json_string);
        
        ESP_LOGI(TAG, "Workout resumed via API, session #%lu", (unsigned long)session_id);
        return ESP_OK;
    }
    
    // Start HR recording
    hr_receiver_start_recording();
    
    // Reset metrics for new workout
    metrics_calculator_reset(g_metrics);
    
    // Start a new session
    session_manager_start_session(g_metrics);
    
    session_id = session_manager_get_current_session_id();
    
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
    
    // Clear any paused state before stopping
    if (g_metrics->is_paused) {
        int64_t now = esp_timer_get_time();
        int64_t paused_duration_us = now - g_metrics->pause_start_time_us;
        if (paused_duration_us > 0) {
            g_metrics->total_paused_time_ms += (uint32_t)(paused_duration_us / 1000);
        }
        g_metrics->is_paused = false;
        g_metrics->pause_start_time_us = 0;
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
 * POST /workout/pause - Pause current workout
 */
static esp_err_t workout_pause_handler(httpd_req_t *req) {
    if (g_metrics == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    cJSON *root = cJSON_CreateObject();
    
    // Set paused state and record when pause started
    if (!g_metrics->is_paused) {
        g_metrics->is_paused = true;
        g_metrics->pause_start_time_us = esp_timer_get_time();
        cJSON_AddStringToObject(root, "status", "paused");
        cJSON_AddBoolToObject(root, "success", true);
        ESP_LOGI(TAG, "Workout paused via API");
    } else {
        cJSON_AddStringToObject(root, "status", "already_paused");
        cJSON_AddBoolToObject(root, "success", false);
    }
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json_string);
    
    free(json_string);
    return ESP_OK;
}

/**
 * POST /workout/resume - Resume paused workout
 */
static esp_err_t workout_resume_handler(httpd_req_t *req) {
    if (g_metrics == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    cJSON *root = cJSON_CreateObject();
    
    // Resume from paused state
    if (g_metrics->is_paused) {
        int64_t now = esp_timer_get_time();
        int64_t paused_duration_us = now - g_metrics->pause_start_time_us;
        // Prevent negative/overflow issues
        if (paused_duration_us > 0) {
            g_metrics->total_paused_time_ms += (uint32_t)(paused_duration_us / 1000);
        }
        g_metrics->is_paused = false;
        g_metrics->pause_start_time_us = 0;
        g_metrics->last_resume_time_us = now;  // Track when we resumed for auto-pause logic
        cJSON_AddStringToObject(root, "status", "resumed");
        cJSON_AddBoolToObject(root, "success", true);
        ESP_LOGI(TAG, "Workout resumed via API (was paused for %lu ms)", 
                 (unsigned long)(paused_duration_us / 1000));
    } else {
        cJSON_AddStringToObject(root, "status", "not_paused");
        cJSON_AddBoolToObject(root, "success", false);
    }
    
    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json_string);
    
    free(json_string);
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
// WiFi Captive Portal Endpoints
// ============================================================================

/**
 * GET /api/wifi/scan - Scan for available WiFi networks
 */
#define MAX_WIFI_SCAN_RESULTS 20

static esp_err_t api_wifi_scan_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "WiFi scan requested");
    
    // Allocate buffer for scan results
    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * MAX_WIFI_SCAN_RESULTS);
    if (ap_records == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    int count = wifi_manager_scan(ap_records, MAX_WIFI_SCAN_RESULTS);
    
    cJSON *root = cJSON_CreateObject();
    cJSON *networks = cJSON_CreateArray();
    
    if (root == NULL || networks == NULL) {
        free(ap_records);
        if (root) cJSON_Delete(root);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    for (int i = 0; i < count; i++) {
        cJSON *net = cJSON_CreateObject();
        if (net == NULL) continue;  // Skip on allocation failure
        
        cJSON_AddStringToObject(net, "ssid", (char *)ap_records[i].ssid);
        cJSON_AddNumberToObject(net, "rssi", ap_records[i].rssi);
        cJSON_AddNumberToObject(net, "channel", ap_records[i].primary);
        
        const char *auth;
        switch (ap_records[i].authmode) {
            case WIFI_AUTH_OPEN: auth = "open"; break;
            case WIFI_AUTH_WEP: auth = "wep"; break;
            case WIFI_AUTH_WPA_PSK: auth = "wpa"; break;
            case WIFI_AUTH_WPA2_PSK: auth = "wpa2"; break;
            case WIFI_AUTH_WPA_WPA2_PSK: auth = "wpa/wpa2"; break;
            case WIFI_AUTH_WPA3_PSK: auth = "wpa3"; break;
            default: auth = "unknown"; break;
        }
        cJSON_AddStringToObject(net, "auth", auth);
        cJSON_AddBoolToObject(net, "secure", ap_records[i].authmode != WIFI_AUTH_OPEN);
        
        cJSON_AddItemToArray(networks, net);
    }
    
    free(ap_records);
    
    cJSON_AddItemToObject(root, "networks", networks);
    cJSON_AddNumberToObject(root, "count", count);
    
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
 * POST /api/wifi/connect - Save WiFi credentials and connect
 */
static esp_err_t api_wifi_connect_handler(httpd_req_t *req) {
    char content[512];  // Increased buffer for longer credentials
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
    
    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *password = cJSON_GetObjectItem(root, "password");
    
    if (ssid == NULL || !cJSON_IsString(ssid)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }
    
    const char *ssid_str = cJSON_GetStringValue(ssid);
    const char *pass_str = password ? cJSON_GetStringValue(password) : "";
    
    if (ssid_str == NULL || strlen(ssid_str) == 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID cannot be empty");
        return ESP_FAIL;
    }
    
    // Validate lengths
    if (strlen(ssid_str) > 31) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID too long (max 31 chars)");
        return ESP_FAIL;
    }
    if (pass_str && strlen(pass_str) > 63) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Password too long (max 63 chars)");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "WiFi connect request: SSID=%s", ssid_str);
    
    // Save to config with explicit null termination
    if (g_config != NULL) {
        strncpy(g_config->sta_ssid, ssid_str, sizeof(g_config->sta_ssid) - 1);
        g_config->sta_ssid[sizeof(g_config->sta_ssid) - 1] = '\0';
        strncpy(g_config->sta_password, pass_str ? pass_str : "", sizeof(g_config->sta_password) - 1);
        g_config->sta_password[sizeof(g_config->sta_password) - 1] = '\0';
        g_config->sta_configured = true;
        config_manager_save(g_config);
    }
    
    cJSON_Delete(root);
    
    // Send success response
    cJSON *response = cJSON_CreateObject();
    if (response == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    cJSON_AddBoolToObject(response, "success", true);
    cJSON_AddStringToObject(response, "message", "WiFi credentials saved. Device will reboot and connect to your network.");
    cJSON_AddStringToObject(response, "ssid", ssid_str);
    cJSON_AddStringToObject(response, "redirect_url", "http://rower.local");
    cJSON_AddNumberToObject(response, "redirect_delay", 5);
    
    char *json_string = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    
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
 * GET /api/wifi/status - Get current WiFi status
 */
static esp_err_t api_wifi_status_handler(httpd_req_t *req) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    
    wifi_operating_mode_t mode = wifi_manager_get_mode();
    cJSON_AddStringToObject(root, "mode", mode == WIFI_OPERATING_MODE_STA ? "sta" : "ap");
    cJSON_AddBoolToObject(root, "connected", wifi_manager_is_connected());
    
    if (g_config != NULL) {
        cJSON_AddBoolToObject(root, "staConfigured", g_config->sta_configured);
        if (g_config->sta_configured) {
            cJSON_AddStringToObject(root, "staSSID", g_config->sta_ssid);
        }
        cJSON_AddStringToObject(root, "apSSID", g_config->wifi_ssid);
    }
    
    // Get current IP
    char ip_str[16];
    wifi_manager_get_ip_string(ip_str, sizeof(ip_str));
    cJSON_AddStringToObject(root, "ip", ip_str);
    
    // Station count if in AP mode
    if (mode == WIFI_OPERATING_MODE_AP) {
        cJSON_AddNumberToObject(root, "stationCount", wifi_manager_get_station_count());
    }
    
    // Add diagnostic info for debugging connection issues
    cJSON *diag = cJSON_CreateObject();
    if (diag != NULL) {
        cJSON_AddStringToObject(diag, "authMode", "WPA2-PSK");
        cJSON_AddNumberToObject(diag, "channel", WIFI_AP_CHANNEL);
        cJSON_AddStringToObject(diag, "bandwidth", "HT20");
        cJSON_AddNumberToObject(diag, "maxConnections", WIFI_AP_MAX_CONNECTIONS);
        
        // Cached WiFi scan to check hardware health (avoid scanning on every request)
        static int64_t last_scan_time = 0;
        static int cached_scan_count = -2;  // -2 = never scanned
        const int64_t SCAN_CACHE_TTL_US = 60 * 1000000;  // 60 seconds
        
        int64_t now = esp_timer_get_time();
        if (cached_scan_count == -2 || (now - last_scan_time) > SCAN_CACHE_TTL_US) {
            // Perform scan (cached result expired)
            wifi_ap_record_t ap_record;
            cached_scan_count = wifi_manager_scan(&ap_record, 1);
            last_scan_time = now;
        }
        
        cJSON_AddBoolToObject(diag, "wifiHardwareOk", cached_scan_count >= 0);
        cJSON_AddNumberToObject(diag, "nearbyNetworks", cached_scan_count);
        
        // Hardware status hints
        if (cached_scan_count < 0) {
            cJSON_AddStringToObject(diag, "hardwareHint", "WiFi scan failed - possible hardware issue");
        } else if (cached_scan_count == 0) {
            cJSON_AddStringToObject(diag, "hardwareHint", "No networks found - check antenna or location");
        } else {
            cJSON_AddStringToObject(diag, "hardwareHint", "WiFi hardware appears functional");
        }
        
        cJSON_AddItemToObject(root, "diagnostics", diag);
    }
    
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
 * POST /api/wifi/disconnect - Clear STA credentials and revert to AP mode
 */
static esp_err_t api_wifi_disconnect_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "WiFi disconnect/forget request");
    
    if (g_config != NULL) {
        g_config->sta_ssid[0] = '\0';
        g_config->sta_password[0] = '\0';
        g_config->sta_configured = false;
        config_manager_save(g_config);
    }
    
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "WiFi credentials cleared. Reboot to use AP mode.");
    
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
 * POST /api/reboot - Reboot the device
 */
static esp_err_t api_reboot_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Reboot requested via API");
    
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "message", "Device will reboot in 2 seconds");
    
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
    
    // Schedule reboot after response is sent
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    
    return ESP_OK;  // Won't reach here
}

// ============================================================================
// Server-Sent Events (SSE) Support
// ============================================================================

// SSE client structure to store both fd and async request handle
typedef struct {
    int fd;
    httpd_req_t *async_req;
} sse_client_t;

// SSE client list
#define MAX_SSE_CLIENTS MAX_STREAMING_CLIENTS
static sse_client_t g_sse_clients[MAX_SSE_CLIENTS];
static SemaphoreHandle_t g_sse_mutex = NULL;

// SSE mutex macros
#define SSE_MUTEX_TAKE() \
    do { \
        if (g_sse_mutex != NULL) { \
            xSemaphoreTake(g_sse_mutex, portMAX_DELAY); \
        } \
    } while(0)

#define SSE_MUTEX_GIVE() \
    do { \
        if (g_sse_mutex != NULL) { \
            xSemaphoreGive(g_sse_mutex); \
        } \
    } while(0)

/**
 * Initialize SSE client list
 */
static void sse_init_clients(void) {
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        g_sse_clients[i].fd = -1;
        g_sse_clients[i].async_req = NULL;
    }
}

/**
 * Add SSE client to list (thread-safe)
 * Stores both the fd and async request handle to keep connection alive
 */
static bool sse_add_client(int fd, httpd_req_t *async_req) {
    SSE_MUTEX_TAKE();
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (g_sse_clients[i].fd < 0) {
            g_sse_clients[i].fd = fd;
            g_sse_clients[i].async_req = async_req;
            ESP_LOGI(TAG, "SSE client added: fd=%d, slot=%d", fd, i);
            SSE_MUTEX_GIVE();
            return true;
        }
    }
    SSE_MUTEX_GIVE();
    ESP_LOGW(TAG, "SSE client list full");
    return false;
}

/**
 * Remove SSE client from list and complete async request (thread-safe)
 */
static void sse_remove_client(int fd) {
    httpd_req_t *async_req = NULL;
    
    SSE_MUTEX_TAKE();
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (g_sse_clients[i].fd == fd) {
            async_req = g_sse_clients[i].async_req;
            g_sse_clients[i].fd = -1;
            g_sse_clients[i].async_req = NULL;
            ESP_LOGI(TAG, "SSE client removed: fd=%d", fd);
            break;
        }
    }
    SSE_MUTEX_GIVE();
    
    // Complete the async request outside of mutex to avoid deadlock
    if (async_req != NULL) {
        httpd_req_async_handler_complete(async_req);
    }
}

/**
 * SSE events endpoint handler
 * Uses raw socket writes for proper SSE streaming
 */
static esp_err_t sse_handler(httpd_req_t *req) {
    int fd = httpd_req_to_sockfd(req);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to get socket fd for SSE");
        return ESP_FAIL;
    }
    
    // Create async copy of the request to keep connection alive
    httpd_req_t *async_req = NULL;
    esp_err_t ret = httpd_req_async_handler_begin(req, &async_req);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start async SSE handler for fd=%d: %s", fd, esp_err_to_name(ret));
        return ret;
    }
    
    // Set socket options to keep the connection alive
    int keep_alive = 1;
    int keep_idle = 60;     // Start keepalive after 60 seconds of idle
    int keep_interval = 10; // Send keepalive every 10 seconds
    int keep_count = 5;     // Close after 5 failed keepalives
    
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive)) < 0) {
        ESP_LOGW(TAG, "Failed to set SO_KEEPALIVE for SSE fd=%d: errno %d", fd, errno);
    }
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(keep_idle)) < 0) {
        ESP_LOGW(TAG, "Failed to set TCP_KEEPIDLE for SSE fd=%d: errno %d", fd, errno);
    }
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keep_interval, sizeof(keep_interval)) < 0) {
        ESP_LOGW(TAG, "Failed to set TCP_KEEPINTVL for SSE fd=%d: errno %d", fd, errno);
    }
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keep_count, sizeof(keep_count)) < 0) {
        ESP_LOGW(TAG, "Failed to set TCP_KEEPCNT for SSE fd=%d: errno %d", fd, errno);
    }
    
    // Disable Nagle's algorithm for lower latency SSE
    int nodelay = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
        ESP_LOGW(TAG, "Failed to set TCP_NODELAY for SSE fd=%d: errno %d", fd, errno);
    }
    
    // Send raw HTTP response headers for SSE
    // This bypasses httpd's response handling which would close the connection
    const char *http_headers = 
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";
    
    int written = send(fd, http_headers, strlen(http_headers), 0);
    if (written < 0) {
        ESP_LOGE(TAG, "Failed to send SSE headers for fd=%d: errno %d", fd, errno);
        httpd_req_async_handler_complete(async_req);
        return ESP_FAIL;
    }
    
    // Send initial connection event
    const char *init_msg = "event: connected\ndata: {\"status\":\"connected\"}\n\n";
    written = send(fd, init_msg, strlen(init_msg), 0);
    if (written < 0) {
        ESP_LOGE(TAG, "Failed to send SSE init for fd=%d: errno %d", fd, errno);
        httpd_req_async_handler_complete(async_req);
        return ESP_FAIL;
    }
    
    // Add to SSE client list for broadcast
    if (!sse_add_client(fd, async_req)) {
        // Client list full - complete the async request to close connection
        ESP_LOGW(TAG, "Rejecting SSE connection for fd=%d: client list full", fd);
        httpd_req_async_handler_complete(async_req);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "SSE connection established: fd=%d", fd);
    
    // Return ESP_OK - the async request keeps the connection open
    // The connection will be closed when sse_remove_client() is called
    return ESP_OK;
}

// ============================================================================
// WebSocket Support (kept as fallback)
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

static const httpd_uri_t uri_setup = {
    .uri = "/setup",
    .method = HTTP_GET,
    .handler = setup_handler,
    .user_ctx = NULL
};

// Captive portal detection URLs for various OS/browsers
static const httpd_uri_t uri_generate_204 = {
    .uri = "/generate_204",
    .method = HTTP_GET,
    .handler = captive_portal_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_gen_204 = {
    .uri = "/gen_204",
    .method = HTTP_GET,
    .handler = captive_portal_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_hotspot_detect = {
    .uri = "/hotspot-detect.html",
    .method = HTTP_GET,
    .handler = captive_portal_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_canonical = {
    .uri = "/canonical.html",
    .method = HTTP_GET,
    .handler = captive_portal_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_success = {
    .uri = "/success.txt",
    .method = HTTP_GET,
    .handler = captive_portal_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_ncsi = {
    .uri = "/ncsi.txt",
    .method = HTTP_GET,
    .handler = captive_portal_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_connecttest = {
    .uri = "/connecttest.txt",
    .method = HTTP_GET,
    .handler = captive_portal_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_redirect = {
    .uri = "/redirect",
    .method = HTTP_GET,
    .handler = captive_portal_handler,
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

// Inertia calibration endpoints
static const httpd_uri_t uri_api_calibrate_inertia_start = {
    .uri = "/api/calibrate/inertia/start",
    .method = HTTP_POST,
    .handler = api_calibrate_inertia_start_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_calibrate_inertia_status = {
    .uri = "/api/calibrate/inertia/status",
    .method = HTTP_GET,
    .handler = api_calibrate_inertia_status_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_calibrate_inertia_cancel = {
    .uri = "/api/calibrate/inertia/cancel",
    .method = HTTP_POST,
    .handler = api_calibrate_inertia_cancel_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_calibrate_inertia_apply = {
    .uri = "/api/calibrate/inertia/apply",
    .method = HTTP_POST,
    .handler = api_calibrate_inertia_apply_handler,
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

// SSE endpoint for real-time metrics streaming
static const httpd_uri_t uri_events = {
    .uri = "/events",
    .method = HTTP_GET,
    .handler = sse_handler,
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

static const httpd_uri_t uri_api_session_delete = {
    .uri = "/api/sessions/*",
    .method = HTTP_DELETE,
    .handler = api_session_delete_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_session_synced = {
    .uri = "/api/sessions/*",  // Wildcard must be at end for httpd_uri_match_wildcard
    .method = HTTP_POST,
    .handler = api_session_synced_handler,
    .user_ctx = NULL
};

// PUT handler for synced - companion app uses PUT instead of POST
static const httpd_uri_t uri_api_session_synced_put = {
    .uri = "/api/sessions/*",  // Wildcard must be at end for httpd_uri_match_wildcard
    .method = HTTP_PUT,
    .handler = api_session_synced_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_sessions_delete_synced = {
    .uri = "/api/sessions/synced",
    .method = HTTP_DELETE,
    .handler = api_sessions_delete_synced_handler,
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

static const httpd_uri_t uri_workout_pause = {
    .uri = "/workout/pause",
    .method = HTTP_POST,
    .handler = workout_pause_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_workout_resume = {
    .uri = "/workout/resume",
    .method = HTTP_POST,
    .handler = workout_resume_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_live_data = {
    .uri = "/live",
    .method = HTTP_GET,
    .handler = live_data_handler,
    .user_ctx = NULL
};

// WiFi captive portal endpoints
static const httpd_uri_t uri_api_wifi_scan = {
    .uri = "/api/wifi/scan",
    .method = HTTP_GET,
    .handler = api_wifi_scan_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_wifi_connect = {
    .uri = "/api/wifi/connect",
    .method = HTTP_POST,
    .handler = api_wifi_connect_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_wifi_status = {
    .uri = "/api/wifi/status",
    .method = HTTP_GET,
    .handler = api_wifi_status_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_wifi_disconnect = {
    .uri = "/api/wifi/disconnect",
    .method = HTTP_POST,
    .handler = api_wifi_disconnect_handler,
    .user_ctx = NULL
};

static const httpd_uri_t uri_api_reboot = {
    .uri = "/api/reboot",
    .method = HTTP_POST,
    .handler = api_reboot_handler,
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
 * Clean up WebSocket and SSE clients
 */
static void ws_close_callback(httpd_handle_t hd, int sockfd) {
    ESP_LOGD(TAG, "Connection closed on fd %d", sockfd);
    ws_remove_client(sockfd);   // Safe to call even if not a WS client
    sse_remove_client(sockfd);  // Safe to call even if not an SSE client
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
    
    // Create mutex for SSE client list
    if (g_sse_mutex == NULL) {
        g_sse_mutex = xSemaphoreCreateMutex();
        if (g_sse_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create SSE mutex");
            return ESP_FAIL;
        }
    }
    
    g_metrics = metrics;
    g_config = config;
    
    // Reset WebSocket client list
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        g_ws_fds[i] = -1;
    }
    
    // Reset SSE client list
    sse_init_clients();
    
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.server_port = WEB_SERVER_PORT;
    http_config.max_open_sockets = 10;   // Max allowed is 13 minus 3 internal = 10 for app use
    http_config.max_uri_handlers = 50;   // We have 42 handlers, set to 50 for headroom
    // Enable LRU purging to clean up stale connections when socket limit is reached.
    // Active SSE/WebSocket connections with recent activity are protected from purging.
    http_config.lru_purge_enable = true;
    http_config.uri_match_fn = httpd_uri_match_wildcard;
    http_config.open_fn = ws_open_callback;
    http_config.close_fn = ws_close_callback;
    http_config.recv_wait_timeout = 30;  // Increased timeout for SSE (30 seconds)
    http_config.send_wait_timeout = 30;  // Increased timeout for SSE (30 seconds)
    http_config.backlog_conn = 5;        // Pending connections in backlog
    http_config.keep_alive_enable = true; // Enable keep-alive for SSE connections
    
    ESP_LOGI(TAG, "Starting web server on port %d (max %d URI handlers)", 
             http_config.server_port, http_config.max_uri_handlers);
    
    esp_err_t ret = httpd_start(&g_server, &http_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Helper macro to register and check handler
    int registered = 0;
    #define REGISTER_URI(handler) do { \
        if (httpd_register_uri_handler(g_server, &handler) == ESP_OK) { \
            registered++; \
        } else { \
            ESP_LOGW(TAG, "Failed to register: %s", handler.uri); \
        } \
    } while(0)
    
    // Register URI handlers
    REGISTER_URI(uri_index);
    REGISTER_URI(uri_setup);
    REGISTER_URI(uri_style);
    REGISTER_URI(uri_app_js);
    REGISTER_URI(uri_favicon);
    REGISTER_URI(uri_api_metrics);
    REGISTER_URI(uri_api_status);
    REGISTER_URI(uri_api_reset);
    REGISTER_URI(uri_api_calibrate_inertia_start);
    REGISTER_URI(uri_api_calibrate_inertia_status);
    REGISTER_URI(uri_api_calibrate_inertia_cancel);
    REGISTER_URI(uri_api_calibrate_inertia_apply);
    REGISTER_URI(uri_api_config_get);
    REGISTER_URI(uri_api_config_post);
    REGISTER_URI(uri_events);  // SSE endpoint for real-time metrics
    REGISTER_URI(uri_ws);      // WebSocket fallback
    
    // Heart rate endpoints (HeartRateToWeb compatible)
    REGISTER_URI(uri_hr_post);
    REGISTER_URI(uri_hr_get);
    
    // Session management endpoints
    // NOTE: Order matters! More specific routes must come before wildcards
    REGISTER_URI(uri_api_sessions);                  // GET /api/sessions (exact)
    REGISTER_URI(uri_api_sessions_delete_synced);    // DELETE /api/sessions/synced (specific)
    REGISTER_URI(uri_api_session_synced);            // POST /api/sessions/* - handler validates /synced suffix
    REGISTER_URI(uri_api_session_synced_put);        // PUT /api/sessions/* - handler validates /synced suffix
    REGISTER_URI(uri_api_session_detail);            // GET /api/sessions/* (wildcard)
    REGISTER_URI(uri_api_session_delete);            // DELETE /api/sessions/* (wildcard)
    
    // Workout control endpoints
    REGISTER_URI(uri_workout_start);
    REGISTER_URI(uri_workout_stop);
    REGISTER_URI(uri_workout_pause);
    REGISTER_URI(uri_workout_resume);
    REGISTER_URI(uri_live_data);
    
    // WiFi captive portal endpoints
    REGISTER_URI(uri_api_wifi_scan);
    REGISTER_URI(uri_api_wifi_connect);
    REGISTER_URI(uri_api_wifi_status);
    REGISTER_URI(uri_api_wifi_disconnect);
    REGISTER_URI(uri_api_reboot);
    
    // Captive portal detection handlers (redirect to setup page)
    REGISTER_URI(uri_generate_204);
    REGISTER_URI(uri_gen_204);
    REGISTER_URI(uri_hotspot_detect);
    REGISTER_URI(uri_canonical);
    REGISTER_URI(uri_success);
    REGISTER_URI(uri_ncsi);
    REGISTER_URI(uri_connecttest);
    REGISTER_URI(uri_redirect);
    
    #undef REGISTER_URI
    
    ESP_LOGI(TAG, "Web server started successfully (%d handlers registered)", registered);
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
    
    // Clean up SSE client list (complete async requests)
    SSE_MUTEX_TAKE();
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (g_sse_clients[i].async_req != NULL) {
            httpd_req_async_handler_complete(g_sse_clients[i].async_req);
        }
        g_sse_clients[i].fd = -1;
        g_sse_clients[i].async_req = NULL;
    }
    SSE_MUTEX_GIVE();
    
    // Delete mutexes
    if (g_ws_mutex != NULL) {
        vSemaphoreDelete(g_ws_mutex);
        g_ws_mutex = NULL;
    }
    if (g_sse_mutex != NULL) {
        vSemaphoreDelete(g_sse_mutex);
        g_sse_mutex = NULL;
    }
}

/**
 * Get HTTP server handle for sharing with provisioning
 */
httpd_handle_t web_server_get_handle(void) {
    return g_server;
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
    
    // Also broadcast to SSE clients
    // SSE format: "data: <json>\n\n"
    char sse_buffer[JSON_BUFFER_SIZE + 16];
    int sse_len = snprintf(sse_buffer, sizeof(sse_buffer), "data: %s\n\n", buffer);
    
    // Take a snapshot of SSE client fds
    int sse_fds_to_send[MAX_SSE_CLIENTS];
    SSE_MUTEX_TAKE();
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        sse_fds_to_send[i] = g_sse_clients[i].fd;
    }
    SSE_MUTEX_GIVE();
    
    // Send to all SSE clients
    int sse_dead_fds[MAX_SSE_CLIENTS];
    int sse_dead_count = 0;
    
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        int fd = sse_fds_to_send[i];
        if (fd >= 0) {
            // Check if socket is still valid
            if (!is_socket_valid(g_server, fd)) {
                ESP_LOGD(TAG, "SSE socket fd %d is no longer valid", fd);
                sse_dead_fds[sse_dead_count++] = fd;
                continue;
            }
            
            // Send SSE data directly via socket
            int written = send(fd, sse_buffer, sse_len, MSG_DONTWAIT);
            if (written < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    ESP_LOGD(TAG, "SSE send failed for fd %d: errno %d", fd, errno);
                    sse_dead_fds[sse_dead_count++] = fd;
                }
            } else {
                sent_count++;
            }
        }
    }
    
    // Remove dead SSE clients (this also completes async requests)
    for (int d = 0; d < sse_dead_count; d++) {
        sse_remove_client(sse_dead_fds[d]);
    }
    
    return (sent_count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

/**
 * Check if any WebSocket or SSE clients are connected (thread-safe)
 */
bool web_server_has_ws_clients(void) {
    bool has_clients = false;
    
    // Check WebSocket clients
    WS_MUTEX_TAKE();
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (g_ws_fds[i] >= 0) {
            has_clients = true;
            break;
        }
    }
    WS_MUTEX_GIVE();
    
    if (has_clients) {
        return true;
    }
    
    // Check SSE clients
    SSE_MUTEX_TAKE();
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (g_sse_clients[i].fd >= 0) {
            has_clients = true;
            break;
        }
    }
    SSE_MUTEX_GIVE();
    
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
    
    SSE_MUTEX_TAKE();
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (g_sse_clients[i].fd >= 0) {
            count++;
        }
    }
    SSE_MUTEX_GIVE();
    
    return count;
}

/**
 * Update inertia calibration with flywheel data
 */
bool web_server_update_inertia_calibration(float angular_velocity, int64_t current_time_us) {
    if (g_inertia_calibration.state == CALIBRATION_IDLE ||
        g_inertia_calibration.state == CALIBRATION_COMPLETE ||
        g_inertia_calibration.state == CALIBRATION_FAILED) {
        return false;
    }
    
    return rowing_physics_update_inertia_calibration(&g_inertia_calibration, angular_velocity, current_time_us);
}

/**
 * Check if inertia calibration is currently active
 */
bool web_server_is_calibrating_inertia(void) {
    return (g_inertia_calibration.state == CALIBRATION_WAITING ||
            g_inertia_calibration.state == CALIBRATION_SPINUP ||
            g_inertia_calibration.state == CALIBRATION_SPINDOWN);
}

/**
 * Start a minimal HTTP server for captive portal during provisioning
 * 
 * This creates a lightweight HTTP server with only captive portal handlers,
 * WITHOUT the wildcard URI matcher (which is incompatible with protocomm).
 * This server can be shared with the provisioning manager.
 * 
 * @return ESP_OK on success
 */
esp_err_t web_server_start_captive_portal(void) {
    if (g_server != NULL) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }
    
    // Create mutex for SSE client list (needed for /events endpoint)
    if (g_sse_mutex == NULL) {
        g_sse_mutex = xSemaphoreCreateMutex();
        if (g_sse_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create SSE mutex");
            return ESP_FAIL;
        }
    }
    
    // Initialize SSE client list
    sse_init_clients();
    
    // Note: g_metrics and g_config may be NULL in captive portal mode
    // Handlers should check for NULL before dereferencing
    
    // Captive portal config - enough handlers for rowing monitor + setup
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.server_port = WEB_SERVER_PORT;
    http_config.max_open_sockets = 7;        // Minimal for provisioning + rowing
    http_config.max_uri_handlers = 30;       // Captive portal + rowing monitor endpoints
    http_config.lru_purge_enable = true;
    // NOTE: Do NOT set uri_match_fn to wildcard - it's incompatible with protocomm
    http_config.recv_wait_timeout = 10;
    http_config.send_wait_timeout = 10;
    
    ESP_LOGI(TAG, "Starting captive portal HTTP server on port %d", http_config.server_port);
    
    esp_err_t ret = httpd_start(&g_server, &http_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start captive portal server: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register only captive portal handlers
    int registered = 0;
    #define REGISTER_CAPTIVE_URI(handler) do { \
        if (httpd_register_uri_handler(g_server, &handler) == ESP_OK) { \
            registered++; \
        } else { \
            ESP_LOGW(TAG, "Failed to register captive: %s", handler.uri); \
        } \
    } while(0)
    
    // Core pages
    REGISTER_CAPTIVE_URI(uri_index);          // /
    REGISTER_CAPTIVE_URI(uri_setup);          // /setup
    REGISTER_CAPTIVE_URI(uri_style);          // /style.css
    REGISTER_CAPTIVE_URI(uri_app_js);         // /app.js
    REGISTER_CAPTIVE_URI(uri_favicon);        // /favicon.ico
    
    // Rowing monitor API endpoints (needed for index.html to work in AP mode)
    REGISTER_CAPTIVE_URI(uri_api_metrics);    // /api/metrics
    REGISTER_CAPTIVE_URI(uri_api_status);     // /api/status
    REGISTER_CAPTIVE_URI(uri_events);         // /events (SSE)
    REGISTER_CAPTIVE_URI(uri_api_config_get); // /api/config (GET)
    REGISTER_CAPTIVE_URI(uri_api_sessions);   // /api/sessions (GET)
    
    // Workout control endpoints (needed for rowing in AP mode)
    REGISTER_CAPTIVE_URI(uri_workout_start);  // /workout/start
    REGISTER_CAPTIVE_URI(uri_workout_stop);   // /workout/stop
    REGISTER_CAPTIVE_URI(uri_workout_pause);  // /workout/pause
    REGISTER_CAPTIVE_URI(uri_workout_resume); // /workout/resume
    
    // WiFi provisioning API endpoints (needed for setup.html)
    REGISTER_CAPTIVE_URI(uri_api_wifi_scan);       // /api/wifi/scan
    REGISTER_CAPTIVE_URI(uri_api_wifi_connect);    // /api/wifi/connect
    REGISTER_CAPTIVE_URI(uri_api_wifi_status);     // /api/wifi/status
    REGISTER_CAPTIVE_URI(uri_api_wifi_disconnect); // /api/wifi/disconnect
    REGISTER_CAPTIVE_URI(uri_api_reboot);          // /api/reboot
    
    // Captive portal detection URLs
    REGISTER_CAPTIVE_URI(uri_generate_204);   // /generate_204 (Android)
    REGISTER_CAPTIVE_URI(uri_gen_204);        // /gen_204 (Android)
    REGISTER_CAPTIVE_URI(uri_hotspot_detect); // /hotspot-detect.html (Apple)
    REGISTER_CAPTIVE_URI(uri_canonical);      // /canonical.html (Apple)
    REGISTER_CAPTIVE_URI(uri_success);        // /success.txt (Windows)
    REGISTER_CAPTIVE_URI(uri_ncsi);           // /ncsi.txt (Windows)
    REGISTER_CAPTIVE_URI(uri_connecttest);    // /connecttest.txt (Windows)
    REGISTER_CAPTIVE_URI(uri_redirect);       // /redirect
    
    #undef REGISTER_CAPTIVE_URI
    
    ESP_LOGI(TAG, "Captive portal server started (%d handlers registered)", registered);
    return ESP_OK;
}
