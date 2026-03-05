#pragma once

#include <esp_err.h>
#include "common.hpp"
#include <FS.h>

esp_err_t init_fs();
esp_err_t find_next_supported_file(char *out_path, size_t out_path_size);
esp_err_t reset_next_supported_file_iterator();
bool has_supported_extension(const char *filename);
supported_file_type_t get_file_type(const char *filename);
fs::FS &get_media_fs();
