/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file main.cpp
 * @example UsdcSigning/main/main.cpp
 * @brief Cryptnox ESP32 example: build, sign, and broadcast a USDC ERC-20 transfer.
 *
 * Wiring & prerequisites:
 *   - PN532 NFC reader — transport selected by @ref PN532_USE_I2C at the top
 *     of this file.
 *   - A Cryptnox card initialised with a seed and a known PIN.
 *   - @c config.h filled in with WiFi, RPC endpoint, and Ethereum addresses
 *     (copy from @c config.template.h and fill in the values).
 *
 * What the firmware does in each loop iteration:
 *   1. Wait for a card tap and establish the secure channel.
 *   2. Fetch the current on-chain nonce from the RPC endpoint.
 *   3. Build an EIP-1559 type-2 transaction for a USDC
 *      @c transfer(address,uint256) call.
 *   4. RLP-encode the unsigned transaction and Keccak-256 hash it.
 *   5. Sign the hash on the card (@ref CryptnoxWallet::sign).
 *   6. Recover the @c v parity bit via @c eth_rpc_ecrecover_parity.
 *   7. RLP-encode the signed transaction and broadcast it.
 *
 * @note Fill in @ref WIFI_SSID, @ref WIFI_PASSWORD, @ref RPC_URL,
 *       @ref ADDR_FROM, @ref ADDR_TO, @ref ADDR_USDC, and @ref CARD_PIN
 *       in @c config.h before building.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "CryptnoxWallet.h"
#include "Pn532NfcTransport.h"
#include "ESP32Logger.h"
#include "esp32_crypto_provider.h"
#include "ESP32Platform.h"

extern "C" {
#include "pn532.h"
#include "keccak256.h"
#include "eth_rlp.h"
#include "eth_rpc.h"
}

#include "config.h"

static const char *const TAG = "usdc_signing";

/* ── PN532 transport selector ─────────────────────────────────────
 * Enable exactly one transport by setting its flag to 1 (the other to 0). */
#define SPI_ENABLED         1
#define I2C_ENABLED         0

/* ── SPI wiring — ESP32-S3 dev kit + Keyestudio PN532 breakout ──── */
#if SPI_ENABLED
#define SPI_MOSI            11
#define SPI_MISO            13
#define SPI_SCLK            12
#define SPI_MAX_TRANSFER_SZ 4096
#define SPI_PIN_UNUSED      (-1)
#define NFC_CS              10
#endif

/* ── I²C wiring — Cheap Yellow Display (ESP32) CN1 connector ───── */
#if I2C_ENABLED
#define PN532_I2C_PORT      0          /* I2C_NUM_0 */
#define PN532_SDA           27         /* CN1 SDA */
#define PN532_SCL           22         /* CN1 SCL */
#define PN532_IRQ           (-1)       /* unused */
#define PN532_RST           (-1)       /* unused */
#define PN532_I2C_HZ        100000U
#endif

/* ── USDC ERC-20 transfer(address,uint256) selector ──────────── */
static const uint8_t TRANSFER_SELECTOR[4] = { 0xa9U, 0x05U, 0x9cU, 0xbbU };

/* ── Unsigned and signed tx buffers (EIP-1559 type 2) ─────────── */
#define TX_BUF_SIZE 300U

/******************************************************************
 * Helpers
 ******************************************************************/

/**
 * @brief Parse a hex string into a 20-byte Ethereum address.
 *
 * Accepts an optional @c 0x or @c 0X prefix.  If the source is shorter than
 * 20 bytes the remaining output bytes are zeroed; if longer, the excess is
 * silently discarded.
 *
 * @param[in]  hex Hex string (with or without @c 0x prefix).
 * @param[out] out 20-byte output buffer for the decoded address.
 */
static void parse_address(const char *hex, uint8_t out[20])
{
    const char *p = hex;
    if ((p[0] == '0') && ((p[1] == 'x') || (p[1] == 'X'))) {
        p += 2;
    }
    (void)memset(out, 0, 20U);
    size_t i;
    for (i = 0U; (i < 20U) && (p[0] != '\0') && (p[1] != '\0'); i++) {
        uint8_t hi = (uint8_t)((*p >= 'a') ? (*p - 'a' + 10) :
                               (*p >= 'A') ? (*p - 'A' + 10) : (*p - '0'));
        p++;
        uint8_t lo = (uint8_t)((*p >= 'a') ? (*p - 'a' + 10) :
                               (*p >= 'A') ? (*p - 'A' + 10) : (*p - '0'));
        p++;
        out[i] = (uint8_t)((hi << 4U) | lo);
    }
}

/**
 * @brief Build the 68-byte ABI-encoded calldata for a USDC @c transfer call.
 *
 * Encodes the ERC-20 @c transfer(address,uint256) selector followed by the
 * ABI-encoded arguments:
 * @code
 * selector(4) | zeroes(12) | to(20) | zeroes(24) | amount_be(8)
 * @endcode
 *
 * @param[out] out    68-byte output buffer for the calldata.
 * @param[in]  to_hex Recipient address as a hex string (with or without @c 0x).
 * @param[in]  amount Transfer amount in USDC base units (6 decimals).
 */
static void build_usdc_calldata(uint8_t out[68], const char *to_hex, uint64_t amount)
{
    (void)memset(out, 0, 68U);

    /* Function selector */
    (void)memcpy(out, TRANSFER_SELECTOR, 4U);

    /* 'to' address: right-aligned in 32-byte slot starting at offset 4 */
    uint8_t addr[20];
    parse_address(to_hex, addr);
    (void)memcpy(out + 4U + 12U, addr, 20U);

    /* amount: right-aligned uint256, 64-bit value fits in the last 8 bytes */
    size_t j;
    for (j = 0U; j < 8U; j++) {
        out[67U - j] = (uint8_t)((amount >> (8U * j)) & 0xFFU);
    }
}

/******************************************************************
 * Signing loop
 ******************************************************************/

/**
 * @brief Main application loop: sign and broadcast a USDC transfer each card tap.
 *
 * Each iteration waits for a card, fetches the current nonce, builds and
 * hashes an EIP-1559 USDC transfer transaction, signs it on the card via
 * @ref CryptnoxWallet::sign, recovers the @c v parity, and broadcasts the
 * signed transaction via @c eth_rpc_send_raw_tx.
 *
 * @param[in] wallet Initialised wallet instance.
 */
static void signing_loop(CryptnoxWallet &wallet)
{
    /* CARD_PIN is a string literal ("000000000"); copy into the pin array. */
    uint8_t card_pin[CW_MAX_PIN_LENGTH];
    (void)memset(card_pin, 0U, sizeof(card_pin));
    (void)memcpy(card_pin, CARD_PIN,
                 (CARD_PIN_LEN < CW_MAX_PIN_LENGTH) ? CARD_PIN_LEN : CW_MAX_PIN_LENGTH);

    uint8_t calldata[68];
    build_usdc_calldata(calldata, "0x" ADDR_TO, AMOUNT_USDC);

    while (true) {
        /* ── 1. Wait for card ──────────────────────────────────── */
        ESP_LOGI(TAG, "Hold Cryptnox card to reader to sign...");

        CW_SecureSession session;
        bool connected = wallet.connect(session);
        if (!connected) {
            vTaskDelay(pdMS_TO_TICKS(100U));
            continue;
        }

        /* ── 2. Get fresh nonce (card is present — fetch now) ──── */
        uint64_t nonce = 0U;
        if (!eth_rpc_get_nonce(&nonce)) {
            ESP_LOGE(TAG, "Failed to get nonce — retrying in 5 s");
            wallet.disconnect(session);
            vTaskDelay(pdMS_TO_TICKS(5000U));
            continue;
        }

        /* ── 3. Build tx ───────────────────────────────────────── */
        eth_tx_t tx;
        (void)memset(&tx, 0, sizeof(tx));
        tx.chain_id          = CHAIN_ID_SEPOLIA;
        tx.nonce             = nonce;
        tx.max_priority_fee  = MAX_PRIORITY_FEE;
        tx.max_fee           = MAX_FEE;
        tx.gas_limit         = GAS_LIMIT_ERC20;
        tx.eth_value         = 0U;
        tx.calldata          = calldata;
        tx.calldata_len      = sizeof(calldata);
        parse_address("0x" ADDR_USDC, tx.to);

        /* ── 4. Encode unsigned tx and hash it ─────────────────── */
        uint8_t unsigned_tx[TX_BUF_SIZE];
        size_t  unsigned_len = eth_rlp_encode_unsigned(&tx, unsigned_tx, sizeof(unsigned_tx));
        if (unsigned_len == 0U) {
            ESP_LOGE(TAG, "RLP encode unsigned overflow");
            wallet.disconnect(session);
            vTaskDelay(pdMS_TO_TICKS(2000U));
            continue;
        }

        uint8_t hash[CW_HASH_SIZE];
        keccak256(unsigned_tx, unsigned_len, hash);

        ESP_LOGI(TAG, "Hash to sign:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, hash, CW_HASH_SIZE, ESP_LOG_INFO);

        /* BIP32 Ethereum derivation path: m/44'/60'/0'/0/0
         * Each level is a 4-byte big-endian uint32; hardened levels have the
         * high bit set. */
        static const uint8_t eth_path[20] = {
            0x80U, 0x00U, 0x00U, 0x2CU,   /* 44' */
            0x80U, 0x00U, 0x00U, 0x3CU,   /* 60' */
            0x80U, 0x00U, 0x00U, 0x00U,   /* 0'  */
            0x00U, 0x00U, 0x00U, 0x00U,   /* 0   */
            0x00U, 0x00U, 0x00U, 0x00U,   /* 0   */
        };

        CW_SignRequest req(session,
                          CW_SIGN_DERIVE_K1,
                          CW_SIGN_SIG_ECDSA_LOW_S,
                          CW_SIGN_WITH_PIN);
        req.hash             = hash;
        req.hashLength       = static_cast<uint8_t>(CW_HASH_SIZE);
        req.derivePath       = eth_path;
        req.derivePathLength = static_cast<uint8_t>(sizeof(eth_path));
        (void)memcpy(req.pin, card_pin, CW_MAX_PIN_LENGTH);

        CW_SignResult result = wallet.sign(req);
        wallet.disconnect(session);

        if (result.errorCode != CW_OK) {
            ESP_LOGE(TAG, "Sign failed: 0x%02X",
                     static_cast<unsigned int>(result.errorCode));
            vTaskDelay(pdMS_TO_TICKS(2000U));
            continue;
        }

        uint8_t sig_r[CW_HASH_SIZE];
        uint8_t sig_s[CW_HASH_SIZE];
        (void)memcpy(sig_r, result.signature + CW_SIG_R_OFFSET, CW_HASH_SIZE);
        (void)memcpy(sig_s, result.signature + CW_SIG_S_OFFSET, CW_HASH_SIZE);

        ESP_LOGI(TAG, "r:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, sig_r, CW_HASH_SIZE, ESP_LOG_INFO);
        ESP_LOGI(TAG, "s:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, sig_s, CW_HASH_SIZE, ESP_LOG_INFO);

        /* ── 6. Determine v parity via ecrecover ───────────────── */
        uint8_t v = eth_rpc_ecrecover_parity(hash, sig_r, sig_s);
        ESP_LOGI(TAG, "v = %u", static_cast<unsigned int>(v));

        /* ── 7. Encode signed tx ───────────────────────────────── */
        uint8_t signed_tx[TX_BUF_SIZE];
        size_t  signed_len = eth_rlp_encode_signed(&tx, v, sig_r, sig_s,
                                                   signed_tx, sizeof(signed_tx));
        if (signed_len == 0U) {
            ESP_LOGE(TAG, "RLP encode signed overflow");
            vTaskDelay(pdMS_TO_TICKS(2000U));
            continue;
        }

        ESP_LOGI(TAG, "Signed tx (%u bytes):", (unsigned int)signed_len);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, signed_tx, signed_len, ESP_LOG_INFO);

        /* ── 8. Broadcast ──────────────────────────────────────── */
        char tx_hash[68] = { 0 };
        if (eth_rpc_send_raw_tx(signed_tx, signed_len,
                                tx_hash, sizeof(tx_hash))) {
            ESP_LOGI(TAG, "TX broadcast OK: %s", tx_hash);
        } else {
            ESP_LOGE(TAG, "TX broadcast failed");
        }

        /* Wait before next iteration so nonce advances on-chain. */
        vTaskDelay(pdMS_TO_TICKS(15000U));
    }
}

/******************************************************************
 * Entry point
 ******************************************************************/

/**
 * @brief ESP-IDF application entry point.
 *
 * Initialises NVS, brings up the PN532 reader (I²C or SPI), connects to
 * Wi-Fi and the Ethereum RPC endpoint, then enters @ref signing_loop.
 */
extern "C" void app_main(void)
{
    /* ── NVS (required by WiFi driver) ────────────────────────── */
    esp_err_t nvs_ret = nvs_flash_init();
    if ((nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);

    /* ── PN532 NFC reader (transport selected at the top of this file) ── */
    pn532_t nfc;
    (void)memset(&nfc, 0, sizeof(nfc));

    pn532_config_t nfc_cfg;
    (void)memset(&nfc_cfg, 0, sizeof(nfc_cfg));
#if SPI_ENABLED
    spi_bus_config_t buscfg;
    (void)memset(&buscfg, 0, sizeof(buscfg));
    buscfg.mosi_io_num     = SPI_MOSI;
    buscfg.miso_io_num     = SPI_MISO;
    buscfg.sclk_io_num     = SPI_SCLK;
    buscfg.quadwp_io_num   = SPI_PIN_UNUSED;
    buscfg.quadhd_io_num   = SPI_PIN_UNUSED;
    buscfg.max_transfer_sz = SPI_MAX_TRANSFER_SZ;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    nfc_cfg.transport     = PN532_TRANSPORT_SPI;
    nfc_cfg.spi_host      = SPI2_HOST;
    nfc_cfg.pin_cs        = NFC_CS;
    nfc_cfg.skip_bus_init = true;
#endif

#if I2C_ENABLED
    nfc_cfg.transport     = PN532_TRANSPORT_I2C;
    nfc_cfg.i2c_port      = PN532_I2C_PORT;
    nfc_cfg.pin_sda       = PN532_SDA;
    nfc_cfg.pin_scl       = PN532_SCL;
    nfc_cfg.pin_irq       = PN532_IRQ;
    nfc_cfg.pin_rst       = PN532_RST;
    nfc_cfg.i2c_clock_hz  = PN532_I2C_HZ;
#endif

    esp_err_t nfc_ret = pn532_init(&nfc, &nfc_cfg);
    if (nfc_ret != ESP_OK) {
        ESP_LOGE(TAG, "PN532 init failed");
        return;
    }

    /* ── Wallet setup ──────────────────────────────────────────── */
    ESP32Logger logger;
    (void)logger.begin(115200UL);

    ESP32CryptoProvider cryptoProvider;
    ESP32Platform       platform;
    Pn532NfcTransport   nfcTransport(&nfc, logger);
    CryptnoxWallet      wallet(nfcTransport, logger, cryptoProvider, platform);

    if (!wallet.begin()) {
        ESP_LOGE(TAG, "Wallet begin (SAMConfig) failed");
        return;
    }
    /* wallet.begin() already prints the PN532 firmware version internally. */

    /* ── WiFi + RPC ────────────────────────────────────────────── */
    eth_rpc_init(RPC_URL, "0x" ADDR_FROM);
#if defined(RPC_PROJECT_ID) && defined(RPC_API_SECRET)
    eth_rpc_set_auth(RPC_PROJECT_ID, RPC_API_SECRET);
#endif

    if (!eth_rpc_wifi_connect(WIFI_SSID, WIFI_PASSWORD)) {
        ESP_LOGE(TAG, "WiFi connect failed — check config.h credentials");
        return;
    }

    ESP_LOGI(TAG, "Ready — will sign USDC transfer each card tap");

    signing_loop(wallet);
}
