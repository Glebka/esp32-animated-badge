#pragma once

#include <esp_err.h>

esp_err_t init_watchdog();
void watchdog_kick();
