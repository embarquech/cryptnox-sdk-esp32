/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

#ifndef ESP32_PLATFORM_H
#define ESP32_PLATFORM_H

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include "CW_Platform.h"

/******************************************************************
 * 2. Class declaration
 ******************************************************************/

/**
 * @class ESP32Platform
 * @brief Concrete CW_Platform implementation for ESP32 using FreeRTOS.
 *
 * sleep_ms delegates to vTaskDelay(pdMS_TO_TICKS(ms)) so the calling task
 * yields to the RTOS scheduler for at least the requested duration.
 */
class ESP32Platform : public CW_Platform {
public:
    void sleep_ms(uint32_t ms) override;

    ~ESP32Platform() override {}
};

#endif /* ESP32_PLATFORM_H */
