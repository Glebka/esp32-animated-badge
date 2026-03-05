#pragma once

#include <esp_err.h>
#include "player_controller.hpp"

esp_err_t init_status_bar();
esp_err_t status_bar_update_battery_info(bool is_charging, uint8_t battery_level_percent);
esp_err_t status_bar_update_player_mode(player_mode_t mode);
esp_err_t status_bar_show();
esp_err_t status_bar_draw_callback(void* lcd, int x, int y, int width, uint16_t* pixels);
esp_err_t status_bar_pre_frame_render_callback(void* lcd);