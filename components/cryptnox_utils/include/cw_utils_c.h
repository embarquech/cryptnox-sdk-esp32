/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file cw_utils_c.h
 * @brief C entry points to @ref CW_Utils, for the drivers written in C.
 *
 * CW_Utils is a C++ class, so a C translation unit (the PN532 driver) cannot
 * call it directly. These forward to it unchanged.
 */

#ifndef CW_UTILS_C_H
#define CW_UTILS_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bounds-checked copy: @ref CW_Utils::safe_memcpy.
 *
 * @param[out] dst      Destination buffer.
 * @param[in]  dst_size Capacity of @p dst in bytes.
 * @param[in]  src      Source buffer.
 * @param[in]  count    Bytes to copy.
 * @return true when copied; false (nothing copied) on a NULL pointer, a zero
 *         @p count, @p count larger than @p dst_size, or overlapping buffers.
 */
bool cw_safe_memcpy(uint8_t *dst, size_t dst_size, const uint8_t *src, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* CW_UTILS_C_H */
