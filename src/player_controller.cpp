#include <Arduino.h>
#include <FreeRTOS.h>
#include <esp_timer.h>

#include "player_controller.hpp"
#include "playlist_utils.hpp"
#include "fs_utils.hpp"
#include "hw_config.hpp"
#include "status_bar.hpp"

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
static esp_timer_handle_t _playlistDelayTimer = nullptr;

static void PlaylistDelayTimerCallback(void *arg)
{
    if (_playerEventGroup != NULL)
    {
        xEventGroupSetBits(_playerEventGroup, PL_PLAYBACK_INT_BIT);
    }
}

static void stop_playlist_delay_timer()
{
    if (_playlistDelayTimer != nullptr)
    {
        esp_timer_stop(_playlistDelayTimer);
    }
}

static void start_playlist_delay_timer(uint16_t delay_s)
{
    if (_playlistDelayTimer == nullptr)
    {
        return;
    }

    esp_timer_stop(_playlistDelayTimer);

    if (delay_s == 0)
    {
        return;
    }

    const uint64_t delay_us = static_cast<uint64_t>(delay_s) * 1000000ULL;
    esp_timer_start_once(_playlistDelayTimer, delay_us);
}

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
            stop_playlist_delay_timer();
            _currentMode = (player_mode_t)((_currentMode + 1) % PLAYER_MODE_SZ_COUNT);
            if (_currentMode == PLAYER_MODE_PLAYLIST)
            {
                if (has_playlist_file() && open_playlist_file() == ESP_OK)
                {
                    xEventGroupSetBits(_playerEventGroup, PL_PLAYBACK_MODE_CHANGED_BIT | PL_PLAYBACK_INT_BIT);
                }
                else
                {
                    _currentMode = PLAYER_MODE_MANUAL;
                }
            }
            else
            {
                close_playlist_file();
                xEventGroupSetBits(_playerEventGroup, PL_PLAYBACK_MODE_CHANGED_BIT);
            }
            reset_next_supported_file_iterator();
            status_bar_update_player_mode(_currentMode);
            status_bar_show();
        }
        if (eventBits & PL_BTN_CLICK_BIT)
        {
            stop_playlist_delay_timer();
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

    esp_timer_create_args_t timer_args = {
        .callback = &PlaylistDelayTimerCallback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "playlist_delay"};

    if (esp_timer_create(&timer_args, &_playlistDelayTimer) != ESP_OK)
    {
        return ESP_FAIL;
    }

    BaseType_t result = xTaskCreatePinnedToCore(
        PlayerControllerTask,
        "PlayerControllerTask",
        4096,
        NULL,
        tskIDLE_PRIORITY + 1,
        &_playerControllerTaskHandle,
        1);

    if (result != pdPASS)
    {
        return ESP_FAIL;
    }

    result = xTaskCreatePinnedToCore(
        MonitorButtonTask,
        "MonitorButtonTask",
        2048,
        NULL,
        1,
        &MonitorButtonTaskHandle,
        1);

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

esp_err_t player_get_next_file(char *out_path, size_t out_path_size)
{
    static uint16_t playlist_delay_s = 0;
    stop_playlist_delay_timer();
    if (_currentMode == PLAYER_MODE_PLAYLIST)
    {
        playlist_result_t res = get_next_playlist_entry(out_path, out_path_size, &playlist_delay_s);
        if (res != PLAYLIST_RESULT_ERROR && playlist_delay_s > 0)
        {
            start_playlist_delay_timer(playlist_delay_s);
        }
        if (res == PLAYLIST_OK_CONTINUE)
        {
            return ESP_OK;
        }
        else if (res == PLAYLIST_OK_EOF)
        {
            reset_playlist_file();
            return ESP_OK;
        }
        else
        {
            return ESP_FAIL;
        }
    }
    else
    {
        return find_next_supported_file(out_path, out_path_size);
    }
}