/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* =========================
 * WiFi Configuration
 * ========================= */
#define WIFI_SSID        "<YOUR_SSID>"
#define WIFI_PASSWORD    "<YOUR_PASSWORD>"

/* =========================
 * TRON full node
 * ========================= */
/**
 * Nile testnet — free test TRX from https://nileex.io/join/getJoinPage
 *
 * Switch to mainnet only when you are ready to move real TRX:
 *
 *     #define TRON_NODE_URL "https://api.trongrid.io"
 *
 * TronGrid works without a key at low rates. Create a free key at
 * https://www.trongrid.io if you hit rate limits; leave the empty
 * string to send no TRON-PRO-API-KEY header.
 */
#define TRON_NODE_URL    "https://nile.trongrid.io"
#define TRON_API_KEY     ""

/* =========================
 * Wallet / Keys (SENSITIVE)
 * ========================= */
/**
 * ⚠️ NEVER COMMIT config.h — it contains credentials.
 *    Add config.h to .gitignore.
 */

#define CARD_PIN      "<CARD_PIN>"   /* 4-9 digit PIN, e.g. "000000000" */
#define CARD_PIN_LEN  (9U)           /* number of digits in CARD_PIN     */

/* =========================
 * TRON Addresses
 * ========================= */
/**
 * Base58Check addresses — 34 characters, starting with 'T'.
 *
 * TRON_ADDR_FROM must be the address of the card key at
 * m/44'/195'/0'/0/0, otherwise the node rejects the signature.
 * Derive it from that path's public key with
 * CW_Tron::addressFromPublicKey().
 */
#define TRON_ADDR_FROM    "<SENDER_TRON_ADDRESS>"
#define TRON_ADDR_TO      "<RECIPIENT_TRON_ADDRESS>"

/* =========================
 * Transaction Parameters
 * ========================= */
/* Amount in SUN — TRX has 6 decimals, so 1 TRX = 1,000,000 SUN. */
#define AMOUNT_SUN        1000000ULL   /* 1.0 TRX */

#endif /* CONFIG_H */
