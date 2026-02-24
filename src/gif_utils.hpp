#pragma once

#include <stdint.h>
#include <AnimatedGIF.h>


#if USE_LITTLEFS
void *gifOpenLFS(const char *filename, int32_t *size);
#endif

void *gifOpenSD(const char *filename, int32_t *size);
void gifClose(void *handle);
int32_t gifRead(GIFFILE *handle, uint8_t *buffer, int32_t length);
int32_t gifSeek(GIFFILE *handle, int32_t iPosition);
