#include <JPEGDEC.h>
#include "jpeg_player.hpp"
#include "FS.h"
#if USE_LITTLEFS
#include <LittleFS.h>
#else
#include <SD_MMC.h>
#endif

static BB_SPI_LCD* _pLcd;
static JPEGDEC _jpg;
static File _jpgFile;
static bool _rendered = false;

static int JPEGDraw(JPEGDRAW *pDraw)
{
  _pLcd->setAddrWindow(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight);
  _pLcd->pushPixels((uint16_t *)pDraw->pPixels, pDraw->iWidth * pDraw->iHeight, DRAW_TO_LCD | DRAW_WITH_DMA);
  return 1;
} 

// static int JPEGDraw(JPEGDRAW *pDraw)
// {
//   int x0 = pDraw->x + _iOffX;
//   int y0 = pDraw->y + _iOffY;
//   int x1 = x0 + pDraw->iWidth  - 1;
//   int y1 = y0 + pDraw->iHeight - 1;

//   _pLcd->setAddrWindow(x0, y0, x1, y1);
//   _pLcd->pushPixels((uint16_t *)pDraw->pPixels,
//                     pDraw->iWidth * pDraw->iHeight,
//                     DRAW_TO_LCD | DRAW_WITH_DMA);
//   return 1;
// }

esp_err_t jpeg_player_init(BB_SPI_LCD* lcd) {
    _pLcd = lcd;
    return ESP_OK;
}

esp_err_t jpeg_player_open_file(const char *filename) {
    #if USE_LITTLEFS
        _jpgFile = LittleFS.open(filename, FILE_READ);
    #else
        _jpgFile = SD_MMC.open(filename);
    #endif
    if (!_jpgFile) {
        Serial.printf("Failed to open file: %s\n", filename);
        return ESP_FAIL;
    }
    if (!_jpg.open(_jpgFile, JPEGDraw)) {
        Serial.printf("Failed to open JPEG: %s\n", filename);
        _jpgFile.close();
        return ESP_FAIL;
    }
    _jpg.setPixelType(RGB565_BIG_ENDIAN);
    _jpg.setMaxOutputSize(368 * 448); // <-- work-around for some JPEGs that cause OOM; should be enough for 720p subsampled to fit in 320x240
    //_jpg.setCropArea(0, 0, 368, 448);

    Serial.printf("Successfully opened JPEG; Canvas size = %d x %d\n", _jpg.getWidth(), _jpg.getHeight());
    
    _rendered = false;
    return ESP_OK;
}

player_result_t jpeg_player_render_frame(bool loop) {
    if (_rendered || _jpg.decode(0, 0, 0)) {
        vTaskDelay(1);
        _rendered = true;
        return loop ? PLAYER_OK_CONTINUE : PLAYER_OK_EOF;
    }
    return PLAYER_ERROR;
}

esp_err_t jpeg_player_close_file() {
    _rendered = false;
    _jpg.close();
    _jpgFile.close();
    return ESP_OK;
}