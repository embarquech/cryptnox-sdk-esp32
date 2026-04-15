#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "pn532.h"

static const char *TAG = "main";

// ESP32-S3 SPI pins — adjust to match your wiring
#define PIN_MOSI  11
#define PIN_MISO  13
#define PIN_SCLK  12
#define PIN_CS    10

void app_main(void)
{
    pn532_t nfc;
    pn532_config_t config = {
        .spi_host = SPI2_HOST,
        .pin_mosi = PIN_MOSI,
        .pin_miso = PIN_MISO,
        .pin_sclk = PIN_SCLK,
        .pin_cs   = PIN_CS,
    };

    esp_err_t ret = pn532_init(&nfc, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init PN532: %s", esp_err_to_name(ret));
        return;
    }

    uint32_t version = pn532_get_firmware_version(&nfc);
    if (!version) {
        ESP_LOGE(TAG, "PN532 not found");
        return;
    }
    ESP_LOGI(TAG, "PN5%02X firmware v%d.%d (supports 0x%02X)",
             (version >> 24) & 0xFF,
             (version >> 16) & 0xFF,
             (version >> 8) & 0xFF,
             version & 0xFF);

    if (!pn532_sam_config(&nfc)) {
        ESP_LOGE(TAG, "SAMConfig failed");
        return;
    }
    ESP_LOGI(TAG, "Waiting for RFID tag...");

    while (1) {
        uint32_t uid = pn532_read_passive_target_id(&nfc, PN532_MIFARE_ISO14443A);
        if (uid != 0) {
            ESP_LOGI(TAG, "Tag detected! UID: 0x%08lX", (unsigned long)uid);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
