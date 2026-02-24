#pragma once

#include <esp_err.h>
#include <FreeRTOS.h>

enum button_event_bits_t {
	BUTTON_EVENT_BIT_PRESSED = (1 << 0),
	BUTTON_EVENT_BIT_RELEASED = (1 << 1),
	BUTTON_EVENT_BIT_LONG_PRESS = (1 << 2),
};

esp_err_t init_button_control();
bool is_button_pressed();
EventGroupHandle_t get_button_event_group();
