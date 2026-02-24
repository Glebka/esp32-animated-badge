#if defined(ARDUINO_ARCH_ESP32) && !defined(BOARD_HAS_PSRAM)
#error "Please enable PSRAM support"
#endif
#ifndef USE_LITTLEFS
#define USE_LITTLEFS 0
#endif
#include <Arduino.h>
#include "FS.h"
#include <bb_spi_lcd.h>

#include "hw_config.hpp"
#include "fs_utils.hpp"
#include "gif_utils.hpp"
#include "btn_control.hpp"
#include "gif_player.hpp"
#include "jpeg_player.hpp"
#include "png_player.hpp"

BB_SPI_LCD lcd;

char filename_buffer[256];

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
  if (init_button_control() != ESP_OK) {
    Serial.println("Failed to initialize button control!");
    while (1)
    {
    };
  }
  lcd.begin(DISPLAY_WS_AMOLED_18);
  lcd.allocBuffer();
  lcd.fillScreen(TFT_BLACK);

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
  esp_err_t res = find_next_supported_file(filename_buffer, sizeof(filename_buffer));
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
  player_t *player = &players[file_type];
  Serial.printf("Opening file: %s\n", filename_buffer);
  while (1)
  { 
    lcd.fillScreen(TFT_BLACK);
    if (player->open_file(filename_buffer) == ESP_OK)
    {
      while (player->render_frame(true) == PLAYER_OK_CONTINUE)
      {
        if (is_button_pressed())
        {
          player->close_file();
          Serial.println("Next file requested, breaking out of frame loop...");
          return;
        }
      }
      Serial.println("File finished, closing...");
      player->close_file();
    } else {
      Serial.printf("Failed to open file: %s\n", filename_buffer);
      return;
    }
  }
}