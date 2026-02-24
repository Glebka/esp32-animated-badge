#include <Arduino.h>
#include "btn_control.hpp"
#include "hw_config.hpp"

static constexpr unsigned long kButtonDebounceMs = 40;
static bool buttonState = HIGH;
static bool lastButtonReading = HIGH;
static unsigned long lastButtonChangeMs = 0;
static volatile bool buttonPressed = false;
static TaskHandle_t MonitorButtonTaskHandle = NULL;

static bool wasButtonClicked()
{
  int reading = digitalRead(FN_BUTTON_PIN);
  if (reading != lastButtonReading)
  {
    lastButtonChangeMs = millis();
    lastButtonReading = reading;
  }
  if ((millis() - lastButtonChangeMs) > kButtonDebounceMs)
  {
    if (reading != buttonState)
    {
      buttonState = reading;
      if (buttonState == LOW)
      {
        return true;
      }
    }
  }
  return false;
}

static void MonitorButtonTask(void *pvParameters)
{
  for (;;)
  { 
    if (wasButtonClicked())
    {
      if (!buttonPressed)
      {
        buttonPressed = true;
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

esp_err_t init_button_control()
{
  pinMode(FN_BUTTON_PIN, INPUT_PULLUP);
  BaseType_t result = xTaskCreatePinnedToCore(
      MonitorButtonTask,    /* Function to implement the task */
      "MonitorButtonTask", /* Name of the task */
      2048,                /* Stack size in words (around 8KB in bytes for ESP32S3) */
      NULL,                /* Task input parameter */
      1,                   /* Priority of the task */
      &MonitorButtonTaskHandle, /* Task handle. */
      0);                  /* Core where the task should run */

  if (result != pdPASS)
  {
    return ESP_FAIL;
  }
  return ESP_OK;
}

bool is_button_pressed()
{
  if (buttonPressed)
  {
    buttonPressed = false;
    return true;
  }
  return false;
}