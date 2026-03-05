#include "hw_config.hpp"

#include <Arduino.h>
#include <PNGdec.h>
#include <bb_spi_lcd.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "player_controller.hpp"
#include "status_bar.hpp"

struct status_bar_t {
    bool isVisible;
    bool isCharging;
    uint8_t batteryLevelPercent;
    player_mode_t currentPlayerMode;
};

static PNG _pngDec;
const int OV_WIDTH = 96;
const int OV_HEIGHT = 40;
const int OV_X = 235, OV_Y = 8;
static uint8_t *_pBatIconBuf = NULL;
static constexpr int STATUS_BAR_HEIGHT = OV_HEIGHT + OV_Y * 2;
static constexpr uint32_t STATUS_BAR_TIMEOUT_MS = 5000;
static constexpr int CHARGE_LEVEL_0_X = 22;
static constexpr int CHARGE_LEVEL_100_X = 73;
static constexpr int CHARGE_LEVEL_Y = 7;
static constexpr int CHARGE_LEVEL_HEIGHT = 25;
static constexpr int CHARGE_ICON_X = OV_X + OV_WIDTH;
static constexpr int CHARGE_ICON_Y = OV_Y + 3;
static constexpr int CHARGE_ICON_W = 30;
static constexpr int CHARGE_ICON_H = OV_HEIGHT;
static constexpr uint16_t ICON_COLOR_KEY = 0x0;
static constexpr int MODE_ICON_X = 20;
static constexpr int MODE_ICON_Y = OV_Y + 5;


static BB_SPI_LCD _statusBarSprite;
static uint8_t *_pStatusBarSpriteBuffer = NULL;
static BB_SPI_LCD _restoreBarSprite;
static uint8_t *_pRestoreBarSpriteBuffer = NULL;
static SemaphoreHandle_t _statusBarSpriteMutex = NULL;
static bool _statusBarVisible = false;
static uint32_t _statusBarShownAtMs = 0;
static bool _statusBarNeedsFirstRender = true;

const char *TAG = "STATUS_BAR";

extern "C"
{
    extern const uint8_t _binary_assets_icons_battery_png_start[] asm("_binary_assets_icons_battery_white_png_start");
    extern const uint8_t _binary_assets_icons_battery_png_end[] asm("_binary_assets_icons_battery_white_png_end");
    extern const uint8_t _binary_assets_icons_charging_icon_bmp_start[] asm("_binary_assets_icons_charging_icon_bmp_start");
    extern const uint8_t _binary_assets_icons_charging_icon_bmp_end[] asm("_binary_assets_icons_charging_icon_bmp_end");
    extern const uint8_t _binary_assets_icons_manual_bmp_start[] asm("_binary_assets_icons_manual_bmp_start");
    extern const uint8_t _binary_assets_icons_manual_bmp_end[] asm("_binary_assets_icons_manual_bmp_end");
    extern const uint8_t _binary_assets_icons_auto_bmp_start[] asm("_binary_assets_icons_auto_bmp_start");
    extern const uint8_t _binary_assets_icons_auto_bmp_end[] asm("_binary_assets_icons_auto_bmp_end");
    extern const uint8_t _binary_assets_icons_playlist_bmp_start[] asm("_binary_assets_icons_playlist_bmp_start");
    extern const uint8_t _binary_assets_icons_playlist_bmp_end[] asm("_binary_assets_icons_playlist_bmp_end");
}

static const uint8_t *_pBatteryPng = _binary_assets_icons_battery_png_start;
static const size_t _batteryPngSize = (size_t)(_binary_assets_icons_battery_png_end - _binary_assets_icons_battery_png_start);
static const uint8_t *_pChargingBmp = _binary_assets_icons_charging_icon_bmp_start;
static const size_t _chargingBmpSize = (size_t)(_binary_assets_icons_charging_icon_bmp_end - _binary_assets_icons_charging_icon_bmp_start);
static const uint8_t *_pManualBmp = _binary_assets_icons_manual_bmp_start;
static const size_t _manualBmpSize = (size_t)(_binary_assets_icons_manual_bmp_end - _binary_assets_icons_manual_bmp_start);
static const uint8_t *_pAutoBmp = _binary_assets_icons_auto_bmp_start;
static const size_t _autoBmpSize = (size_t)(_binary_assets_icons_auto_bmp_end - _binary_assets_icons_auto_bmp_start);
static const uint8_t *_pPlaylistBmp = _binary_assets_icons_playlist_bmp_start;
static const size_t _playlistBmpSize = (size_t)(_binary_assets_icons_playlist_bmp_end - _binary_assets_icons_playlist_bmp_start);

static status_bar_t statusBar;

static inline bool lock_status_bar_sprite()
{
    return (_statusBarSpriteMutex == NULL) || (xSemaphoreTake(_statusBarSpriteMutex, portMAX_DELAY) == pdTRUE);
}

static inline void unlock_status_bar_sprite()
{
    if (_statusBarSpriteMutex)
    {
        xSemaphoreGive(_statusBarSpriteMutex);
    }
}

static esp_err_t redraw_status_bar_widget()
{
    if (!lock_status_bar_sprite())
    {
        return ESP_FAIL;
    }

    constexpr uint16_t barColor565 = 0;//0x8410;
    constexpr uint16_t chargeFillColor565 = 0xFFFF;

    _statusBarSprite.fillRect(0, 0, LCD_WIDTH, STATUS_BAR_HEIGHT, barColor565, DRAW_TO_RAM);

    constexpr uint8_t barR5 = (uint8_t)(barColor565 >> 11);
    constexpr uint8_t barG6 = (uint8_t)((barColor565 >> 5) & 0x3F);
    constexpr uint8_t barB5 = (uint8_t)(barColor565 & 0x1F);

    for (int y = 0; y < OV_HEIGHT; ++y)
    {
        const int dstY = OV_Y + y;
        if (dstY < 0 || dstY >= STATUS_BAR_HEIGHT)
        {
            continue;
        }

        for (int x = 0; x < OV_WIDTH; ++x)
        {
            const int dstX = OV_X + x;
            if (dstX < 0 || dstX >= LCD_WIDTH)
            {
                continue;
            }

            const uint8_t *ovPx = &_pBatIconBuf[(y * OV_WIDTH + x) * 4];
            const uint8_t ovR = ovPx[0];
            const uint8_t ovG = ovPx[1];
            const uint8_t ovB = ovPx[2];
            const uint8_t ovA = ovPx[3];

            if (ovA == 0)
            {
                continue;
            }

            uint8_t outR5;
            uint8_t outG6;
            uint8_t outB5;

            if (ovA == 255)
            {
                outR5 = ovR >> 3;
                outG6 = ovG >> 2;
                outB5 = ovB >> 3;
            }
            else
            {
                const uint8_t ovR5 = ovR >> 3;
                const uint8_t ovG6 = ovG >> 2;
                const uint8_t ovB5 = ovB >> 3;
                const uint16_t invA = 255 - ovA;

                outR5 = (uint8_t)((barR5 * invA + ovR5 * ovA + 127) / 255);
                outG6 = (uint8_t)((barG6 * invA + ovG6 * ovA + 127) / 255);
                outB5 = (uint8_t)((barB5 * invA + ovB5 * ovA + 127) / 255);
            }

            const uint16_t out565 = (uint16_t)((outR5 << 11) | (outG6 << 5) | outB5);
            _statusBarSprite.drawPixel(dstX, dstY, out565, DRAW_TO_RAM);
        }
    }

    const int maxChargeWidth = CHARGE_LEVEL_100_X - CHARGE_LEVEL_0_X;
    const uint8_t clampedBatteryLevel =
        (statusBar.batteryLevelPercent > 100) ? 100 : statusBar.batteryLevelPercent;

    int chargeWidth = (maxChargeWidth * clampedBatteryLevel) / 100;
    if (clampedBatteryLevel > 0 && chargeWidth == 0)
    {
        chargeWidth = 1;
    }

    if (chargeWidth > 0)
    {
        _statusBarSprite.fillRect(
            OV_X + CHARGE_LEVEL_0_X,
            OV_Y + CHARGE_LEVEL_Y,
            chargeWidth,
            CHARGE_LEVEL_HEIGHT,
            chargeFillColor565,
            DRAW_TO_RAM);
    }
    if (statusBar.isCharging)
    {
        _statusBarSprite.drawBMP(_pChargingBmp,
            CHARGE_ICON_X,
            CHARGE_ICON_Y,
            0,
            ICON_COLOR_KEY,
            DRAW_TO_RAM);
    }
    switch (statusBar.currentPlayerMode)
    {
    case PLAYER_MODE_MANUAL:
        _statusBarSprite.drawBMP(_pManualBmp,
            MODE_ICON_X,
            MODE_ICON_Y,
            0,
            ICON_COLOR_KEY,
            DRAW_TO_RAM);
        break;
    case PLAYER_MODE_AUTO:
        _statusBarSprite.drawBMP(_pAutoBmp,
            MODE_ICON_X,
            MODE_ICON_Y,
            0,
            ICON_COLOR_KEY,
            DRAW_TO_RAM);
        break;
    case PLAYER_MODE_PLAYLIST:
        _statusBarSprite.drawBMP(_pPlaylistBmp,
            MODE_ICON_X,
            MODE_ICON_Y,
            0,
            ICON_COLOR_KEY,
            DRAW_TO_RAM);
        break;
    default:
        break;
    }

    unlock_status_bar_sprite();
    return ESP_OK;
}

esp_err_t status_bar_draw_callback(void* lcd, int x, int y, int width, uint16_t* pixels) {

    if ((_pStatusBarSpriteBuffer || _pRestoreBarSpriteBuffer) && y >= 0 && y < STATUS_BAR_HEIGHT)
    {
        const int lineStart = x;
        const int lineEnd = x + width;
        const int copyStart = (lineStart > 0) ? lineStart : 0;
        const int copyEnd = (lineEnd < LCD_WIDTH) ? lineEnd : LCD_WIDTH;

        if (copyStart < copyEnd)
        {
            const int dstOffsetBytes = (copyStart - lineStart) * 2;
            const int srcOffsetBytes = (y * LCD_WIDTH + copyStart) * 2;
            const int copyBytes = (copyEnd - copyStart) * 2;

            if (_pRestoreBarSpriteBuffer)
            {
                memcpy(&_pRestoreBarSpriteBuffer[srcOffsetBytes], &pixels[dstOffsetBytes], copyBytes);
            }

            if (_statusBarVisible && _pStatusBarSpriteBuffer)
            {
                if (!lock_status_bar_sprite())
                {
                    return ESP_FAIL;
                }
                memcpy(&pixels[dstOffsetBytes], &_pStatusBarSpriteBuffer[srcOffsetBytes], copyBytes);
                unlock_status_bar_sprite();
            }
        }
    }
    return ESP_OK;
}

esp_err_t status_bar_pre_frame_render_callback(void* lcd) {

    if (_statusBarVisible && _statusBarNeedsFirstRender)
    {
        if (!lock_status_bar_sprite())
        {
            return ESP_FAIL;
        }
        ((BB_SPI_LCD *)lcd)->waitDMA();
        ((BB_SPI_LCD *)lcd)->drawSprite(0, 0, &_statusBarSprite, 1.0f, 0xffffffff, DRAW_TO_LCD | DRAW_WITH_DMA);
        ((BB_SPI_LCD *)lcd)->waitDMA();
        unlock_status_bar_sprite();
        _statusBarNeedsFirstRender = false;
    }
    if (_statusBarVisible && (uint32_t)(millis() - _statusBarShownAtMs) >= STATUS_BAR_TIMEOUT_MS)
    {
        if (_pRestoreBarSpriteBuffer)
        {
            ((BB_SPI_LCD *)lcd)->waitDMA();
            ((BB_SPI_LCD *)lcd)->drawSprite(0, 0, &_restoreBarSprite, 1.0f, 0xffffffff, DRAW_TO_LCD | DRAW_WITH_DMA);
            ((BB_SPI_LCD *)lcd)->waitDMA();
        }
        _statusBarVisible = false;
        _statusBarNeedsFirstRender = false;
    }
    return ESP_OK;
}

static esp_err_t load_battery_icon()
{
    _pBatIconBuf = (uint8_t *)heap_caps_malloc(OV_WIDTH * OV_HEIGHT * 4, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (!_pBatIconBuf)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for battery icon");
        return ESP_ERR_NO_MEM;
    }
    if (_pngDec.openRAM((uint8_t *)_pBatteryPng, _batteryPngSize, NULL) != 0)
    {
        ESP_LOGE(TAG, "Failed to open battery PNG");
        return ESP_FAIL;
    }
    _pngDec.setBuffer(_pBatIconBuf);
    if (_pngDec.decode(NULL, 0) != 0)
    {
        ESP_LOGE(TAG, "Failed to decode battery PNG");
        ESP_LOGE(TAG, "PNG error code: %d", _pngDec.getLastError());
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t init_sprites()
{
    if (_statusBarSpriteMutex == NULL)
    {
        _statusBarSpriteMutex = xSemaphoreCreateMutex();
        if (_statusBarSpriteMutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create status bar sprite mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    if (!lock_status_bar_sprite())
    {
        ESP_LOGE(TAG, "Failed to lock status bar sprite mutex");
        return ESP_FAIL;
    }

    if (_statusBarSprite.createVirtual(LCD_WIDTH, STATUS_BAR_HEIGHT, nullptr, true) == 0)
    {
        ESP_LOGE(TAG, "Failed to create status bar sprite");
        unlock_status_bar_sprite();
        return ESP_ERR_NO_MEM;
    }

    if (_restoreBarSprite.createVirtual(LCD_WIDTH, STATUS_BAR_HEIGHT, nullptr, true) == 0)
    {
        ESP_LOGE(TAG, "Failed to create restore bar sprite");
        unlock_status_bar_sprite();
        return ESP_ERR_NO_MEM;
    }
    _pStatusBarSpriteBuffer = (uint8_t *)_statusBarSprite.getBuffer();
    _pRestoreBarSpriteBuffer = (uint8_t *)_restoreBarSprite.getBuffer();
    if (!_pStatusBarSpriteBuffer || !_pRestoreBarSpriteBuffer)
    {
        ESP_LOGE(TAG, "Failed to get sprite buffers");
        unlock_status_bar_sprite();
        return ESP_FAIL;
    }
    memset(_pRestoreBarSpriteBuffer, 0, (size_t)LCD_WIDTH * (size_t)STATUS_BAR_HEIGHT * 2);
    //redraw_status_bar_widget();

    unlock_status_bar_sprite();
    return ESP_OK;
}

esp_err_t init_status_bar() {
    // Initialize status bar
    statusBar.isVisible = false;
    statusBar.isCharging = true;
    statusBar.batteryLevelPercent = 100;
    statusBar.currentPlayerMode = PLAYER_MODE_MANUAL;

    if (init_sprites() != ESP_OK)
    {
        return ESP_FAIL;
    }
    if (load_battery_icon() != ESP_OK)
    {
        return ESP_FAIL;
    }
    if (redraw_status_bar_widget() != ESP_OK)
    {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t status_bar_update_battery_info(bool is_charging, uint8_t battery_level_percent) {
    statusBar.isCharging = is_charging;
    statusBar.batteryLevelPercent = battery_level_percent;
    redraw_status_bar_widget();
    _statusBarNeedsFirstRender = true;
    return ESP_OK;
}

esp_err_t status_bar_update_player_mode(player_mode_t mode) {
    statusBar.currentPlayerMode = mode;
    redraw_status_bar_widget();
    _statusBarNeedsFirstRender = true;
    return ESP_OK;
}

esp_err_t status_bar_show() {
    statusBar.isVisible = true;
    _statusBarShownAtMs = millis();
    _statusBarVisible = true;
    _statusBarNeedsFirstRender = true;
    return ESP_OK;
}
