/**
 * @file sensor_manager.c
 * @brief GPIO interrupt handlers with debouncing for rowing sensors
 * 
 * Critical Performance Requirements:
 * - ISR execution time < 50 microseconds
 * - No blocking operations in ISR
 * - Thread-safe counter increments
 * - Microsecond-precision timestamps
 * 
 * Compatible with ESP-IDF 6.0+
 */

#include "sensor_manager.h"
#include "app_config.h"
#include "stroke_detector.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_attr.h"

static const char *TAG = "SENSOR";

// Event bits for signaling tasks
#define FLYWHEEL_EVENT_BIT  BIT0
#define SEAT_EVENT_BIT      BIT1

// Global volatile counters (accessed from ISR and main tasks)
static volatile uint32_t g_flywheel_pulse_count = 0;
static volatile int64_t g_last_flywheel_time_us = 0;

static volatile uint32_t g_seat_trigger_count = 0;
static volatile int64_t g_last_seat_time_us = 0;

// Event group for signaling tasks
static EventGroupHandle_t sensor_event_group = NULL;

// Task handle
static TaskHandle_t sensor_task_handle = NULL;

// Task control
static volatile bool task_running = false;

/**
 * Flywheel sensor ISR
 * 
 * CRITICAL: Keep this as fast as possible
 * - Read timestamp
 * - Simple debounce check
 * - Increment counter
 * - Set event bit
 * - NO logging, NO complex math
 */
static void IRAM_ATTR flywheel_isr_handler(void* arg) {
    int64_t now = esp_timer_get_time();
    
    // Debounce: ignore if too soon after last pulse
    if ((now - g_last_flywheel_time_us) > FLYWHEEL_DEBOUNCE_US) {
        g_last_flywheel_time_us = now;
        g_flywheel_pulse_count++;
        
        // Signal processing task (non-blocking)
        if (sensor_event_group != NULL) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xEventGroupSetBitsFromISR(sensor_event_group, FLYWHEEL_EVENT_BIT, 
                                      &xHigherPriorityTaskWoken);
            
            if (xHigherPriorityTaskWoken == pdTRUE) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

/**
 * Seat position sensor ISR
 */
static void IRAM_ATTR seat_isr_handler(void* arg) {
    int64_t now = esp_timer_get_time();
    
    // Debounce: ignore if too soon after last trigger
    if ((now - g_last_seat_time_us) > SEAT_DEBOUNCE_US) {
        g_last_seat_time_us = now;
        g_seat_trigger_count++;
        
        // Signal processing task (non-blocking)
        if (sensor_event_group != NULL) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xEventGroupSetBitsFromISR(sensor_event_group, SEAT_EVENT_BIT,
                                      &xHigherPriorityTaskWoken);
            
            if (xHigherPriorityTaskWoken == pdTRUE) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

/**
 * Sensor processing task
 * Waits for events from ISR, performs detailed processing
 */
static void sensor_processing_task(void *arg) {
    rowing_metrics_t *metrics = (rowing_metrics_t*)arg;
    
    ESP_LOGI(TAG, "Sensor processing task started");
    task_running = true;
    
    while (task_running) {
        // Wait for sensor events (block until event or timeout)
        EventBits_t bits = xEventGroupWaitBits(
            sensor_event_group,
            FLYWHEEL_EVENT_BIT | SEAT_EVENT_BIT,
            pdTRUE,  // Clear bits on exit
            pdFALSE, // Don't wait for all bits
            pdMS_TO_TICKS(100)  // 100ms timeout for idle check
        );
        
        if (bits & FLYWHEEL_EVENT_BIT) {
            // Flywheel pulse detected - process physics
            int64_t pulse_time = g_last_flywheel_time_us;
            rowing_physics_process_flywheel_pulse(metrics, pulse_time);
            
            // Update stroke detection
            stroke_detector_update(metrics);
        }
        
        if (bits & SEAT_EVENT_BIT) {
            // Seat trigger detected
            stroke_detector_process_seat_trigger(metrics);
        }
        
        // Check for idle timeout
        int64_t now = esp_timer_get_time();
        int64_t time_since_last_pulse = now - g_last_flywheel_time_us;
        
        if (time_since_last_pulse > (IDLE_TIMEOUT_MS * 1000LL)) {
            // Mark as idle
            if (metrics->is_active) {
                metrics->is_active = false;
                metrics->current_phase = STROKE_PHASE_IDLE;
                ESP_LOGI(TAG, "Rowing stopped (idle timeout)");
            }
        } else if (g_flywheel_pulse_count > 0) {
            if (!metrics->is_active) {
                metrics->is_active = true;
                ESP_LOGI(TAG, "Rowing started");
            }
        }
        
        // Update elapsed time
        rowing_physics_update_elapsed_time(metrics);
    }
    
    ESP_LOGI(TAG, "Sensor processing task stopped");
    vTaskDelete(NULL);
}

/**
 * Initialize sensor GPIO and interrupts
 */
esp_err_t sensor_manager_init(void) {
    esp_err_t ret;
    
    // Create event group
    sensor_event_group = xEventGroupCreate();
    if (sensor_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create sensor event group");
        return ESP_FAIL;
    }
    
    // Configure flywheel sensor pin
    gpio_config_t flywheel_conf = {
        .pin_bit_mask = (1ULL << GPIO_FLYWHEEL_SENSOR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,   // Use internal pull-up (no external power needed)
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE      // Trigger on falling edge (HIGH→LOW)
    };
    ret = gpio_config(&flywheel_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure flywheel GPIO: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure seat sensor pin
    gpio_config_t seat_conf = {
        .pin_bit_mask = (1ULL << GPIO_SEAT_SENSOR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,   // Use internal pull-up (no external power needed)
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE      // Trigger on falling edge
    };
    ret = gpio_config(&seat_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure seat GPIO: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Install GPIO ISR service with appropriate priority
    ret = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE means already installed, which is OK
        ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Hook up interrupt handlers
    ret = gpio_isr_handler_add(GPIO_FLYWHEEL_SENSOR, flywheel_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add flywheel ISR handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = gpio_isr_handler_add(GPIO_SEAT_SENSOR, seat_isr_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add seat ISR handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Sensor manager initialized");
    ESP_LOGI(TAG, "Flywheel sensor: GPIO%d (active LOW)", GPIO_FLYWHEEL_SENSOR);
    ESP_LOGI(TAG, "Seat sensor: GPIO%d (active LOW)", GPIO_SEAT_SENSOR);
    
    return ESP_OK;
}

/**
 * Deinitialize sensor manager
 */
void sensor_manager_deinit(void) {
    // Remove ISR handlers
    gpio_isr_handler_remove(GPIO_FLYWHEEL_SENSOR);
    gpio_isr_handler_remove(GPIO_SEAT_SENSOR);
    
    // Uninstall ISR service
    gpio_uninstall_isr_service();
    
    // Delete event group
    if (sensor_event_group != NULL) {
        vEventGroupDelete(sensor_event_group);
        sensor_event_group = NULL;
    }
    
    ESP_LOGI(TAG, "Sensor manager deinitialized");
}

/**
 * Start sensor processing task
 */
esp_err_t sensor_manager_start_task(rowing_metrics_t *metrics, const config_t *config) {
    if (sensor_task_handle != NULL) {
        ESP_LOGW(TAG, "Sensor task already running");
        return ESP_ERR_INVALID_STATE;
    }
    
    BaseType_t result = xTaskCreate(
        sensor_processing_task,
        "sensor_task",
        SENSOR_TASK_STACK_SIZE,
        (void*)metrics,
        SENSOR_TASK_PRIORITY,
        &sensor_task_handle
    );
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sensor task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Sensor processing task started");
    return ESP_OK;
}

/**
 * Stop sensor processing task
 */
void sensor_manager_stop_task(void) {
    task_running = false;
    
    // Give task time to exit
    vTaskDelay(pdMS_TO_TICKS(200));
    
    sensor_task_handle = NULL;
}

// Accessor functions for ISR data (thread-safe reads)
uint32_t sensor_get_flywheel_count(void) {
    return g_flywheel_pulse_count;
}

int64_t sensor_get_last_flywheel_time(void) {
    return g_last_flywheel_time_us;
}

uint32_t sensor_get_seat_count(void) {
    return g_seat_trigger_count;
}

int64_t sensor_get_last_seat_time(void) {
    return g_last_seat_time_us;
}

bool sensor_manager_is_active(void) {
    int64_t now = esp_timer_get_time();
    return (now - g_last_flywheel_time_us) < (IDLE_TIMEOUT_MS * 1000LL);
}

void sensor_manager_reset_counters(void) {
    g_flywheel_pulse_count = 0;
    g_last_flywheel_time_us = 0;
    g_seat_trigger_count = 0;
    g_last_seat_time_us = 0;
    ESP_LOGI(TAG, "Sensor counters reset");
}
