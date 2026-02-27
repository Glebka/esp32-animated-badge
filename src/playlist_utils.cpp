#include <FS.h>
#include "playlist_utils.hpp"
#include "fs_utils.hpp"


static File _playlistFile;

bool has_playlist_file() {
    fs::FS &filesystem = get_media_fs();
    File file = filesystem.open("/playlist.txt");
    bool exists = file && !file.isDirectory();
    file.close();
    return exists;
}

esp_err_t open_playlist_file() {
    if (_playlistFile) {
        return ESP_ERR_INVALID_STATE;
    }
    fs::FS &filesystem = get_media_fs();
    _playlistFile = filesystem.open("/playlist.txt");
    if (!_playlistFile || _playlistFile.isDirectory()) {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t close_playlist_file() {
    if (!_playlistFile) {
        return ESP_ERR_INVALID_STATE;
    }
    _playlistFile.close();
    return ESP_OK;
}

esp_err_t reset_playlist_file() {
    if (!_playlistFile) {
        return ESP_ERR_INVALID_STATE;
    }
    _playlistFile.seek(0);
    return ESP_OK;
}

playlist_result_t get_next_playlist_entry(char *filename_buffer, size_t buffer_size, uint16_t *delay_s) {
    if (!_playlistFile) {
        return PLAYLIST_RESULT_ERROR;
    }
    if (filename_buffer == NULL || buffer_size == 0 || delay_s == NULL) {
        return PLAYLIST_RESULT_ERROR;
    }

    String line = _playlistFile.readStringUntil('\n');
    bool eof = _playlistFile.position() >= _playlistFile.size();

    if (line.length() == 0) {
        return PLAYLIST_RESULT_ERROR;
    }

    int separator_index = line.indexOf(' ');
    if (separator_index == -1) {
        line.trim();
        if (line.length() >= buffer_size) {
            return PLAYLIST_RESULT_ERROR;
        }
        line.toCharArray(filename_buffer, buffer_size);
        if (delay_s != NULL) {
            *delay_s = 0;
        }
        return eof ? PLAYLIST_OK_EOF : PLAYLIST_OK_CONTINUE;
    }

    String filename_part = line.substring(0, separator_index);
    String delay_part = line.substring(separator_index + 1);

    filename_part.trim();
    delay_part.trim();

    if (filename_part.length() >= buffer_size) {
        return PLAYLIST_RESULT_ERROR;
    }

    filename_part.toCharArray(filename_buffer, buffer_size);
    if (delay_s != NULL) {
        *delay_s = (uint16_t)delay_part.toInt();
    }

    return eof ? PLAYLIST_OK_EOF : PLAYLIST_OK_CONTINUE;
}