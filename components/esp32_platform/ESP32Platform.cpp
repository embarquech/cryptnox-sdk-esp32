/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file ESP32Platform.cpp
 * @brief Implementation of @ref ESP32Platform — FreeRTOS sleep backend.
 *
 * Delegates @ref CW_Platform::sleep_ms to @c vTaskDelay so the calling
 * FreeRTOS task yields to the scheduler for the requested duration.  Full API
 * documentation lives on the declarations in @ref ESP32Platform.h.
 */

#include "ESP32Platform.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/** @brief Yield to the FreeRTOS scheduler for at least @p ms milliseconds. */
void ESP32Platform::sleep_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(static_cast<TickType_t>(ms)));
}
