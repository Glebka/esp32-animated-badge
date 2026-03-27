#if defined(ARDUINO_ARCH_ESP32) && !defined(BOARD_HAS_PSRAM)
#error "Please enable PSRAM support"
#endif
#include <Arduino.h>
#include <FreeRTOS.h>
#include <esp_log.h>
#include <FS.h>
#include <bb_spi_lcd.h>

#include "hw_config.hpp"
#include "fs_utils.hpp"
#include "gif_player.hpp"
#include "png_player.hpp"
#include "player_controller.hpp"
#include "battery_status.hpp"
#include "status_bar.hpp"
#include "watchdog.hpp"

BB_SPI_LCD lcd;

char filename_buffer[256];
static EventGroupHandle_t _playerEventGroup = NULL;
static EventBits_t _playerEventBits = 0;
static player_mode_t _currentPlayerMode = PLAYER_MODE_MANUAL;

player_t players[FILE_TYPE_SZ_COUNT] = {
    {gif_player_init, gif_player_open_file, gif_player_render_frame, gif_player_close_file},
    {png_player_init, png_player_open_file, png_player_render_frame, png_player_close_file}
};

static const char *TAG = "MAIN";

static void draw_error_message(const char *message, int yOffset = 0) {
    BB_RECT rect;
    lcd.setFont(FONT_12x16);
    lcd.setCursor(0, 0);
    lcd.getStringBox(message, &rect);
    lcd.setCursor(LCD_WIDTH / 2 - rect.w / 2 - 10, LCD_HEIGHT / 2 - rect.h / 2 + yOffset);
    lcd.setTextColor(TFT_RED);
    lcd.print(message);
}

void setup()
{
  lcd.begin(DISPLAY_WS_AMOLED_18);
  lcd.allocBuffer();
  lcd.fillScreen(TFT_BLACK);
  while (init_fs() != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to initialize filesystem!");
    draw_error_message("SD Card not found", -15);
    draw_error_message("Please insert SD", 15);
    delay(1000);
  }
  
  if (init_player_controller() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize player controller!");
    draw_error_message("Unexpected error", -15);
    draw_error_message("E_PLAYER_CONTROLLER", 15);
    while (1)
    {
    };
  }
  _playerEventGroup = get_player_event_group();
  _currentPlayerMode = get_current_player_mode();

  if (init_status_bar() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize status bar!");
    draw_error_message("Unexpected error", -15);
    draw_error_message("E_STATUS_BAR", 15);
    while (1)
    {
    };
  }

  if (init_battery_status() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize battery status!");
    draw_error_message("Unexpected error", -15);
    draw_error_message("E_BATTERY_STATUS", 15);
    while (1)
    {
    };
  }

  if (gif_player_init(&lcd) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize GIF player!");
    draw_error_message("Unexpected error", -15);
    draw_error_message("E_GIF_PLAYER", 15);
    while (1)
    {
    };
  }
  if(png_player_init(&lcd) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize PNG player!");
    draw_error_message("Unexpected error", -15);
    draw_error_message("E_PNG_PLAYER", 15);
    while (1)
    {
    };
  }

  if (init_watchdog() != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize watchdog!");
    draw_error_message("Unexpected error", -15);
    draw_error_message("E_WATCHDOG", 15);
    while (1)
    {
    };
  }

  lcd.fillScreen(TFT_BLACK);
  status_bar_show();
}

void loop()
{
  watchdog_kick();
  _currentPlayerMode = get_current_player_mode();
  esp_err_t res = player_get_next_file(filename_buffer, sizeof(filename_buffer));
  if (res != ESP_OK)
  {
    ESP_LOGE(TAG, "No supported files found!");
    lcd.fillScreen(TFT_BLACK);
    draw_error_message("No supported files found");
    while (1) {};
  }
  supported_file_type_t file_type = get_file_type(filename_buffer);
  if (file_type >= FILE_TYPE_SZ_COUNT)
  {
    ESP_LOGE(TAG, "No player for file: %s", filename_buffer);
    return;
  }
  if (_currentPlayerMode == PLAYER_MODE_AUTO && file_type == FILE_TYPE_PNG)
  {
    ESP_LOGI(TAG, "Skipping PNG file in AUTO mode: %s", filename_buffer);
    xEventGroupSetBits(_playerEventGroup, PL_FILE_DONE_BIT);
    return;
  }
  bool loop = (_currentPlayerMode == PLAYER_MODE_MANUAL) || file_type == FILE_TYPE_PNG;
  player_t *player = &players[file_type];
  ESP_LOGI(TAG, "Opening file: %s", filename_buffer);

  while (1)
  { 
    lcd.fillScreen(TFT_BLACK);
    if (player->open_file(filename_buffer) == ESP_OK)
    {
      while (player->render_frame(loop) == PLAYER_OK_CONTINUE)
      {
        watchdog_kick();
        _playerEventBits = xEventGroupGetBits(_playerEventGroup);
        if (_playerEventBits & PL_PLAYBACK_MODE_CHANGED_BIT)
        {
          _currentPlayerMode = get_current_player_mode();
          loop = (_currentPlayerMode == PLAYER_MODE_MANUAL) || file_type == FILE_TYPE_PNG;
          xEventGroupClearBits(_playerEventGroup, PL_PLAYBACK_MODE_CHANGED_BIT);
        }
        if (_playerEventBits & PL_PLAYBACK_INT_BIT)
        {
          xEventGroupClearBits(_playerEventGroup, PL_PLAYBACK_INT_BIT);
          player->close_file();
          ESP_LOGI(TAG, "Next file requested, breaking out of frame loop...");
          return;
        }
      }
      ESP_LOGI(TAG, "File finished, closing...");
      player->close_file();
      xEventGroupSetBits(_playerEventGroup, PL_FILE_DONE_BIT);
      if (!loop) {
        ESP_LOGI(TAG, "Not looping, breaking out of file loop...");
        return;
      }
    } else {
      ESP_LOGE(TAG, "Failed to open file: %s", filename_buffer);
      return;
    }
  }
}