/**
 * @file hw_hal.c
 * @brief Lookup helpers for hw_hal types.
 */

#include "hw_hal.h"

const char *hw_hal_action_name(ui_action_t a)
{
    switch (a) {
        case UI_ACTION_NONE:            return "none";
        case UI_ACTION_START_STOP:      return "start_stop";
        case UI_ACTION_PAUSE_RESUME:    return "pause_resume";
        case UI_ACTION_LAP:             return "lap";
        case UI_ACTION_RESET_SESSION:   return "reset_session";
        case UI_ACTION_NEXT_SCREEN:     return "next_screen";
        case UI_ACTION_PREV_SCREEN:     return "prev_screen";
        case UI_ACTION_SCREEN_HOME:     return "screen_home";
        case UI_ACTION_BRIGHTNESS_UP:   return "brightness_up";
        case UI_ACTION_BRIGHTNESS_DOWN: return "brightness_down";
        case UI_ACTION_VOLUME_UP:       return "volume_up";
        case UI_ACTION_VOLUME_DOWN:     return "volume_down";
        case UI_ACTION_MUTE_TOGGLE:     return "mute_toggle";
        case UI_ACTION_TOGGLE_HR_SCAN:  return "hr_scan_toggle";
        case UI_ACTION_WIFI_PROVISION:  return "wifi_provision";
        case UI_ACTION_BLE_TOGGLE:      return "ble_toggle";
        case UI_ACTION_USER_1:          return "user_1";
        case UI_ACTION_USER_2:          return "user_2";
        case UI_ACTION_USER_3:          return "user_3";
        case UI_ACTION_USER_4:          return "user_4";
        case UI_ACTION_USER_5:          return "user_5";
        case UI_ACTION_USER_6:          return "user_6";
        case UI_ACTION_USER_7:          return "user_7";
        case UI_ACTION_USER_8:          return "user_8";
        default:                        return "unknown";
    }
}
