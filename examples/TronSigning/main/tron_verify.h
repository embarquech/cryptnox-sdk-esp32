/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file tron_verify.h
 * @brief Check that a node-built TRON transaction spends what we asked for.
 *
 * The TRON full node serialises the transaction (protobuf) on our behalf,
 * so the firmware must confirm the bytes before handing the digest to the
 * card. Kept free of ESP-IDF dependencies so it can be self-checked on a
 * host — see the build line in tron_verify.cpp.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Raw TRON address length (0x41 prefix + 20 bytes), mirrors CW_TRON_ADDRESS_BYTES. */
#define TRON_ADDR_RAW_BYTES  (21U)

/**
 * Verify that a decoded @c raw_data really encodes the expected
 * TransferContract fields.
 *
 * @param raw      Decoded @c raw_data_hex bytes.
 * @param raw_len  Length of @p raw.
 * @param owner21  Expected sender, raw 21 bytes.
 * @param to21     Expected recipient, raw 21 bytes.
 * @param amount   Expected amount in SUN.
 * @return NULL when the transaction matches, otherwise a static string
 *         naming the first field that did not.
 */
const char *tron_verify_transfer(const uint8_t *raw, size_t raw_len,
                                 const uint8_t *owner21, const uint8_t *to21,
                                 uint64_t amount);

#ifdef __cplusplus
}
#endif
