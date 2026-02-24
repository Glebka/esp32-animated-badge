#include <FS.h>

#if USE_LITTLEFS

#include <LittleFS.h>

#else

#include <SD_MMC.h>

#endif

#include "gif_utils.hpp"

#if USE_LITTLEFS
void *gifOpenLFS(const char *filename, int32_t *size)
{
    static File myfile;
    myfile = LittleFS.open(filename, FILE_READ);
    if (myfile)
    {
        *size = myfile.size();
        return &myfile;
    }
    return NULL;
}
#endif

void *gifOpenSD(const char *filename, int32_t *size)
{
    static File myfile;
    myfile = SD_MMC.open(filename);
    if (myfile)
    {
        *size = myfile.size();
        return &myfile;
    }
    return NULL;
}

void gifClose(void *handle)
{
    File *pFile = (File *)handle;
    if (pFile)
        pFile->close();
}

int32_t gifRead(GIFFILE *handle, uint8_t *buffer, int32_t length)
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

int32_t gifSeek(GIFFILE *handle, int32_t iPosition)
{
    File *f = static_cast<File *>(handle->fHandle);
    f->seek(iPosition);
    handle->iPos = (int32_t)f->position();
    return handle->iPos;
}