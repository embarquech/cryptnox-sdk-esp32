/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file cw_utils_c.cpp
 * @brief C linkage for @ref CW_Utils — see cw_utils_c.h.
 */

#include "cw_utils_c.h"
#include "CW_Utils.h"

extern "C" bool cw_safe_memcpy(uint8_t *dst, size_t dst_size,
                               const uint8_t *src, size_t count)
{
    return CW_Utils::safe_memcpy(dst, dst_size, src, count);
}
