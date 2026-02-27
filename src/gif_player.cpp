#include <AnimatedGIF.h>
#include <stddef.h>
#include <string.h>
#include <Arduino.h>
#include "gif_player.hpp"
#include "gif_utils.hpp"
#include <PNGdec.h>
#include <esp_log.h>

static BB_SPI_LCD *_pLcd;
static AnimatedGIF _gif;
static uint8_t *_pTurboBuffer;
static uint8_t *_pFrameBuffer;
static int _iOffX, _iOffY;
static BB_SPI_LCD _statusBar;
static uint8_t *_pStatusBarBuffer;
static BB_SPI_LCD _restoreBar;
static uint8_t *_pRestoreBarBuffer;
static bool _statusBarVisible;
static uint32_t _statusBarShownAtMs;

static PNG pd;
const int OV_WIDTH = 96;
const int OV_HEIGHT = 40;
const int OV_X = 240, OV_Y = 8;
static uint8_t *_pOvBuffer;
static constexpr int STATUS_BAR_HEIGHT = OV_HEIGHT + OV_Y * 2;
static constexpr uint32_t STATUS_BAR_TIMEOUT_MS = 5000;

const char *TAG = "GIF_PLAYER";

extern "C"
{
    extern const uint8_t _binary_assets_icons_battery_png_start[] asm("_binary_assets_icons_battery_white_png_start");
    extern const uint8_t _binary_assets_icons_battery_png_end[] asm("_binary_assets_icons_battery_white_png_end");
}

static const uint8_t *_pBatteryPng = _binary_assets_icons_battery_png_start;
static const size_t _batteryPngSize = (size_t)(_binary_assets_icons_battery_png_end - _binary_assets_icons_battery_png_start);

static esp_err_t prepare_status_bar()
{
    const int screenWidth = _pLcd->width();
    const int barHeight = STATUS_BAR_HEIGHT;
    if (_statusBar.createVirtual(screenWidth, barHeight, nullptr, true) == 0)
    {
        return ESP_ERR_NO_MEM;
    }

    if (_restoreBar.createVirtual(screenWidth, barHeight, nullptr, true) == 0)
    {
        return ESP_ERR_NO_MEM;
    }

    constexpr uint16_t barColor565 = 0;//0x8410;

    _statusBar.fillRect(0, 0, screenWidth, barHeight, barColor565, DRAW_TO_RAM);

    constexpr uint8_t barR5 = (uint8_t)(barColor565 >> 11);
    constexpr uint8_t barG6 = (uint8_t)((barColor565 >> 5) & 0x3F);
    constexpr uint8_t barB5 = (uint8_t)(barColor565 & 0x1F);

    for (int y = 0; y < OV_HEIGHT; ++y)
    {
        const int dstY = OV_Y + y;
        if (dstY < 0 || dstY >= barHeight)
        {
            continue;
        }

        for (int x = 0; x < OV_WIDTH; ++x)
        {
            const int dstX = OV_X + x;
            if (dstX < 0 || dstX >= screenWidth)
            {
                continue;
            }

            const uint8_t *ovPx = &_pOvBuffer[(y * OV_WIDTH + x) * 4];
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
            _statusBar.drawPixel(dstX, dstY, out565, DRAW_TO_RAM);
        }
    }

    _pStatusBarBuffer = (uint8_t *)_statusBar.getBuffer();
    _pRestoreBarBuffer = (uint8_t *)_restoreBar.getBuffer();
    if (!_pStatusBarBuffer || !_pRestoreBarBuffer)
    {
        return ESP_FAIL;
    }
    memset(_pRestoreBarBuffer, 0, (size_t)screenWidth * (size_t)barHeight * 2);
    return ESP_OK;
}

static void GIFDraw(GIFDRAW *pDraw)
{
    const int drawX = _iOffX + pDraw->iX;
    const int drawY = _iOffY + pDraw->iY + pDraw->y;
    const int barHeight = STATUS_BAR_HEIGHT;

    if ((_pStatusBarBuffer || _pRestoreBarBuffer) && drawY >= 0 && drawY < barHeight)
    {
        const int lineStart = drawX;
        const int lineEnd = drawX + pDraw->iWidth;
        const int copyStart = (lineStart > 0) ? lineStart : 0;
        const int copyEnd = (lineEnd < _pLcd->width()) ? lineEnd : _pLcd->width();

        if (copyStart < copyEnd)
        {
            const int dstOffsetBytes = (copyStart - lineStart) * 2;
            const int srcOffsetBytes = (drawY * _pLcd->width() + copyStart) * 2;
            const int copyBytes = (copyEnd - copyStart) * 2;

            if (_statusBarVisible && _pRestoreBarBuffer)
            {
                memcpy(&_pRestoreBarBuffer[srcOffsetBytes], &pDraw->pPixels[dstOffsetBytes], copyBytes);
            }

            if (_statusBarVisible && _pStatusBarBuffer)
            {
                memcpy(&pDraw->pPixels[dstOffsetBytes], &_pStatusBarBuffer[srcOffsetBytes], copyBytes);
            }
        }
    }

    if (pDraw->y == 0)
    {
        _pLcd->setAddrWindow(_iOffX + pDraw->iX, _iOffY + pDraw->iY, pDraw->iWidth, pDraw->iHeight);
    }

    _pLcd->pushPixels((uint16_t *)pDraw->pPixels, pDraw->iWidth, DRAW_TO_LCD | DRAW_WITH_DMA);
}

esp_err_t gif_player_init(BB_SPI_LCD *lcd)
{
    _iOffX = 0;
    _iOffY = 0;
    _gif.begin(GIF_PALETTE_RGB565_BE);
    _pLcd = lcd;
    _pTurboBuffer = (uint8_t *)heap_caps_malloc(TURBO_BUFFER_SIZE + (_pLcd->width() * _pLcd->height()), MALLOC_CAP_8BIT);
    _pFrameBuffer = (uint8_t *)heap_caps_malloc(_pLcd->width() * _pLcd->height() * sizeof(uint16_t), MALLOC_CAP_8BIT);
    if (!_pTurboBuffer || !_pFrameBuffer)
    {
        return ESP_ERR_NO_MEM;
    }
    _pOvBuffer = (uint8_t *)heap_caps_malloc(OV_WIDTH * OV_HEIGHT * 4, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if (!_pOvBuffer)
    {
        return ESP_ERR_NO_MEM;
    }
    if (pd.openRAM((uint8_t *)_pBatteryPng, _batteryPngSize, NULL) != 0)
    {
        return ESP_FAIL;
    }
    pd.setBuffer(_pOvBuffer);
    if (pd.decode(NULL, 0) != 0)
    {
        ESP_LOGE(TAG, "Failed to decode battery PNG");
        ESP_LOGE(TAG, "PNG error code: %d", pd.getLastError());
        return ESP_FAIL;
    }
    if (prepare_status_bar() != ESP_OK)
    {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t gif_player_open_file(const char *filename)
{
    if (!_gif.open(filename, gifOpenSD, gifClose, gifRead, gifSeek, GIFDraw))
    {
        return ESP_FAIL;
    }
    Serial.printf("Successfully opened GIF; Canvas size = %d x %d\n", _gif.getCanvasWidth(), _gif.getCanvasHeight());
    _gif.setDrawType(GIF_DRAW_COOKED);
    _gif.setFrameBuf(_pFrameBuffer);
    _gif.setTurboBuf(_pTurboBuffer);
    _iOffX = (_pLcd->width() - _gif.getCanvasWidth()) / 2;
    _iOffY = (_pLcd->height() - _gif.getCanvasHeight()) / 2;
    _statusBarVisible = true;
    _statusBarShownAtMs = millis();
    return ESP_OK;
}

player_result_t gif_player_render_frame(bool loop)
{
    if (_statusBarVisible && (uint32_t)(millis() - _statusBarShownAtMs) >= STATUS_BAR_TIMEOUT_MS)
    {
        if (_pRestoreBarBuffer)
        {
            _pLcd->drawSprite(0, 0, &_restoreBar, 1.0f, 0xffffffff, DRAW_TO_LCD | DRAW_WITH_DMA);
        }
        _statusBarVisible = false;
    }

    int res = _gif.playFrame(true, NULL);
    if (res == PLAYER_OK_EOF && loop)
    {
        _gif.reset();
        return PLAYER_OK_CONTINUE;
    }
    return (player_result_t)res;
}

esp_err_t gif_player_close_file()
{
    _gif.close();
    return ESP_OK;
}