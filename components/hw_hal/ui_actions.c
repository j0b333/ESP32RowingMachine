/**
 * @file ui_actions.c
 * @brief Tiny dispatcher: queues UI actions from any context and invokes
 *        the registered handler from a worker task.
 */

#include "hw_hal.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define UI_ACTIONS_QUEUE_LEN     16
#define UI_ACTIONS_TASK_STACK    3072
#define UI_ACTIONS_TASK_PRIO     4

static QueueHandle_t        s_q = NULL;
static TaskHandle_t         s_task = NULL;
static ui_action_handler_t  s_cb = NULL;
static void                *s_user = NULL;

static void worker(void *arg)
{
    (void)arg;
    ui_action_t a;
    for (;;) {
        if (xQueueReceive(s_q, &a, portMAX_DELAY) == pdTRUE) {
            ui_action_handler_t cb = s_cb;
            void *u = s_user;
            if (cb && a != UI_ACTION_NONE && a < UI_ACTION__COUNT) {
                cb(a, u);
            }
        }
    }
}

int ui_actions_init(void)
{
    if (s_q) return 0;
    s_q = xQueueCreate(UI_ACTIONS_QUEUE_LEN, sizeof(ui_action_t));
    if (!s_q) return -1;
    BaseType_t r = xTaskCreate(worker, "ui_actions",
                               UI_ACTIONS_TASK_STACK, NULL,
                               UI_ACTIONS_TASK_PRIO, &s_task);
    if (r != pdPASS) {
        vQueueDelete(s_q);
        s_q = NULL;
        return -1;
    }
    return 0;
}

void ui_actions_set_handler(ui_action_handler_t cb, void *user)
{
    s_cb = cb;
    s_user = user;
}

int ui_actions_post(ui_action_t a)
{
    if (!s_q || a == UI_ACTION_NONE) return -1;
    return (xQueueSend(s_q, &a, 0) == pdTRUE) ? 0 : -1;
}

int ui_actions_post_isr(ui_action_t a)
{
    if (!s_q || a == UI_ACTION_NONE) return -1;
    BaseType_t hpw = pdFALSE;
    BaseType_t ok  = xQueueSendFromISR(s_q, &a, &hpw);
    if (hpw) portYIELD_FROM_ISR();
    return ok == pdTRUE ? 0 : -1;
}
