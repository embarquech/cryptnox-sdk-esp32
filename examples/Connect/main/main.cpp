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

static const char *const TAG = "connect";
static const uint32_t LOOP_DELAY_MS = 1000U;

#define SPI_MOSI            11
#define SPI_MISO            13
#define SPI_SCLK            12
#define SPI_MAX_TRANSFER_SZ 4096
#define SPI_PIN_UNUSED      (-1)
#define NFC_CS              10

static void run_connect_loop(CryptnoxWallet &wallet)
{
    while (true) {
        CW_SecureSession session{};
        bool connected = wallet.connect(session);

        if (connected) {
            ESP_LOGI(TAG, "Card connected, secure channel established");

            CW_CardInfo info{};
            bool infoOk = wallet.getCardInfo(session, &info);
            if (infoOk) {
                ESP_LOGI(TAG, "Owner name : %s", info.name);
                ESP_LOGI(TAG, "Owner email: %s", info.email);
            } else {
                ESP_LOGW(TAG, "getCardInfo failed");
            }
        } else {
            ESP_LOGW(TAG, "Card not detected or secure channel failed");
        }

        wallet.disconnect(session);
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
            (void)nfcTransport.printFirmwareVersion();
            run_connect_loop(wallet);
        } else {
            ESP_LOGE(TAG, "Wallet init failed");
        }
    } else {
        ESP_LOGE(TAG, "PN532 init failed");
    }
}
