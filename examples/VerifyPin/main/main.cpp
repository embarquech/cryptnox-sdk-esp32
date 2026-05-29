/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file main.cpp
 * @example VerifyPin/main/main.cpp
 * @brief Minimal Cryptnox ESP32 example: verify the card PIN over a secure channel.
 *
 * Wiring & prerequisites:
 *   - PN532 NFC reader on SPI: MOSI=11, MISO=13, SCLK=12, CS=10.
 *   - A Cryptnox card initialised with a known PIN.
 *   - @ref DEMO_PIN must match the PIN set on the card.
 *
 * What the firmware does in each loop iteration:
 *   1. Connect to the card and establish the secure channel.
 *   2. Submit the PIN via @ref CryptnoxWallet::verifyPin.
 *   3. On success print "PIN accepted"; on failure halt immediately to
 *      protect the card's retry counter.
 *
 * @warning On PIN rejection the firmware enters an infinite halt.  Each
 *          wrong PIN decrements the card's retry counter; reaching 0
 *          permanently blocks the PIN and requires the PUK to unblock.
 *          Fix @ref DEMO_PIN before flashing again.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "pn532.h"
#include "CryptnoxWallet.h"
#include "Pn532NfcTransport.h"
#include "ESP32Logger.h"
#include "esp32_crypto_provider.h"
#include "ESP32Platform.h"
#include "config.h"

static const char *const TAG           = "verify_pin";
static const uint32_t    LOOP_DELAY_MS  = 1000U;
static const uint32_t    WIFI_TIMEOUT_MS = 10000U;
static const int         WIFI_MAX_RETRY  = 5;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int                s_retry_num = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
        esp_wifi_connect();
    } else if ((event_base == WIFI_EVENT) &&
               (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else {
        /* other events ignored */
    }
}

static void wifi_start(void)
{
    s_wifi_event_group = xEventGroupCreate();
    s_retry_num = 0;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    (void)esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t h_any;
    esp_event_handler_instance_t h_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &h_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &h_ip));
    wifi_config_t wifi_cfg;
    (void)memset(&wifi_cfg, 0, sizeof(wifi_cfg));
    (void)strncpy((char *)wifi_cfg.sta.ssid,     WIFI_SSID,
                  sizeof(wifi_cfg.sta.ssid)     - 1U);
    (void)strncpy((char *)wifi_cfg.sta.password, WIFI_PASSWORD,
                  sizeof(wifi_cfg.sta.password) - 1U);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_TIMEOUT_MS));
    if ((bits & WIFI_CONNECTED_BIT) != 0U) {
        ESP_LOGI(TAG, "WiFi connected");
    } else {
        ESP_LOGW(TAG, "WiFi connect failed — TRNG entropy may be reduced");
    }
    (void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, h_ip);
    (void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, h_any);
    vEventGroupDelete(s_wifi_event_group);
}

#define SPI_MOSI            11
#define SPI_MISO            13
#define SPI_SCLK            12
#define SPI_MAX_TRANSFER_SZ 4096
#define SPI_PIN_UNUSED      (-1)
#define NFC_CS              10

static void run_verify_pin_loop(CryptnoxWallet &wallet)
{
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
            bool pinOk = wallet.verifyPin(session,
                                          DEMO_PIN,
                                          static_cast<uint8_t>(CW_MAX_PIN_LENGTH));
            if (pinOk) {
                ESP_LOGI(TAG, "PIN accepted");
            } else {
                /* CRITICAL: do NOT retry — each wrong PIN decrements the card's
                 * retry counter. Reaching 0 permanently blocks the PIN.
                 * Fix DEMO_PIN before flashing again. */
                ESP_LOGE(TAG, "PIN rejected — halting to protect retry counter");
                keep_running = false;
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
    esp_err_t nvs_ret = nvs_flash_init();
    if ((nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_ret);
    wifi_start();

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
            run_verify_pin_loop(wallet);
        } else {
            ESP_LOGE(TAG, "Wallet init failed");
        }
    } else {
        ESP_LOGE(TAG, "PN532 init failed");
    }
}
