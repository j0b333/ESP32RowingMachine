/**
 * @file hw_hal.h
 * @brief Shared types for the hardware abstraction layer (HAL).
 *
 * Used by the display, touch, input, audio and indicator components so
 * the application code can talk to a uniform abstraction while the
 * actual hardware driver is selected via Kconfig.
 */

#ifndef HW_HAL_H
#define HW_HAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/*                              UI ACTIONS                                    */
/* ------------------------------------------------------------------------- */

/**
 * High level UI / control actions produced by physical inputs (buttons,
 * rotary encoders, touch widgets) and consumed by the action dispatcher.
 *
 * New actions can be appended at the end without breaking persistent
 * mappings.
 */
typedef enum {
    UI_ACTION_NONE = 0,

    /* Workout control */
    UI_ACTION_START_STOP,        /* 1 */
    UI_ACTION_PAUSE_RESUME,      /* 2 */
    UI_ACTION_LAP,               /* 3 */
    UI_ACTION_RESET_SESSION,     /* 4 */

    /* Screen navigation */
    UI_ACTION_NEXT_SCREEN,       /* 5 */
    UI_ACTION_PREV_SCREEN,       /* 6 */
    UI_ACTION_SCREEN_HOME,       /* 7 */

    /* Brightness / volume */
    UI_ACTION_BRIGHTNESS_UP,     /* 8 */
    UI_ACTION_BRIGHTNESS_DOWN,   /* 9 */
    UI_ACTION_VOLUME_UP,         /* 10 */
    UI_ACTION_VOLUME_DOWN,       /* 11 */
    UI_ACTION_MUTE_TOGGLE,       /* 12 */

    /* Connectivity */
    UI_ACTION_TOGGLE_HR_SCAN,    /* 13 */
    UI_ACTION_WIFI_PROVISION,    /* 14 */
    UI_ACTION_BLE_TOGGLE,        /* 15 */

    /* Programmable user actions (mappable from web UI) */
    UI_ACTION_USER_1,            /* 16 */
    UI_ACTION_USER_2,
    UI_ACTION_USER_3,
    UI_ACTION_USER_4,
    UI_ACTION_USER_5,
    UI_ACTION_USER_6,
    UI_ACTION_USER_7,
    UI_ACTION_USER_8,            /* 23 */

    UI_ACTION__COUNT             /* sentinel — keep last */
} ui_action_t;

/** Press kinds produced by the button manager. */
typedef enum {
    BUTTON_EVENT_PRESS = 0,
    BUTTON_EVENT_RELEASE,
    BUTTON_EVENT_SHORT,
    BUTTON_EVENT_LONG,
    BUTTON_EVENT_DOUBLE,
    BUTTON_EVENT_HOLD_REPEAT,
} button_event_t;

/* ------------------------------------------------------------------------- */
/*                              GRAPHICS TYPES                                */
/* ------------------------------------------------------------------------- */

/** RGB565 color (R5 G6 B5, big-endian on the wire for most controllers). */
typedef uint16_t hw_color_t;

#define HW_COLOR_BLACK     0x0000
#define HW_COLOR_WHITE     0xFFFF
#define HW_COLOR_RED       0xF800
#define HW_COLOR_GREEN     0x07E0
#define HW_COLOR_BLUE      0x001F
#define HW_COLOR_YELLOW    0xFFE0
#define HW_COLOR_CYAN      0x07FF
#define HW_COLOR_MAGENTA   0xF81F
#define HW_COLOR_GRAY      0x8410
#define HW_COLOR_DARKGRAY  0x4208

static inline hw_color_t hw_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (hw_color_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/** Touch / pointer event delivered upward by the touch HAL. */
typedef struct {
    int16_t x;          /* X in display pixels (after rotation), -1 if none */
    int16_t y;          /* Y in display pixels (after rotation) */
    bool    pressed;    /* true while touch contact is active */
} touch_event_t;

/* ------------------------------------------------------------------------- */
/*                              UTILITY                                       */
/* ------------------------------------------------------------------------- */

/** Stable lowercase string name for a UI action. Never returns NULL. */
const char *hw_hal_action_name(ui_action_t a);

/* ------------------------------------------------------------------------- */
/*                          UI ACTION DISPATCHER                              */
/* ------------------------------------------------------------------------- */

/**
 * Callback invoked from the action dispatcher task whenever an action is
 * posted. The handler is executed on a normal FreeRTOS task (never from
 * an ISR) and may perform blocking work.
 */
typedef void (*ui_action_handler_t)(ui_action_t action, void *user);

/** Initialize the dispatcher (creates internal queue + worker task).
 *  Returns 0 on success. Safe to call more than once. */
int  ui_actions_init(void);

/** Register the handler. Only one handler is active at a time. */
void ui_actions_set_handler(ui_action_handler_t cb, void *user);

/** Post an action from a normal task context. Returns 0 on success. */
int  ui_actions_post(ui_action_t action);

/** Post an action from an ISR. Returns 0 on success. */
int  ui_actions_post_isr(ui_action_t action);

#ifdef __cplusplus
}
#endif

#endif /* HW_HAL_H */
