/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "pn532.h"
#include "CryptnoxWallet.h"
#include "Pn532NfcTransport.h"
#include "ESP32Logger.h"
#include "esp32_crypto_provider.h"
#include "ESP32Platform.h"

static const char *const TAG = "sign";
static const uint32_t LOOP_DELAY_MS = 1000U;

#define SPI_MOSI            11
#define SPI_MISO            13
#define SPI_SCLK            12
#define SPI_MAX_TRANSFER_SZ 4096
#define SPI_PIN_UNUSED      (-1)
#define NFC_CS              10

static void run_sign_loop(CryptnoxWallet &wallet)
{
    /* Replace with the SHA-256 (or Keccak-256) digest of the real transaction. */
    static const uint8_t TEST_HASH[CW_HASH_SIZE] = {
        0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U,
        0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U,
        0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U,
        0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U, 0x01U,
    };
    /* Replace with the PIN set on the card (4–9 ASCII digits). */
    static const uint8_t DEMO_PIN[CW_MAX_PIN_LENGTH] = {
        '0', '0', '0', '0', '0', '0', '0', '0', '0'
    };

    bool keep_running = true;
    while (keep_running) {
        CW_SecureSession session{};
        bool connected = wallet.connect(session);

        if (!connected) {
            ESP_LOGW(TAG, "Card not detected");
        }

        if (connected) {
            CW_SignRequest req(session,
                               CW_SIGN_CURR_K1,
                               CW_SIGN_SIG_ECDSA_LOW_S,
                               CW_SIGN_WITH_PIN);
            req.hash       = TEST_HASH;
            req.hashLength = static_cast<uint8_t>(CW_HASH_SIZE);
            (void)memcpy(req.pin, DEMO_PIN, CW_MAX_PIN_LENGTH);

            CW_SignResult result = wallet.sign(req);

            if (result.errorCode == CW_OK) {
                ESP_LOGI(TAG, "Sign OK - r:");
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, &result.signature[CW_SIG_R_OFFSET],
                                         CW_HASH_SIZE, ESP_LOG_INFO);
                ESP_LOGI(TAG, "s:");
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, &result.signature[CW_SIG_S_OFFSET],
                                         CW_HASH_SIZE, ESP_LOG_INFO);
            } else if (result.errorCode == CW_SIGN_PIN_INCORRECT) {
                /* CRITICAL: do NOT retry — each wrong PIN decrements the card's
                 * retry counter. Reaching 0 permanently blocks the PIN.
                 * Fix DEMO_PIN before flashing again. */
                ESP_LOGE(TAG, "Wrong PIN — halting to protect retry counter");
                keep_running = false;
            } else {
                ESP_LOGE(TAG, "Sign failed: 0x%02X",
                         static_cast<unsigned int>(result.errorCode));
            }
        }

        wallet.disconnect(session);

        if (keep_running) {
            vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
        }
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
    }
}

extern "C" void app_main(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num     = SPI_MOSI,
        .miso_io_num     = SPI_MISO,
        .sclk_io_num     = SPI_SCLK,
        .quadwp_io_num   = SPI_PIN_UNUSED,
        .quadhd_io_num   = SPI_PIN_UNUSED,
        .max_transfer_sz = SPI_MAX_TRANSFER_SZ,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    pn532_t nfc = {};
    pn532_config_t nfc_cfg = {
        .spi_host      = SPI2_HOST,
        .pin_cs        = NFC_CS,
        .skip_bus_init = true,
    };
    esp_err_t nfc_ret = pn532_init(&nfc, &nfc_cfg);

    if (nfc_ret == ESP_OK) {
        ESP32Logger logger;
        (void)logger.begin(115200UL);

        ESP32CryptoProvider cryptoProvider;
        ESP32Platform platform;
        Pn532NfcTransport nfcTransport(&nfc, logger);
        CryptnoxWallet wallet(nfcTransport, logger, cryptoProvider, platform);

        if (wallet.begin()) {
            run_sign_loop(wallet);
        } else {
            ESP_LOGE(TAG, "Wallet init failed");
        }
    } else {
        ESP_LOGE(TAG, "PN532 init failed");
    }
}
