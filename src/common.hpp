#pragma once

#include <esp_err.h>
#include <bb_spi_lcd.h>

enum player_result_t {
    PLAYER_OK_EOF = 0,
    PLAYER_OK_CONTINUE = 1,
    PLAYER_ERROR = -1
};

typedef esp_err_t (*player_init_func_t)(BB_SPI_LCD* lcd);
typedef esp_err_t (*player_open_func_t)(const char *filename);
typedef player_result_t (*player_render_func_t)(bool loop);
typedef esp_err_t (*player_close_func_t)();

struct player_t {
    player_init_func_t init;
    player_open_func_t open_file;
    player_render_func_t render_frame;
    player_close_func_t close_file;
};

enum supported_file_type_t {
    FILE_TYPE_GIF,
    FILE_TYPE_PNG,
    FILE_TYPE_SZ_COUNT
};