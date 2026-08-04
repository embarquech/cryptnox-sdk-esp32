/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file main.cpp
 * @example TronSigning/main/main.cpp
 * @brief Cryptnox ESP32 example: sign and broadcast a native TRX transfer on TRON.
 *
 * Wiring & prerequisites:
 *   - PN532 NFC reader — transport selected by @c SPI_ENABLED / @c I2C_ENABLED
 *     at the top of this file.
 *   - A Cryptnox card initialised with a seed and a known PIN.
 *   - @c config.h filled in with Wi-Fi, TRON node URL, and TRON addresses
 *     (copy from @c config.template.h and fill in the values).
 *
 * What the firmware does in each loop iteration:
 *   1. Wait for a card tap and establish the secure channel.
 *   2. Ask the TRON full node to build an unsigned TransferContract
 *      (@c /wallet/createtransaction).
 *   3. **Verify the returned transaction locally** before signing:
 *      @c txID must equal @c sha256(raw_data_hex), and @c raw_data must
 *      carry the expected sender, recipient, and amount.
 *   4. Sign @c txID on the card over @c m/44'/195'/0'/0/0
 *      (@ref CW_TRON_DERIVE_PATH).
 *   5. Broadcast @c r||s||v (@c /wallet/broadcasttransaction), retrying with
 *      the other parity bit if the node reports a signature error.
 *
 * @note Step 3 is what makes it safe to let a remote node serialise the
 *       transaction: a node that swaps the recipient or the amount cannot get
 *       a signature out of the card, because the bytes are checked against
 *       @c config.h before the SIGN APDU is sent.
 *
 * @note TRON's recovery id cannot be derived without an ecrecover, which no
 *       adapter provides — so the firmware tries @c v=0 and falls back to
 *       @c v=1. A wrong parity is rejected by the node (@c SIGERROR) and
 *       commits nothing.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "CryptnoxWallet.h"
#include "CW_Tron.h"
#include "CW_Utils.h"
#include "Pn532NfcTransport.h"
#include "ESP32Logger.h"
#include "esp32_crypto_provider.h"
#include "ESP32Platform.h"

extern "C" {
#include "pn532.h"
#include "tron_api.h"
#include "tron_verify.h"
}

#include "config.h"

static_assert(TRON_ADDR_RAW_BYTES == CW_TRON_ADDRESS_BYTES,
              "tron_verify and CW_Tron must agree on the raw address length");
static_assert(TRON_SIGNATURE_BYTES == (CW_RAW_SIGNATURE_SIZE + 1U),
              "TRON signature is the card's r||s plus one recovery byte");

static const char *const TAG = "tron_signing";

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

/* ── I²C wiring — Keyestudio ESP32-S3 + Keyestudio PN532 ─────────
 * NOTE: GPIO 26–32 are taken by the S3's flash / PSRAM, so the
 * ESP32-classic pins (SDA 27 / SCL 22) cannot be used here. */
#if I2C_ENABLED
#define PN532_I2C_PORT      0          /* I2C_NUM_0 */
#define PN532_SDA           8
#define PN532_SCL           9
#define PN532_IRQ           (-1)       /* unused */
#define PN532_RST           (-1)       /* unused */
#define PN532_I2C_HZ        100000U
#endif

/* Buffers for the node's transaction JSON and its decoded raw_data. */
#define TX_JSON_SIZE        2048U
#define RAW_DATA_MAX        256U
#define HEX_STR_MAX         ((RAW_DATA_MAX * 2U) + 1U)

/* Number of signature parity candidates to try on broadcast. */
#define PARITY_CANDIDATES   (2U)

/******************************************************************
 * Local transaction verification
 ******************************************************************/

/**
 * @brief Extract raw_data_hex / txID, verify both, and return the digest to sign.
 *
 * @param[out] hash 32-byte buffer receiving the verified txID.
 * @return true when the transaction is safe to sign.
 */
static bool verify_transaction(const char *tx_json, CW_CryptoProvider &crypto,
                               const uint8_t *owner21, const uint8_t *to21,
                               uint64_t amount, uint8_t *hash)
{
    char raw_hex[HEX_STR_MAX];
    char txid_hex[(CW_HASH_SIZE * 2U) + 1U];

    if (!tron_api_json_string(tx_json, "raw_data_hex", raw_hex, sizeof(raw_hex)) ||
        !tron_api_json_string(tx_json, "txID", txid_hex, sizeof(txid_hex))) {
        ESP_LOGE(TAG, "transaction is missing raw_data_hex / txID");
        return false;
    }

    uint8_t raw[RAW_DATA_MAX];
    size_t  raw_len = 0U;
    if (!tron_api_hex_to_bytes(raw_hex, raw, sizeof(raw), &raw_len)) {
        ESP_LOGE(TAG, "raw_data_hex is not valid hex (or too long)");
        return false;
    }

    uint8_t txid[CW_HASH_SIZE];
    size_t  txid_len = 0U;
    if (!tron_api_hex_to_bytes(txid_hex, txid, sizeof(txid), &txid_len) ||
        (txid_len != CW_HASH_SIZE)) {
        ESP_LOGE(TAG, "txID is not a 32-byte hex string");
        return false;
    }

    /* txID must be the SHA-256 of the bytes we are about to sign — otherwise
     * the node showed us one transaction and asked us to sign another. */
    uint8_t digest[CW_HASH_SIZE];
    if (!crypto.sha256(raw, raw_len, digest)) {
        ESP_LOGE(TAG, "SHA-256 failed");
        return false;
    }
    if (!CW_Utils::secure_compare(digest, txid, CW_HASH_SIZE)) {
        ESP_LOGE(TAG, "txID does not match sha256(raw_data) — refusing to sign");
        return false;
    }

    const char *reason = tron_verify_transfer(raw, raw_len, owner21, to21, amount);
    if (reason != NULL) {
        ESP_LOGE(TAG, "raw_data: %s — refusing to sign", reason);
        return false;
    }

    (void)CW_Utils::safe_memcpy(hash, CW_HASH_SIZE, digest, CW_HASH_SIZE);
    return true;
}

/******************************************************************
 * Signing loop
 ******************************************************************/

/**
 * @brief Main application loop: sign and broadcast a TRX transfer each card tap.
 *
 * @param[in] wallet  Initialised wallet instance.
 * @param[in] crypto  Crypto provider (SHA-256 for txID verification).
 * @param[in] owner21 Raw 21-byte sender address (validated at startup).
 * @param[in] to21    Raw 21-byte recipient address (validated at startup).
 */
static void signing_loop(CryptnoxWallet &wallet, CW_CryptoProvider &crypto,
                         const uint8_t *owner21, const uint8_t *to21)
{
    /* CARD_PIN is a string literal ("000000000"); copy into the pin array. */
    uint8_t card_pin[CW_MAX_PIN_LENGTH] = {};
    const size_t pin_len = (CARD_PIN_LEN < CW_MAX_PIN_LENGTH) ? CARD_PIN_LEN
                                                              : CW_MAX_PIN_LENGTH;
    (void)CW_Utils::safe_memcpy(card_pin, sizeof(card_pin),
                                reinterpret_cast<const uint8_t *>(CARD_PIN),
                                pin_len);

    while (true) {
        /* ── 1. Wait for card ──────────────────────────────────── */
        ESP_LOGI(TAG, "Hold Cryptnox card to reader to sign...");

        CW_SecureSession session;
        if (!wallet.connect(session)) {
            vTaskDelay(pdMS_TO_TICKS(100U));
            continue;
        }

        /* ── 2. Let the node build the unsigned transfer ────────── */
        char tx_json[TX_JSON_SIZE];
        if (!tron_api_create_transfer(TRON_ADDR_FROM, TRON_ADDR_TO,
                                      AMOUNT_SUN, tx_json, sizeof(tx_json))) {
            ESP_LOGE(TAG, "createtransaction failed — retrying in 5 s");
            wallet.disconnect(session);
            vTaskDelay(pdMS_TO_TICKS(5000U));
            continue;
        }

        /* ── 3. Verify it before the card ever sees it ──────────── */
        uint8_t hash[CW_HASH_SIZE];
        if (!verify_transaction(tx_json, crypto, owner21, to21,
                                AMOUNT_SUN, hash)) {
            wallet.disconnect(session);
            vTaskDelay(pdMS_TO_TICKS(5000U));
            continue;
        }

        ESP_LOGI(TAG, "Verified txID / hash to sign:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, hash, CW_HASH_SIZE, ESP_LOG_INFO);

        /* ── 4. Sign on the card over m/44'/195'/0'/0/0 ─────────── */
        CW_SignRequest req(session,
                           CW_SIGN_DERIVE_K1,
                           CW_SIGN_SIG_ECDSA_LOW_S,
                           CW_SIGN_WITH_PIN);
        req.hash             = hash;
        req.hashLength       = static_cast<uint8_t>(CW_HASH_SIZE);
        req.derivePath       = CW_TRON_DERIVE_PATH;
        req.derivePathLength = static_cast<uint8_t>(CW_TRON_PATH_LENGTH);
        (void)CW_Utils::safe_memcpy(req.pin, sizeof(req.pin),
                                    card_pin, CW_MAX_PIN_LENGTH);

        CW_SignResult result = wallet.sign(req);
        wallet.disconnect(session);

        if (result.errorCode != CW_OK) {
            ESP_LOGE(TAG, "Sign failed: 0x%02X",
                     static_cast<unsigned int>(result.errorCode));
            vTaskDelay(pdMS_TO_TICKS(2000U));
            continue;
        }

        /* ── 5. Broadcast r||s||v, trying both parities ─────────── */
        uint8_t signature[TRON_SIGNATURE_BYTES];
        (void)CW_Utils::safe_memcpy(signature, sizeof(signature),
                                    result.signature, CW_RAW_SIGNATURE_SIZE);

        bool broadcast = false;
        for (uint8_t v = 0U; (v < PARITY_CANDIDATES) && !broadcast; v++) {
            signature[CW_RAW_SIGNATURE_SIZE] = v;

            char resp[512] = {};
            broadcast = tron_api_broadcast(tx_json, signature, resp, sizeof(resp));
            if (broadcast) {
                ESP_LOGI(TAG, "TX broadcast OK (v=%u): %s",
                         static_cast<unsigned int>(v), resp);
            } else {
                ESP_LOGW(TAG, "Broadcast rejected with v=%u: %s",
                         static_cast<unsigned int>(v), resp);
            }
        }

        if (!broadcast) {
            ESP_LOGE(TAG, "TX broadcast failed with both parity bits");
        }

        /* Wait before the next iteration so the transfer can confirm. */
        vTaskDelay(pdMS_TO_TICKS(15000U));
    }
}

/******************************************************************
 * Entry point
 ******************************************************************/

/**
 * @brief ESP-IDF application entry point.
 *
 * Initialises NVS, brings up the PN532 reader, validates the configured TRON
 * addresses, connects to Wi-Fi, then enters @ref signing_loop.
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
    pn532_t nfc = {};
    pn532_config_t nfc_cfg = {};
#if SPI_ENABLED
    spi_bus_config_t buscfg = {};
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

    if (pn532_init(&nfc, &nfc_cfg) != ESP_OK) {
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

    /* ── Validate the configured addresses once, up front ───────── */
    uint8_t owner21[CW_TRON_ADDRESS_BYTES];
    uint8_t to21[CW_TRON_ADDRESS_BYTES];
    if (!CW_Tron::decodeAddress(TRON_ADDR_FROM, cryptoProvider, owner21)) {
        ESP_LOGE(TAG, "TRON_ADDR_FROM is not a valid TRON address");
        return;
    }
    if (!CW_Tron::decodeAddress(TRON_ADDR_TO, cryptoProvider, to21)) {
        ESP_LOGE(TAG, "TRON_ADDR_TO is not a valid TRON address");
        return;
    }

    /* ── WiFi + node ───────────────────────────────────────────── */
    tron_api_init(TRON_NODE_URL, TRON_API_KEY);

    if (!tron_api_wifi_connect(WIFI_SSID, WIFI_PASSWORD)) {
        ESP_LOGE(TAG, "WiFi connect failed — check config.h credentials");
        return;
    }

    ESP_LOGI(TAG, "Ready — will send %" PRIu64 " SUN from %s to %s on each card tap",
             (uint64_t)AMOUNT_SUN, TRON_ADDR_FROM, TRON_ADDR_TO);

    signing_loop(wallet, cryptoProvider, owner21, to21);
}
