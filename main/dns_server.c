/**
 * @file dns_server.c
 * @brief Simple DNS server for captive portal
 * 
 * Redirects all DNS queries to the ESP32's IP address.
 * This enables captive portal detection on phones/laptops.
 */

#include "dns_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "DNS_SERVER";

#define DNS_PORT 53
#define DNS_MAX_PACKET_SIZE 512

// DNS header structure
typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;  // Question count
    uint16_t ancount;  // Answer count
    uint16_t nscount;  // Authority count
    uint16_t arcount;  // Additional count
} dns_header_t;

static int s_dns_socket = -1;
static TaskHandle_t s_dns_task_handle = NULL;
static bool s_running = false;
static uint32_t s_redirect_ip = 0;

// Answer section size: 2 (name ptr) + 2 (type) + 2 (class) + 4 (ttl) + 2 (rdlen) + 4 (IP) = 16 bytes
#define DNS_ANSWER_SIZE 16

/**
 * Parse DNS question name and return pointer to next section
 * Returns NULL if parsing fails or bounds are exceeded
 */
static const uint8_t* parse_dns_name(const uint8_t *query, const uint8_t *packet_end,
                                      char *name_out, size_t name_max) {
    const uint8_t *p = query;
    size_t name_len = 0;
    
    // Safety limit to prevent infinite loops
    int max_iterations = 128;
    
    while (p < packet_end && *p != 0 && max_iterations-- > 0) {
        if ((*p & 0xC0) == 0xC0) {
            // Compression pointer - skip 2 bytes
            if (p + 2 > packet_end) return NULL;
            p += 2;
            break;
        }
        
        uint8_t label_len = *p++;
        if (p + label_len > packet_end) return NULL;
        if (name_len + label_len + 1 >= name_max) break;
        
        if (name_len > 0) {
            name_out[name_len++] = '.';
        }
        
        memcpy(name_out + name_len, p, label_len);
        name_len += label_len;
        p += label_len;
    }
    
    if (p < packet_end && *p == 0) p++;  // Skip null terminator
    
    name_out[name_len] = '\0';
    return p;
}

/**
 * Build DNS response packet
 */
static int build_dns_response(const uint8_t *query, int query_len, 
                               uint8_t *response, size_t response_max) {
    // Validate input
    if (query_len < (int)sizeof(dns_header_t) + 5) {
        return -1;
    }
    
    // Check if response will fit (query + answer section)
    if ((size_t)(query_len + DNS_ANSWER_SIZE) > response_max) {
        return -1;
    }
    
    // Copy query to response
    memcpy(response, query, query_len);
    
    dns_header_t *header = (dns_header_t *)response;
    
    // Set response flags
    header->flags = htons(0x8180);  // Standard response, no error
    header->ancount = htons(1);     // One answer
    
    // Find end of question section with bounds checking
    const uint8_t *packet_end = query + query_len;
    const uint8_t *q_ptr = query + sizeof(dns_header_t);
    char qname[128];
    q_ptr = parse_dns_name(q_ptr, packet_end, qname, sizeof(qname));
    
    if (q_ptr == NULL || q_ptr + 4 > packet_end) {
        return -1;  // Malformed packet
    }
    q_ptr += 4;  // Skip QTYPE and QCLASS
    
    ESP_LOGD(TAG, "DNS query for: %s", qname);
    
    // Build answer section
    uint8_t *ans = response + query_len;
    int ans_len = 0;
    
    // Name pointer to question (compression)
    ans[ans_len++] = 0xC0;
    ans[ans_len++] = sizeof(dns_header_t);
    
    // TYPE A (1)
    ans[ans_len++] = 0x00;
    ans[ans_len++] = 0x01;
    
    // CLASS IN (1)
    ans[ans_len++] = 0x00;
    ans[ans_len++] = 0x01;
    
    // TTL (300 seconds)
    ans[ans_len++] = 0x00;
    ans[ans_len++] = 0x00;
    ans[ans_len++] = 0x01;
    ans[ans_len++] = 0x2C;
    
    // RDLENGTH (4 bytes for IPv4)
    ans[ans_len++] = 0x00;
    ans[ans_len++] = 0x04;
    
    // RDATA (IP address)
    memcpy(ans + ans_len, &s_redirect_ip, 4);
    ans_len += 4;
    
    return query_len + ans_len;
}

/**
 * DNS server task
 */
static void dns_server_task(void *pvParameters) {
    uint8_t rx_buffer[DNS_MAX_PACKET_SIZE];
    uint8_t tx_buffer[DNS_MAX_PACKET_SIZE];
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    ESP_LOGI(TAG, "DNS server task started, listening on port %d", DNS_PORT);
    
    while (s_running) {
        client_addr_len = sizeof(client_addr);  // Reset each iteration
        int len = recvfrom(s_dns_socket, rx_buffer, sizeof(rx_buffer), 0,
                           (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            if (s_running) {
                ESP_LOGE(TAG, "recvfrom failed: %d", errno);
            }
            break;
        }
        
        if (len < (int)sizeof(dns_header_t)) {
            continue;
        }
        
        // Log that we received a DNS query
        ESP_LOGI(TAG, "DNS query received from " IPSTR, IP2STR((esp_ip4_addr_t*)&client_addr.sin_addr.s_addr));
        
        // Build response
        int resp_len = build_dns_response(rx_buffer, len, tx_buffer, sizeof(tx_buffer));
        if (resp_len > 0) {
            int sent = sendto(s_dns_socket, tx_buffer, resp_len, 0,
                   (struct sockaddr *)&client_addr, client_addr_len);
            if (sent < 0) {
                ESP_LOGE(TAG, "Failed to send DNS response: %d", errno);
            } else {
                ESP_LOGD(TAG, "DNS response sent (%d bytes)", sent);
            }
        }
    }
    
    ESP_LOGI(TAG, "DNS server task stopped");
    s_dns_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * Start DNS server for captive portal
 */
esp_err_t dns_server_start(const char *ip_addr) {
    if (s_running) {
        ESP_LOGW(TAG, "DNS server already running");
        return ESP_OK;
    }
    
    // Parse IP address
    struct in_addr addr;
    if (inet_aton(ip_addr, &addr) == 0) {
        ESP_LOGE(TAG, "Invalid IP address: %s", ip_addr);
        return ESP_ERR_INVALID_ARG;
    }
    s_redirect_ip = addr.s_addr;
    
    // Create UDP socket
    s_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: %d", errno);
        return ESP_FAIL;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(s_dns_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Set non-blocking with timeout
    struct timeval timeout = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(s_dns_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    // Bind to DNS port
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    
    if (bind(s_dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: %d", errno);
        close(s_dns_socket);
        s_dns_socket = -1;
        return ESP_FAIL;
    }
    
    s_running = true;
    
    // Create DNS server task
    BaseType_t ret = xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &s_dns_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS task");
        close(s_dns_socket);
        s_dns_socket = -1;
        s_running = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "DNS server started, redirecting to %s", ip_addr);
    return ESP_OK;
}

/**
 * Stop DNS server
 */
void dns_server_stop(void) {
    if (!s_running) {
        return;
    }
    
    // Close socket first to unblock recvfrom
    if (s_dns_socket >= 0) {
        shutdown(s_dns_socket, SHUT_RDWR);
        close(s_dns_socket);
        s_dns_socket = -1;
    }
    
    // Now signal task to stop
    s_running = false;
    
    // Wait for task to finish
    vTaskDelay(pdMS_TO_TICKS(200));
    s_dns_task_handle = NULL;
    
    ESP_LOGI(TAG, "DNS server stopped");
}

/**
 * Check if DNS server is running
 */
bool dns_server_is_running(void) {
    return s_running;
}
