#include "hw_config.hpp"

#define PNG_MAX_BUFFERED_PIXELS ((LCD_WIDTH*4 + 1)*2)

#include <PNGdec.h>
#include "png_player.hpp"
#include "FS.h"
#if USE_LITTLEFS
#include <LittleFS.h>
#else
#include <SD_MMC.h>
#endif
#include "gif_utils.hpp"

static BB_SPI_LCD *_pLcd;
static PNG _png;
static bool _rendered = false;
static int w, h, xoff, yoff;
static uint16_t usPixels[LCD_WIDTH];


static int32_t pngRead(PNGFILE *handle, uint8_t *buffer, int32_t length)
{
    int32_t iBytesRead;
    iBytesRead = length;
    File *f = static_cast<File *>(handle->fHandle);
    // Note: If you read a file all the way to the last byte, seek() stops working
    if ((handle->iSize - handle->iPos) < length)
        iBytesRead = handle->iSize - handle->iPos - 1; // <-- ugly work-around
    if (iBytesRead <= 0)
        return 0;
    iBytesRead = (int32_t)f->read(buffer, iBytesRead);
    handle->iPos = f->position();
    return iBytesRead;
}

static int32_t pngSeek(PNGFILE *handle, int32_t position)
{
    File *f = static_cast<File *>(handle->fHandle);
    f->seek(position);
    handle->iPos = (int32_t)f->position();
    return handle->iPos;
}

int PNGDraw(PNGDRAW *pDraw)
{
    if (pDraw->y == 0)
    {
        // set the address window when we get the first line
        _pLcd->setAddrWindow(xoff, yoff, w, h);
    }
    // There's a risk of overwriting pixels that are still being sent to the display if we only use a single \
    // DMA buffer, **BUT** in this case, the PNG decoding plus the pixel conversion takes a long time
    // relative to sending pixels to the display. If the display were a REALLY slow one, then it would be
    // prodent to use a dual (ping-pong) buffer scheme to avoid that risk.
    _png.getLineAsRGB565(pDraw, usPixels, PNG_RGB565_BIG_ENDIAN, 0); // get help converting to RGB565
    _pLcd->pushPixels(usPixels, pDraw->iWidth, DRAW_TO_LCD | DRAW_WITH_DMA);
    return 1;
}

esp_err_t png_player_init(BB_SPI_LCD *lcd)
{
    _pLcd = lcd;
    xoff = 0;
    yoff = 0;
    w = LCD_WIDTH;
    h = LCD_HEIGHT;
    return ESP_OK;
}

esp_err_t png_player_open_file(const char *filename)
{
    int rc = _png.open(filename, gifOpenSD, gifClose, pngRead, pngSeek, PNGDraw);
    if (rc != PNG_SUCCESS)
    {
        Serial.printf("PNG error: %d\n", rc);
        Serial.printf("Failed to open PNG: %s\n", filename);
        return ESP_FAIL;
    }
    Serial.printf("Successfully opened PNG; Canvas size = %d x %d\n", _png.getWidth(), _png.getHeight());
    _rendered = false;
    return ESP_OK;
}

player_result_t png_player_render_frame(bool loop)
{
    if (_rendered || _png.decode(NULL, PNG_FAST_PALETTE) == 0)
    {
        vTaskDelay(1);
        _rendered = true;
        return loop ? PLAYER_OK_CONTINUE : PLAYER_OK_EOF;
    }
    if (_png.getLastError() > 0)
    {
        Serial.println("PNG decoding error!");
        Serial.printf("PNG error code: %d\n", _png.getLastError());
        return PLAYER_OK_EOF;
    }
    return PLAYER_ERROR;
}

esp_err_t png_player_close_file()
{
    _rendered = false;
    _png.close();
    return ESP_OK;
}