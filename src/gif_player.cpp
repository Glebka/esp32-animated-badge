#include <AnimatedGIF.h>
#include <stddef.h>
#include <string.h>
#include <Arduino.h>
#include "gif_player.hpp"
#include "gif_utils.hpp"
#include <PNGdec.h>
#include <esp_log.h>
#include "status_bar.hpp"

static BB_SPI_LCD *_pLcd;
static AnimatedGIF _gif;
static uint8_t *_pTurboBuffer;
static uint8_t *_pFrameBuffer;
static int _iOffX, _iOffY;

static const char *TAG = "GIF_PLAYER";


static void GIFDraw(GIFDRAW *pDraw)
{
    const int drawX = _iOffX + pDraw->iX;
    const int drawY = _iOffY + pDraw->iY + pDraw->y;

    uint16_t *sendPixels = status_bar_draw_callback(_pLcd, drawX, drawY, pDraw->iWidth, (uint16_t *)pDraw->pPixels);

    if (pDraw->y == 0)
    {
        _pLcd->setAddrWindow(_iOffX + pDraw->iX, _iOffY + pDraw->iY, pDraw->iWidth, pDraw->iHeight);
    }

    _pLcd->pushPixels(sendPixels, pDraw->iWidth, DRAW_TO_LCD | DRAW_WITH_DMA);
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
    return ESP_OK;
}

esp_err_t gif_player_open_file(const char *filename)
{
    if (!_gif.open(filename, gifOpenSD, gifClose, gifRead, gifSeek, GIFDraw))
    {
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Successfully opened GIF; Canvas size = %d x %d", _gif.getCanvasWidth(), _gif.getCanvasHeight());
    _gif.setDrawType(GIF_DRAW_COOKED);
    _gif.setFrameBuf(_pFrameBuffer);
    _gif.setTurboBuf(_pTurboBuffer);
    _iOffX = (_pLcd->width() - _gif.getCanvasWidth()) / 2;
    _iOffY = (_pLcd->height() - _gif.getCanvasHeight()) / 2;
    return ESP_OK;
}

player_result_t gif_player_render_frame(bool loop)
{
    status_bar_pre_frame_render_callback(_pLcd, true);
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