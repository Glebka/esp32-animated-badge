#if defined(ARDUINO_ARCH_ESP32) && !defined(BOARD_HAS_PSRAM)
#error "Please enable PSRAM support"
#endif
#ifndef USE_LITTLEFS
#define USE_LITTLEFS 0
#endif
#include <Arduino.h>
#include "FS.h"
#include <bb_spi_lcd.h>
#include <FreeRTOS.h>

#include "hw_config.hpp"
#include "fs_utils.hpp"
#include "gif_utils.hpp"
//#include "btn_control.hpp"
#include "gif_player.hpp"
#include "jpeg_player.hpp"
#include "png_player.hpp"
#include "player_controller.hpp"

BB_SPI_LCD lcd;

char filename_buffer[256];
static EventGroupHandle_t _playerEventGroup = NULL;
static EventBits_t _playerEventBits = 0;
static player_mode_t _currentPlayerMode = PLAYER_MODE_MANUAL;

player_t players[FILE_TYPE_SZ_COUNT] = {
    {gif_player_init, gif_player_open_file, gif_player_render_frame, gif_player_close_file},
    // {jpeg_player_init, jpeg_player_open_file, jpeg_player_render_frame, jpeg_player_close_file},
    {png_player_init, png_player_open_file, png_player_render_frame, png_player_close_file}
};

void setup()
{
  Serial.begin(115200);
  delay(3000);

  if (init_fs() != ESP_OK) {
    Serial.println("Failed to initialize filesystem!");
    while (1)
    {
    };
  }
  // if (init_button_control() != ESP_OK) {
  //   Serial.println("Failed to initialize button control!");
  //   while (1)
  //   {
  //   };
  // }
  if (init_player_controller() != ESP_OK) {
    Serial.println("Failed to initialize player controller!");
    while (1)
    {
    };
  }
  _playerEventGroup = get_player_event_group();
  _currentPlayerMode = get_current_player_mode();
  lcd.begin(DISPLAY_WS_AMOLED_18);
  lcd.allocBuffer();
  lcd.fillScreen(TFT_BLACK);
  //lcd.setBrightness(128);

  if (gif_player_init(&lcd) != ESP_OK) {
    Serial.println("Failed to initialize GIF player!");
    while (1)
    {
    };
  }
  // if (jpeg_player_init(&lcd) != ESP_OK) {
  //   Serial.println("Failed to initialize JPEG player!");
  //   while (1)
  //   {
  //   };
  // }
  if(png_player_init(&lcd) != ESP_OK) {
    Serial.println("Failed to initialize PNG player!");
    while (1)
    {
    };
  }
}

void loop()
{
  _currentPlayerMode = get_current_player_mode();
  esp_err_t res = player_get_next_file(filename_buffer, sizeof(filename_buffer));
  if (res != ESP_OK)
  {
    Serial.println("No supported files found!");
    while (1) {};
  }
  supported_file_type_t file_type = get_file_type(filename_buffer);
  if (file_type >= FILE_TYPE_SZ_COUNT)
  {
    Serial.printf("No player for file: %s\n", filename_buffer);
    return;
  }
  if (_currentPlayerMode == PLAYER_MODE_AUTO && file_type == FILE_TYPE_PNG)
  {
    Serial.printf("Skipping PNG file in AUTO mode: %s\n", filename_buffer);
    xEventGroupSetBits(_playerEventGroup, PL_FILE_DONE_BIT);
    return;
  }
  bool loop = (_currentPlayerMode == PLAYER_MODE_MANUAL) || file_type == FILE_TYPE_PNG;
  player_t *player = &players[file_type];
  Serial.printf("Opening file: %s\n", filename_buffer);
  while (1)
  { 
    lcd.fillScreen(TFT_BLACK);
    if (player->open_file(filename_buffer) == ESP_OK)
    {
      while (player->render_frame(loop) == PLAYER_OK_CONTINUE)
      {
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
          Serial.println("Next file requested, breaking out of frame loop...");
          return;
        }
      }
      Serial.println("File finished, closing...");
      player->close_file();
      xEventGroupSetBits(_playerEventGroup, PL_FILE_DONE_BIT);
      if (!loop) {
        Serial.println("Not looping, breaking out of file loop...");
        return;
      }
    } else {
      Serial.printf("Failed to open file: %s\n", filename_buffer);
      return;
    }
  }
}