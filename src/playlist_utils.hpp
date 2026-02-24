#pragma once

#include <esp_err.h>

enum playlist_result_t {
    PLAYLIST_OK_CONTINUE,
    PLAYLIST_OK_EOF,
    PLAYLIST_RESULT_ERROR
};

bool has_playlist_file();
esp_err_t open_playlist_file();
esp_err_t close_playlist_file();
esp_err_t reset_playlist_file();
playlist_result_t get_next_playlist_entry(char *filename_buffer, size_t buffer_size, uint16_t *delay_s);