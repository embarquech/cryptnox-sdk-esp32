/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file main.cpp
 * @example Sign/main/main.cpp
 * @brief Minimal Cryptnox ESP32 example: sign a 32-byte hash on the secp256k1 curve.
 *
 * Wiring & prerequisites:
 *   - PN532 NFC reader on SPI: MOSI=11, MISO=13, SCLK=12, CS=10.
 *   - A Cryptnox card initialised with a known PIN and a loaded seed
 *     (use the Cryptnox CLI: @c cryptnox @c initialize then
 *     @c cryptnox @c seed @c generate).
 *   - @ref DEMO_PIN must match the PIN set on the card.
 *   - @c config.h filled in with @ref WIFI_SSID and @ref WIFI_PASSWORD.
 *
 * What the firmware does in each loop iteration:
 *   1. Connect to the card and establish the secure channel.
 *   2. Sign a 32-byte test hash on the secp256k1 curve (key type
 *      @ref CW_SIGN_CURR_K1, signature type @ref CW_SIGN_SIG_ECDSA_LOW_S,
 *      PIN included in the sign payload via @ref CW_SIGN_WITH_PIN).
 *   3. Print the raw r‖s signature bytes, wipe sensitive buffers, disconnect.
 *
 * @warning On @ref CW_SIGN_PIN_INCORRECT the firmware enters an infinite
 *          halt: every wrong PIN attempt decrements the card's retry counter
 *          and reaching 0 permanently blocks the PIN.  Verify @ref DEMO_PIN
 *          matches the card before flashing.
 *
 * @note The hash filled with 0x01 is a test pattern.  In real use replace it
 *       with the SHA-256 (or Keccak-256 for Ethereum) digest of the
 *       transaction you want the card to sign.
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

/* ── SPI wiring — ESP32-S3 dev kit + Keyestudio PN532 breakout ──── */
#define SPI_MOSI            11
#define SPI_MISO            13
#define SPI_SCLK            12
#define SPI_MAX_TRANSFER_SZ 4096
#define SPI_PIN_UNUSED      (-1)
#define NFC_CS              10

static const char *const TAG           = "sign";
static const uint32_t    LOOP_DELAY_MS  = 1000U;
static const uint32_t    WIFI_TIMEOUT_MS = 10000U;
static const int         WIFI_MAX_RETRY  = 5;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int                s_retry_num = 0;

/**
 * @brief FreeRTOS event handler driving the Wi-Fi station state machine.
 *
 * Handles @c WIFI_EVENT_STA_START (triggers connection),
 * @c WIFI_EVENT_STA_DISCONNECTED (retries up to @c WIFI_MAX_RETRY times),
 * and @c IP_EVENT_STA_GOT_IP (signals success via the event group).
 *
 * @param[in] arg        Unused.
 * @param[in] event_base Event base (@c WIFI_EVENT or @c IP_EVENT).
 * @param[in] event_id   Event identifier within @p event_base.
 * @param[in] event_data Event-specific data (unused).
 */
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

/**
 * @brief Initialise Wi-Fi station mode and block until connected or timeout.
 *
 * Starts the ESP Wi-Fi stack, registers @ref wifi_event_handler, configures
 * the SSID and password from @ref WIFI_SSID / @ref WIFI_PASSWORD in
 * @c config.h, then waits up to @c WIFI_TIMEOUT_MS for an IP address.
 * The radio must be active before crypto operations so the hardware TRNG
 * operates with full entropy (SEC-001).
 *
 * @note If the connection fails the function logs a warning and returns
 *       normally; the firmware continues with reduced TRNG entropy.
 */
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

/**
 * @brief Main application loop: connect, sign a test hash, and disconnect.
 *
 * Each iteration establishes the secure channel, signs a 32-byte test hash
 * on the secp256k1 curve via @ref CryptnoxWallet::sign, then disconnects.
 * Halts permanently on @ref CW_SIGN_PIN_INCORRECT to protect the card's
 * retry counter.
 *
 * @param[in] wallet Initialised wallet instance.
 */
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
            (void)CW_Utils::safe_memcpy(req.pin, sizeof(req.pin),
                                        DEMO_PIN, CW_MAX_PIN_LENGTH);

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

/**
 * @brief ESP-IDF application entry point.
 *
 * Initialises NVS, starts Wi-Fi for full TRNG entropy, brings up the SPI
 * bus and PN532 reader, then enters @ref run_sign_loop.
 */
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

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num     = SPI_MOSI;
    buscfg.miso_io_num     = SPI_MISO;
    buscfg.sclk_io_num     = SPI_SCLK;
    buscfg.quadwp_io_num   = SPI_PIN_UNUSED;
    buscfg.quadhd_io_num   = SPI_PIN_UNUSED;
    buscfg.max_transfer_sz = SPI_MAX_TRANSFER_SZ;
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    pn532_t nfc = {};
    pn532_config_t nfc_cfg = {};
    nfc_cfg.spi_host      = SPI2_HOST;
    nfc_cfg.pin_cs        = NFC_CS;
    nfc_cfg.skip_bus_init = true;
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
