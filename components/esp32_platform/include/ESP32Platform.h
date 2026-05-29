/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file ESP32Platform.h
 * @brief @ref CW_Platform implementation for ESP32 using FreeRTOS.
 *
 * @ref ESP32Platform maps the single @ref CW_Platform::sleep_ms abstraction to
 * @c vTaskDelay so the platform-independent SDK core can insert timed pauses
 * without depending on any Arduino or RTOS header directly.
 *
 * @ingroup esp32_adapters
 */

#ifndef ESP32_PLATFORM_H
#define ESP32_PLATFORM_H

#include "CW_Platform.h"

/**
 * @class ESP32Platform
 * @ingroup esp32_adapters
 * @brief @ref CW_Platform backed by FreeRTOS @c vTaskDelay.
 *
 * The @ref sleep_ms implementation delegates to
 * @c vTaskDelay(pdMS_TO_TICKS(ms)), yielding the calling FreeRTOS task to the
 * scheduler for at least the requested duration.  The actual sleep may be
 * slightly longer than @p ms due to tick granularity.
 *
 * @note Instantiate once and inject into @ref CryptnoxWallet alongside the
 *       other adapter objects.
 *
 * @see CW_Platform
 * @see CryptnoxWallet
 */
class ESP32Platform : public CW_Platform {
public:
    /**
     * @brief Sleep for at least @p ms milliseconds.
     *
     * Calls @c vTaskDelay(pdMS_TO_TICKS(ms)).  The calling FreeRTOS task is
     * blocked and yields the CPU to other ready tasks for the duration.
     *
     * @param[in] ms Minimum sleep duration in milliseconds.  A value of @c 0
     *               yields once to the scheduler without a guaranteed delay.
     */
    void sleep_ms(uint32_t ms) override;

    /** @brief Default destructor. */
    ~ESP32Platform() override {}
};

#endif /* ESP32_PLATFORM_H */
