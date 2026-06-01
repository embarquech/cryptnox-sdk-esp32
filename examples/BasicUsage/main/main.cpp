/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file main.cpp
 * @example BasicUsage/main/main.cpp
 * @brief BasicUsage ESP32 example: initialise PN532, connect to a Cryptnox card, and sign a hash.
 *
 * This is the ESP32-IDF port of the Arduino @c BasicUsage.ino sketch.
 * It demonstrates the complete wallet flow with minimal code:
 *   1. Initialise the PN532 NFC reader (SPI or I²C — selected below).
 *   2. Connect to a Cryptnox card and establish the secure channel.
 *   3. Sign a 32-byte test hash and print the raw r‖s bytes.
 *   4. Securely wipe sensitive buffers.
 *
 * Select the communication interface by setting @ref SPI_ENABLED / @ref I2C_ENABLED
 * at the top of this file.
 *
 * @note The card must have a seed loaded before signing will work.
 *       Use the Cryptnox CLI: @c cryptnox @c seed @c generate.
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
#include "CW_Utils.h"
#include "config.h"

/* ============================================================================
 * 1. Interface Selection
 * --------------------------------------------------------------------------
 * Set exactly ONE flag to 1 and the other to 0.
 * ========================================================================= */
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

static const char *const TAG           = "basic_usage";
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

/* ============================================================================
 * 2. Default PIN
 * --------------------------------------------------------------------------
 * Must match the PIN set on the card (4–9 ASCII digits).
 * ========================================================================= */
/** @brief Demo PIN — replace with the PIN used during card initialisation. */
static const uint8_t DEFAULT_PIN[]    = "000000000";
static const size_t  DEFAULT_PIN_LEN  = sizeof(DEFAULT_PIN) - 1U;

/******************************************************************
 * Main loop
 ******************************************************************/

/**
 * @brief Main application loop: connect, sign a test hash, wipe buffers, disconnect.
 *
 * Each iteration establishes the secure channel, signs a 32-byte test hash,
 * prints the raw r‖s bytes, securely wipes sensitive buffers, then
 * disconnects.  Halts permanently on @ref CW_SIGN_PIN_INCORRECT to protect
 * the card's retry counter.
 *
 * @param[in] wallet Initialised wallet instance.
 */
static void run_basic_usage_loop(CryptnoxWallet &wallet)
{
    bool keep_running = true;

    while (keep_running) {
        /* ── Step 1: Connect to card and establish secure channel ── */
        CW_SecureSession session{};

        if (!wallet.connect(session)) {
            ESP_LOGW(TAG, "Card not detected — hold card to reader");
            vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
            continue;
        }

        ESP_LOGI(TAG, "Card connected and secure channel established");

        /* ── Step 2: Sign a test hash ──────────────────────────── */
        /* NOTE: Card must have a seed loaded before signing works.
         * Use the Cryptnox CLI: cryptnox seed generate */
        ESP_LOGI(TAG, "Signing test hash...");

        uint8_t testHash[CW_HASH_SIZE];
        (void)memset(testHash, 0x01, sizeof(testHash));

        /* Build sign request — PIN included in sign payload for
         * authentication.  Alternatively call verifyPin() first
         * and use CW_SIGN_PINLESS_K1 here. */
        CW_SignRequest signRequest(session,
                                   CW_SIGN_CURR_K1,
                                   CW_SIGN_SIG_ECDSA_LOW_S,
                                   CW_SIGN_WITH_PIN);
        signRequest.hash       = testHash;
        signRequest.hashLength = static_cast<uint8_t>(CW_HASH_SIZE);
        (void)CW_Utils::safe_memcpy(signRequest.pin, sizeof(signRequest.pin),
                                    DEFAULT_PIN, DEFAULT_PIN_LEN);

        CW_SignResult signResult = wallet.sign(signRequest);

        if (signResult.errorCode == CW_OK) {
            ESP_LOGI(TAG, "Signature received (64 bytes raw r||s)");

            /* Print first 8 bytes of R for a quick visual check */
            ESP_LOG_BUFFER_HEX_LEVEL(TAG,
                &signResult.signature[CW_SIG_R_OFFSET], 8U, ESP_LOG_INFO);
            ESP_LOGI(TAG, "s:");
            ESP_LOG_BUFFER_HEX_LEVEL(TAG,
                &signResult.signature[CW_SIG_S_OFFSET], 8U, ESP_LOG_INFO);

            ESP_LOGI(TAG, "Card processed successfully");

        } else if (signResult.errorCode == CW_SIGN_PIN_INCORRECT) {
            /* CRITICAL: do NOT retry — each wrong PIN decrements the
             * card's retry counter.  Reaching 0 permanently blocks
             * the PIN.  Fix DEFAULT_PIN before flashing again. */
            ESP_LOGE(TAG, "Wrong PIN — halting to protect retry counter");
            CW_Utils::secure_wipe(testHash,            sizeof(testHash));
            CW_Utils::secure_wipe(signResult.signature, sizeof(signResult.signature));
            wallet.disconnect(session);
            keep_running = false;
            continue;

        } else {
            ESP_LOGE(TAG, "Sign failed: errorCode = 0x%02X",
                     static_cast<unsigned int>(signResult.errorCode));
        }

        /* ── Step 3: Securely wipe sensitive buffers ───────────── */
        CW_Utils::secure_wipe(testHash,            sizeof(testHash));
        CW_Utils::secure_wipe(signResult.signature, sizeof(signResult.signature));

        /* Always disconnect to reset the reader for the next tap. */
        wallet.disconnect(session);

        vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
    }

    /* Halt after a fatal error (wrong PIN). */
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(LOOP_DELAY_MS));
    }
}

/******************************************************************
 * Entry point
 ******************************************************************/

/**
 * @brief ESP-IDF application entry point.
 *
 * Initialises NVS, starts Wi-Fi for full TRNG entropy, brings up the SPI
 * or I²C bus and PN532 reader, then enters @ref run_basic_usage_loop.
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

    pn532_t        nfc     = {};
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
    nfc_cfg.transport    = PN532_TRANSPORT_I2C;
    nfc_cfg.i2c_port     = PN532_I2C_PORT;
    nfc_cfg.pin_sda      = PN532_SDA;
    nfc_cfg.pin_scl      = PN532_SCL;
    nfc_cfg.pin_irq      = PN532_IRQ;
    nfc_cfg.pin_rst      = PN532_RST;
    nfc_cfg.i2c_clock_hz = PN532_I2C_HZ;
#endif

    esp_err_t nfc_ret = pn532_init(&nfc, &nfc_cfg);
    if (nfc_ret != ESP_OK) {
        ESP_LOGE(TAG, "PN532 init failed — check wiring and interface selection");
        return;
    }

    ESP32Logger          logger;
    (void)logger.begin(115200UL);

    ESP32CryptoProvider  cryptoProvider;
    ESP32Platform        platform;
    Pn532NfcTransport    nfcTransport(&nfc, logger);
    CryptnoxWallet       wallet(nfcTransport, logger, cryptoProvider, platform);

    if (!wallet.begin()) {
        ESP_LOGE(TAG, "Wallet init failed (SAMConfig)");
        return;
    }

    /* Print PN532 firmware version as a sanity check. */
    (void)nfcTransport.printFirmwareVersion();

    ESP_LOGI(TAG, "Ready — hold Cryptnox card to reader");

    run_basic_usage_loop(wallet);
}
