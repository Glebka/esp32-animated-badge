#pragma once

#include <esp_err.h>
#include <FreeRTOS.h>

enum player_mode_t {
    PLAYER_MODE_MANUAL,
    PLAYER_MODE_AUTO,
    PLAYER_MODE_PLAYLIST,
    PLAYER_MODE_SZ_COUNT,
};

enum player_event_bits_t {
	PL_BTN_PRESSED_BIT = (1 << 0),
	PL_BTN_RELEASED_BIT = (1 << 1),
	PL_BTN_LONG_PRESS_BIT = (1 << 2),
    PL_BTN_CLICK_BIT = (1 << 3),
    PL_FRAME_DONE_BIT = (1 << 4),
    PL_FILE_DONE_BIT = (1 << 5),
    PL_PLAYBACK_INT_BIT = (1 << 6),
    PL_PLAYBACK_MODE_CHANGED_BIT = (1 << 7),
};

esp_err_t init_player_controller();
EventGroupHandle_t get_player_event_group();
player_mode_t get_current_player_mode();
