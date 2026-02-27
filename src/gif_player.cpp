#include <AnimatedGIF.h>
#include <stddef.h>
#include "gif_player.hpp"
#include "gif_utils.hpp"
#include <PNGdec.h>
#include <esp_log.h>

static BB_SPI_LCD *_pLcd;
static AnimatedGIF _gif;
static uint8_t *_pTurboBuffer;
static uint8_t *_pFrameBuffer;
static int _iOffX, _iOffY;

static PNG pd;
const int OV_WIDTH = 96;
const int OV_HEIGHT = 40;
const int OV_X = 240, OV_Y = 8;
static uint8_t *_pOvBuffer;

const char *TAG = "GIF_PLAYER";

extern "C"
{
    extern const uint8_t _binary_assets_icons_battery_png_start[] asm("_binary_assets_icons_battery_white_png_start");
    extern const uint8_t _binary_assets_icons_battery_png_end[] asm("_binary_assets_icons_battery_white_png_end");
}

static const uint8_t *_pBatteryPng = _binary_assets_icons_battery_png_start;
static const size_t _batteryPngSize = (size_t)(_binary_assets_icons_battery_png_end - _binary_assets_icons_battery_png_start);

static void GIFDraw(GIFDRAW *pDraw)
{
    const int drawX = _iOffX + pDraw->iX;
    const int drawY = _iOffY + pDraw->iY + pDraw->y;

    if (drawY >= OV_Y && drawY < (OV_Y + OV_HEIGHT))
    {
        const int lineStart = drawX;
        const int lineEnd = drawX + pDraw->iWidth;
        const int ovStart = (lineStart > OV_X) ? lineStart : OV_X;
        const int ovEnd = (lineEnd < (OV_X + OV_WIDTH)) ? lineEnd : (OV_X + OV_WIDTH);

        if (ovStart < ovEnd)
        {
            const int ovY = drawY - OV_Y;
            uint8_t *linePixels = pDraw->pPixels;

            for (int x = ovStart; x < ovEnd; ++x)
            {
                const int linePixelIndex = x - lineStart;
                uint8_t *gifPx = &linePixels[linePixelIndex * 2];

                const uint8_t gifHi = gifPx[0];
                const uint8_t gifLo = gifPx[1];
                const uint8_t gifR5 = gifHi >> 3;
                const uint8_t gifG6 = (uint8_t)(((gifHi & 0x07) << 3) | (gifLo >> 5));
                const uint8_t gifB5 = gifLo & 0x1F;

                const int ovX = x - OV_X;
                const uint8_t *ovPx = &_pOvBuffer[(ovY * OV_WIDTH + ovX) * 4];
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

                    outR5 = (uint8_t)((gifR5 * invA + ovR5 * ovA + 127) / 255);
                    outG6 = (uint8_t)((gifG6 * invA + ovG6 * ovA + 127) / 255);
                    outB5 = (uint8_t)((gifB5 * invA + ovB5 * ovA + 127) / 255);
                }

                const uint16_t out565 = (uint16_t)((outR5 << 11) | (outG6 << 5) | outB5);
                gifPx[0] = (uint8_t)(out565 >> 8);
                gifPx[1] = (uint8_t)(out565 & 0xFF);
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
    return ESP_OK;
}

player_result_t gif_player_render_frame(bool loop)
{
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