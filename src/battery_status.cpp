#include "hw_config.hpp"

#include <esp_log.h>

#include <Arduino.h>
#include <Wire.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <Adafruit_XCA9554.h>
#include <XPowersLib.h>
#include "status_bar.hpp"

#include "battery_status.hpp"

static const char *TAG = "BatteryStatus";
static Adafruit_XCA9554 expander;
static XPowersPMU power;
static TaskHandle_t pmu_irq_task_handle = nullptr;
static esp_timer_handle_t battery_poll_timer_handle = nullptr;
static SemaphoreHandle_t pmu_mutex = nullptr;

static esp_err_t update_battery_status_bar(bool show_status_bar) {
    if (pmu_mutex == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(pmu_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to lock PMU mutex for battery update");
        return ESP_ERR_TIMEOUT;
    }

    const bool is_charging = power.isVbusIn();
    const uint8_t battery_percent = power.getBatteryPercent() > 0 ? power.getBatteryPercent() : 0;
    xSemaphoreGive(pmu_mutex);

    status_bar_update_battery_info(is_charging, battery_percent);
    if (show_status_bar) {
        status_bar_show();
    }

    const uint16_t currTable[] = {
        0, 0, 0, 0, 100, 125, 150, 175, 200, 300, 400, 500, 600, 700, 800, 900, 1000
    };
    uint8_t val = power.getChargerConstantCurr();

    ESP_LOGI(TAG, "Battery update: charging=%d, level=%u%%, voltage=%umV", power.isCharging(), battery_percent, power.getBattVoltage());
    return ESP_OK;
}

static void battery_poll_timer_callback(void *arg) {
    (void)arg;
    update_battery_status_bar(false);
}

static void pmu_irq_task(void *param) {
    (void)param;
    static bool show_status_bar_on_event = false;

    for (;;) {
        if (pmu_mutex == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (xSemaphoreTake(pmu_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        uint64_t irq_mask = power.getIrqStatus();
        if (irq_mask != 0) {
            if (power.isVbusInsertIrq()) {
                ESP_LOGI(TAG, "Event: USB inserted");
                show_status_bar_on_event = true;
            }
            if (power.isVbusRemoveIrq()) {
                ESP_LOGI(TAG, "Event: USB removed");
                show_status_bar_on_event = true;
            }
            if (power.isPekeyShortPressIrq()) {
                ESP_LOGI(TAG, "Event: Power button short press");
                show_status_bar_on_event = true;
            }
            power.clearIrqStatus();
        }

        xSemaphoreGive(pmu_mutex);

        if (show_status_bar_on_event) {
            update_battery_status_bar(show_status_bar_on_event);
            show_status_bar_on_event = false;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t init_battery_status() {
    if (pmu_mutex == nullptr) {
        pmu_mutex = xSemaphoreCreateMutex();
        if (pmu_mutex == nullptr) {
            ESP_LOGE(TAG, "Failed to create PMU mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    Wire.begin(IIC_SDA, IIC_SCL);
    if (!expander.begin(0x20)) {  // Replace with actual I2C address if different
        ESP_LOGE(TAG, "Failed to find XCA9554 chip");
        return ESP_FAIL;
    }
    expander.pinMode(5, INPUT); // IRQ pin from PMU
    expander.pinMode(4, INPUT); // SYS_OUT (?)

    if (!power.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        ESP_LOGE(TAG, "Failed to initialize AXP2101 PMU");
        return ESP_FAIL;
    }

    power.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    power.enableInterrupt(XPOWERS_USB_INSERT_INT | XPOWERS_USB_REMOVE_INT | XPOWERS_CHARGE_START_INT | XPOWERS_CHARGE_DONE_INT | XPOWERS_PWR_BTN_CLICK_INT | XPOWERS_PWR_BTN_LONGPRESSED_INT);
    power.setIrqLevelTime(XPOWERS_AXP2101_IRQ_TIME_2S);
    power.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    power.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V36);
    power.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_1500MA);
    power.setSysPowerDownVoltage(2600);
    power.disableTSPinMeasure();
    power.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
    power.setPowerKeyPressOnTime(XPOWERS_POWERON_1S);
    power.clearIrqStatus();

    power.enableBattDetection();
    power.enableVbusVoltageMeasure();
    power.enableBattVoltageMeasure();
    power.enableSystemVoltageMeasure();
    power.disableWatchdog();

    power.setPrechargeCurr(XPOWERS_AXP2101_PRECHARGE_75MA);
    power.setChargerTerminationCurr(XPOWERS_AXP2101_CHG_ITERM_25MA);

    power.setLowBatWarnThreshold(10);
    power.setLowBatShutdownThreshold(5);

    uint8_t charging_current_setting = XPOWERS_AXP2101_CHG_CUR_150MA;
#if defined(HIGHER_CHARGE_CURRENT)
    charging_current_setting = XPOWERS_AXP2101_CHG_CUR_500MA;
#endif
    
    if (!power.setChargerConstantCurr(charging_current_setting)) {
        ESP_LOGE(TAG, "Failed to set charger constant current");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Battery connected: %s", power.isBatteryConnect() ? "Yes" : "No");
    ESP_LOGI(TAG, "Battery charging: %s", power.isCharging() ? "Yes" : "No");
    ESP_LOGI(TAG, "Battery voltage: %.2f V", power.getBattVoltage());
    ESP_LOGI(TAG, "Battery percent: %d %%", power.getBatteryPercent());

    update_battery_status_bar(false);

    if (battery_poll_timer_handle == nullptr) {
        const esp_timer_create_args_t timer_args = {
            .callback = &battery_poll_timer_callback,
            .arg = nullptr,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "battery_poll"
        };

        esp_err_t timer_create_ret = esp_timer_create(&timer_args, &battery_poll_timer_handle);
        if (timer_create_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create battery poll timer: %s", esp_err_to_name(timer_create_ret));
            battery_poll_timer_handle = nullptr;
            return timer_create_ret;
        }

        constexpr uint64_t POLL_INTERVAL_US = 30ULL * 1000ULL * 1000ULL;
        esp_err_t timer_start_ret = esp_timer_start_periodic(battery_poll_timer_handle, POLL_INTERVAL_US);
        if (timer_start_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start battery poll timer: %s", esp_err_to_name(timer_start_ret));
            esp_timer_delete(battery_poll_timer_handle);
            battery_poll_timer_handle = nullptr;
            return timer_start_ret;
        }

        ESP_LOGI(TAG, "Battery poll timer started (every 30 seconds)");
    }

    if (pmu_irq_task_handle == nullptr) {
        BaseType_t task_ok = xTaskCreate(
            pmu_irq_task,
            "pmu_irq_task",
            4096,
            nullptr,
            1,
            &pmu_irq_task_handle
        );
        if (task_ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to start PMU IRQ task");
            pmu_irq_task_handle = nullptr;
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "PMU IRQ task started");
    }

    return ESP_OK;
}