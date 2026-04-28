#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "epd.h"

static const char *TAG = "epd_hello_world";

/* E-paper SPI pins (SPI3_HOST, standalone bus) */
#define EPD_PIN_MOSI   11
#define EPD_PIN_SCLK   12
#define EPD_PIN_CS     38
#define EPD_PIN_DC     40
#define EPD_PIN_RST    41
#define EPD_PIN_BUSY   42

/* 4.2" display resolution */
#define EPD_DISP_WIDTH   400
#define EPD_DISP_HEIGHT  300

/* Bits packed per byte in the framebuffer */
#define BITS_PER_BYTE  8U

/* "Hello World" text position */
#define TEXT_POS_X  20U
#define TEXT_POS_Y  56U

static uint8_t image_buf[EPD_DISP_WIDTH / BITS_PER_BYTE * EPD_DISP_HEIGHT];

void app_main(void) {
    epd_config_t cfg = {
        .spi_host      = SPI3_HOST,
        .pin_mosi      = EPD_PIN_MOSI,
        .pin_sclk      = EPD_PIN_SCLK,
        .pin_cs        = EPD_PIN_CS,
        .pin_dc        = EPD_PIN_DC,
        .pin_rst       = EPD_PIN_RST,
        .pin_busy      = EPD_PIN_BUSY,
        .skip_bus_init = false,
    };

    esp_err_t ret = epd_io_init(&cfg);

    if (ret == ESP_OK) {
        epd_set_panel((uint8_t)EPD420,
                      (uint16_t)EPD_DISP_WIDTH, (uint16_t)EPD_DISP_HEIGHT);
        epd_paint_newimage(image_buf,
                           (uint16_t)EPD_DISP_WIDTH, (uint16_t)EPD_DISP_HEIGHT,
                           (uint16_t)EPD_ROTATE_0, (uint16_t)EPD_COLOR_WHITE);
        epd_paint_clear((uint16_t)EPD_COLOR_WHITE);
        epd_paint_showString((uint16_t)TEXT_POS_X, (uint16_t)TEXT_POS_Y,
                             (uint8_t *)"Hello World",
                             (uint16_t)EPD_FONT_SIZE24x12, (uint16_t)EPD_COLOR_BLACK);

        if (epd_init() == 0U) {
            epd_displayBW(image_buf);
            epd_enter_deepsleepmode(EPD_DEEPSLEEP_MODE1);
            ESP_LOGI(TAG, "Hello World displayed on e-paper");
        } else {
            ESP_LOGE(TAG, "EPD panel init failed (busy timeout)");
        }
    } else {
        ESP_LOGE(TAG, "EPD SPI init failed: %s", esp_err_to_name(ret));
    }
}
