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
 */

#include "web_server.h"
#include "app_config.h"
#include "metrics_calculator.h"
#include "config_manager.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "WEB_SERVER";

// HTTP server handle
static httpd_handle_t g_server = NULL;

// WebSocket file descriptors for connected clients
#define MAX_WS_CLIENTS 4
static int g_ws_fds[MAX_WS_CLIENTS] = {-1, -1, -1, -1};

// Pointers to shared data
static rowing_metrics_t *g_metrics = NULL;
static config_t *g_config = NULL;

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
// WebSocket Handler
// ============================================================================

/**
 * Add WebSocket client to list
 */
static void ws_add_client(int fd) {
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (g_ws_fds[i] < 0) {
            g_ws_fds[i] = fd;
            ESP_LOGI(TAG, "WebSocket client added: fd=%d", fd);
            return;
        }
    }
    ESP_LOGW(TAG, "WebSocket client list full");
}

/**
 * Remove WebSocket client from list
 */
static void ws_remove_client(int fd) {
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (g_ws_fds[i] == fd) {
            g_ws_fds[i] = -1;
            ESP_LOGI(TAG, "WebSocket client removed: fd=%d", fd);
            return;
        }
    }
}

/**
 * WebSocket handler
 */
static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        // WebSocket handshake
        ESP_LOGI(TAG, "WebSocket handshake initiated");
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

// ============================================================================
// Open/Close Callbacks for WebSocket tracking
// ============================================================================

static esp_err_t ws_open_callback(httpd_handle_t hd, int sockfd) {
    ESP_LOGI(TAG, "New connection on fd %d", sockfd);
    ws_add_client(sockfd);
    return ESP_OK;
}

static void ws_close_callback(httpd_handle_t hd, int sockfd) {
    ESP_LOGI(TAG, "Connection closed on fd %d", sockfd);
    ws_remove_client(sockfd);
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
}

/**
 * Broadcast metrics to all connected WebSocket clients
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
    
    // Send to all connected WebSocket clients
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)buffer;
    ws_pkt.len = len;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    int sent_count = 0;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (g_ws_fds[i] >= 0) {
            esp_err_t ret = httpd_ws_send_frame_async(g_server, g_ws_fds[i], &ws_pkt);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send to fd %d: %s", g_ws_fds[i], esp_err_to_name(ret));
                // Remove dead client
                g_ws_fds[i] = -1;
            } else {
                sent_count++;
            }
        }
    }
    
    return (sent_count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

/**
 * Check if any WebSocket clients are connected
 */
bool web_server_has_ws_clients(void) {
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (g_ws_fds[i] >= 0) {
            return true;
        }
    }
    return false;
}

/**
 * Get number of active connections
 */
int web_server_get_connection_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (g_ws_fds[i] >= 0) {
            count++;
        }
    }
    return count;
}
