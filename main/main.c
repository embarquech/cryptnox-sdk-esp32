#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "pn532.h"
#include "epd.h"
#include "logo.h"

static const char *TAG = "main";

// Shared SPI bus (MOSI=IO11, SCLK=IO12, MISO=IO13)
#define SPI_MOSI  11
#define SPI_MISO  13
#define SPI_SCLK  12

// PN532 (HAT)
#define NFC_CS    10

// E-Paper
#define EPD_PIN_CS    38
#define EPD_PIN_DC    40
#define EPD_PIN_RST   41
#define EPD_PIN_BUSY  42

// 4.2" = 400x300
#define EPD_WIDTH  400
#define EPD_HEIGHT 300

static uint8_t image_bw[EPD_WIDTH / 8 * EPD_HEIGHT];

static void draw_logo(void)
{
    uint16_t buf_stride = EPD_WIDTH / 8;
    uint16_t off_x_px = (EPD_WIDTH - LOGO_H) / 2;
    uint16_t off_y = (EPD_HEIGHT - LOGO_W) / 2;

    for (uint16_t sy = 0; sy < LOGO_H; sy++) {
        for (uint16_t sx = 0; sx < LOGO_W; sx++) {
            uint8_t src_byte = logo_cryptnox[sy * (LOGO_W / 8) + sx / 8];
            uint8_t src_bit = (src_byte >> (7 - (sx % 8))) & 1;
            uint16_t dx = sy;
            uint16_t dy = (LOGO_W - 1) - sx;
            uint8_t pixel = src_bit ? 0 : 1;
            uint16_t bx = off_x_px + dx;
            uint16_t by = off_y + dy;
            uint32_t addr = by * buf_stride + bx / 8;
            if (pixel)
                image_bw[addr] |= (0x80 >> (bx % 8));
            else
                image_bw[addr] &= ~(0x80 >> (bx % 8));
        }
    }
}

static void epd_show_uid(uint32_t uid)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "UID: %08lX", (unsigned long)uid);

    epd_paint_selectimage(image_bw);
    epd_paint_clear(EPD_COLOR_BLACK);
    draw_logo();
    epd_paint_showString(10, 10, (uint8_t *)buf, EPD_FONT_SIZE24x12, EPD_COLOR_WHITE);

    epd_init_fast();
    epd_displayBW_fast(image_bw);
    epd_enter_deepsleepmode(EPD_DEEPSLEEP_MODE1);
}

void app_main(void)
{
    // --- Init shared SPI bus ---
    spi_bus_config_t buscfg = {
        .mosi_io_num = SPI_MOSI,
        .miso_io_num = SPI_MISO,
        .sclk_io_num = SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // --- Init E-Paper ---
    epd_config_t epd_cfg = {
        .spi_host = SPI2_HOST,
        .pin_cs   = EPD_PIN_CS,
        .pin_dc   = EPD_PIN_DC,
        .pin_rst  = EPD_PIN_RST,
        .pin_busy = EPD_PIN_BUSY,
        .skip_bus_init = true,
    };
    if (epd_io_init(&epd_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "EPD init failed");
        return;
    }
    epd_set_panel(EPD420, EPD_WIDTH, EPD_HEIGHT);

    // Draw welcome screen with logo
    epd_paint_newimage(image_bw, EPD_WIDTH, EPD_HEIGHT, EPD_ROTATE_0, EPD_COLOR_WHITE);
    epd_paint_clear(EPD_COLOR_BLACK);
    draw_logo();

    if (epd_init()) {
        ESP_LOGE(TAG, "EPD init failed (busy timeout?)");
    } else {
        epd_displayBW(image_bw);
        ESP_LOGI(TAG, "EPD: logo displayed");
        epd_enter_deepsleepmode(EPD_DEEPSLEEP_MODE1);
    }

    // --- Init PN532 ---
    pn532_t nfc;
    pn532_config_t nfc_cfg = {
        .spi_host = SPI2_HOST,
        .pin_cs   = NFC_CS,
        .skip_bus_init = true,
    };
    if (pn532_init(&nfc, &nfc_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "PN532 init failed");
        return;
    }

    uint32_t version = pn532_get_firmware_version(&nfc);
    if (!version) {
        ESP_LOGE(TAG, "PN532 not found");
        return;
    }
    ESP_LOGI(TAG, "PN5%02X firmware v%d.%d",
             (version >> 24) & 0xFF,
             (version >> 16) & 0xFF,
             (version >> 8) & 0xFF);

    if (!pn532_sam_config(&nfc)) {
        ESP_LOGE(TAG, "SAMConfig failed");
        return;
    }

    ESP_LOGI(TAG, "Ready — scan a tag");

    uint32_t last_uid = 0;
    while (1) {
        uint32_t uid = pn532_read_passive_target_id(&nfc, PN532_MIFARE_ISO14443A);
        if (uid != 0 && uid != last_uid) {
            ESP_LOGI(TAG, "Tag: 0x%08lX", (unsigned long)uid);
            epd_show_uid(uid);
            last_uid = uid;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
