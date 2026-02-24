#pragma once

#include <esp_err.h>
#include <bb_spi_lcd.h>
#include "common.hpp"

esp_err_t gif_player_init(BB_SPI_LCD* lcd);
esp_err_t gif_player_open_file(const char *filename);
player_result_t gif_player_render_frame(bool loop);
esp_err_t gif_player_close_file();
