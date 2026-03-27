#include "watchdog.hpp"

#include <Arduino.h>
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "WATCHDOG";
static TaskHandle_t _watchdogTaskHandle = nullptr;
static portMUX_TYPE _watchdogMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t _lastKickMs = 0;
static constexpr uint32_t WATCHDOG_TIMEOUT_MS = 3000;
static constexpr TickType_t WATCHDOG_CHECK_PERIOD_TICKS = pdMS_TO_TICKS(500);

static void WatchdogTask(void *pvParameters)
{
    (void)pvParameters;

    while (true)
    {
        uint32_t lastKickMs = 0;
        portENTER_CRITICAL(&_watchdogMux);
        lastKickMs = _lastKickMs;
        portEXIT_CRITICAL(&_watchdogMux);

        const uint32_t nowMs = millis();
        if ((nowMs - lastKickMs) > WATCHDOG_TIMEOUT_MS)
        {
            ESP_LOGE(TAG, "Watchdog timeout (%lu ms), restarting...", (unsigned long)(nowMs - lastKickMs));
            delay(50);
            esp_restart();
        }

        vTaskDelay(WATCHDOG_CHECK_PERIOD_TICKS);
    }
}

esp_err_t init_watchdog()
{
    if (_watchdogTaskHandle != nullptr)
    {
        return ESP_OK;
    }

    watchdog_kick();

    BaseType_t result = xTaskCreatePinnedToCore(
        WatchdogTask,
        "WatchdogTask",
        2048,
        nullptr,
        tskIDLE_PRIORITY + 1,
        &_watchdogTaskHandle,
        1);

    if (result != pdPASS)
    {
        _watchdogTaskHandle = nullptr;
        return ESP_FAIL;
    }

    return ESP_OK;
}

void watchdog_kick()
{
    portENTER_CRITICAL(&_watchdogMux);
    _lastKickMs = millis();
    portEXIT_CRITICAL(&_watchdogMux);
}
