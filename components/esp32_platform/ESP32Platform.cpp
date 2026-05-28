/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

#include "ESP32Platform.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/** @brief Yield to the FreeRTOS scheduler for at least @p ms milliseconds. */
void ESP32Platform::sleep_ms(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(static_cast<TickType_t>(ms)));
}
