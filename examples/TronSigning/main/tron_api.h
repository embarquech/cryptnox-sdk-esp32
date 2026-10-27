/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file tron_api.h
 * @brief Minimal TRON full-node HTTP client (Wi-Fi + /wallet endpoints).
 *
 * The node serialises the transaction (protobuf) for us, so the firmware
 * never needs a protobuf encoder — it only has to verify what came back
 * before signing it. See @c main.cpp for that verification.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Raw signature length posted to the node: r(32) || s(32) || v(1). */
#define TRON_SIGNATURE_BYTES  (65U)

/**
 * Set the node base URL (e.g. "https://nile.trongrid.io") and, optionally,
 * a TronGrid API key. Pass NULL / "" for api_key on endpoints that don't
 * need one. Must be called before any other tron_api_* function.
 */
void tron_api_init(const char *base_url, const char *api_key);

/**
 * Connect to a Wi-Fi network and block until an IP is obtained (up to 30 s).
 * Returns true on success.
 */
bool tron_api_wifi_connect(const char *ssid, const char *password);

/**
 * POST /wallet/createtransaction — ask the node to build an unsigned TRX
 * transfer. On success the complete transaction JSON (txID, raw_data,
 * raw_data_hex) is written NUL-terminated into tx_json, ready to be handed
 * to tron_api_broadcast() once a signature is available.
 *
 * @param owner_b58 Sender address, Base58Check ("T…").
 * @param to_b58    Recipient address, Base58Check ("T…").
 * @param amount    Amount in SUN (1 TRX = 1,000,000 SUN).
 */
bool tron_api_create_transfer(const char *owner_b58, const char *to_b58,
                              uint64_t amount,
                              char *tx_json, size_t tx_json_size);

/**
 * Extract the value of a top-level JSON string field ("txID",
 * "raw_data_hex", …) into out. Returns false if absent or too long.
 */
bool tron_api_json_string(const char *json, const char *key,
                          char *out, size_t out_size);

/**
 * Decode a hex string into bytes. Returns false on an odd length, a
 * non-hex character, or insufficient room. Writes the byte count to
 * *out_len.
 */
bool tron_api_hex_to_bytes(const char *hex, uint8_t *out, size_t out_size,
                           size_t *out_len);

/**
 * POST /wallet/broadcasttransaction — splice the 65-byte signature into the
 * transaction JSON returned by tron_api_create_transfer() and submit it.
 *
 * Returns true only when the node reports "result":true. On a signature
 * that recovers to the wrong account the node answers SIGERROR and nothing
 * is committed, so the caller can safely retry with the other parity bit.
 *
 * @param resp Buffer for the node's raw response; always NUL-terminated so it
 *             can be logged on failure. Required.
 */
bool tron_api_broadcast(const char *tx_json,
                        const uint8_t signature[TRON_SIGNATURE_BYTES],
                        char *resp, size_t resp_size);

#ifdef __cplusplus
}
#endif
