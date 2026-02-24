#include <Arduino.h>
#include <FreeRTOS.h>
#include "player_controller.hpp"
#include "hw_config.hpp"

static player_mode_t _currentMode = PLAYER_MODE_MANUAL;
TaskHandle_t _playerControllerTaskHandle = NULL;
EventGroupHandle_t _playerEventGroup = NULL;

static TaskHandle_t MonitorButtonTaskHandle = NULL;
static constexpr unsigned long kButtonDebounceMs = 40;
static constexpr unsigned long kButtonLongPressMs = 1000;
static bool buttonState = HIGH;
static bool lastButtonReading = HIGH;
static unsigned long lastButtonChangeMs = 0;
static volatile bool buttonPressed = false;

static void MonitorButtonTask(void *pvParameters)
{
    unsigned long pressStartMs = 0;
    bool longPressReported = false;
    EventBits_t bitsMask = 0;

    while (true)
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
                    buttonPressed = true;
                    pressStartMs = millis();
                    longPressReported = false;
                    xEventGroupSetBits(_playerEventGroup, PL_BTN_PRESSED_BIT);
                }
                else
                {
                    if (!longPressReported)
                    {
                        bitsMask |= PL_BTN_CLICK_BIT;
                    }
                    pressStartMs = 0;
                    longPressReported = false;
                    xEventGroupSetBits(_playerEventGroup, bitsMask | PL_BTN_RELEASED_BIT);
                    bitsMask = 0;
                }
            }
        }

        if (buttonState == LOW && !longPressReported && pressStartMs > 0 &&
            (millis() - pressStartMs) >= kButtonLongPressMs)
        {
            longPressReported = true;
            xEventGroupSetBits(_playerEventGroup, PL_BTN_LONG_PRESS_BIT);
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

static void PlayerControllerTask(void *pvParameters)
{
    EventBits_t eventBits = 0;
    EventBits_t bitsToWaitFor = PL_BTN_LONG_PRESS_BIT | PL_BTN_CLICK_BIT | PL_FRAME_DONE_BIT | PL_FILE_DONE_BIT;
    while (true)
    {
        eventBits = xEventGroupWaitBits(
            _playerEventGroup,
            bitsToWaitFor,
            pdTRUE,  // Clear bits on exit
            pdFALSE, // Wait for any bit
            portMAX_DELAY);
        if (eventBits & PL_BTN_LONG_PRESS_BIT)
        {
            _currentMode = (player_mode_t)((_currentMode + 1) % PLAYER_MODE_SZ_COUNT);
            if (_currentMode == PLAYER_MODE_PLAYLIST)
            {
                xEventGroupSetBits(_playerEventGroup, PL_PLAYBACK_MODE_CHANGED_BIT | PL_PLAYBACK_INT_BIT);
            } else {
                xEventGroupSetBits(_playerEventGroup, PL_PLAYBACK_MODE_CHANGED_BIT);
            }
        }
        if (eventBits & PL_BTN_CLICK_BIT)
        {
            xEventGroupSetBits(_playerEventGroup, PL_PLAYBACK_INT_BIT);
        }
    }
}

esp_err_t init_player_controller()
{
    pinMode(FN_BUTTON_PIN, INPUT_PULLUP);
    buttonState = digitalRead(FN_BUTTON_PIN);
    lastButtonReading = buttonState;
    lastButtonChangeMs = millis();

    _playerEventGroup = xEventGroupCreate();
    if (_playerEventGroup == NULL)
    {
        return ESP_FAIL;
    }

    BaseType_t result = xTaskCreatePinnedToCore(
        PlayerControllerTask,         // Task function
        "PlayerControllerTask",       // Name of the task
        4096,                         // Stack size in words
        NULL,                         // Task input parameter
        tskIDLE_PRIORITY + 1,         // Priority of the task
        &_playerControllerTaskHandle, // Task handle
        0                             // Core to run the task on
    );

    if (result != pdPASS)
    {
        return ESP_FAIL;
    }

    result = xTaskCreatePinnedToCore(
        MonitorButtonTask,        /* Function to implement the task */
        "MonitorButtonTask",      /* Name of the task */
        2048,                     /* Stack size in words (around 8KB in bytes for ESP32S3) */
        NULL,                     /* Task input parameter */
        1,                        /* Priority of the task */
        &MonitorButtonTaskHandle, /* Task handle. */
        0);                       /* Core where the task should run */

    if (result != pdPASS)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

EventGroupHandle_t get_player_event_group()
{
    return _playerEventGroup;
}

player_mode_t get_current_player_mode()
{
    return _currentMode;
}