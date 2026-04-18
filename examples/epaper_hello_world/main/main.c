#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "epaper.h"

static const char *TAG = "epaper_hello_world";

// E-paper SPI pins (SPI3_HOST) — Waveshare 2.9" V2 (SSD1680, 296x128)
// Adjust to match your wiring
#define EPD_PIN_MOSI    11
#define EPD_PIN_SCK     12
#define EPD_PIN_CS      38
#define EPD_PIN_DC      40
#define EPD_PIN_RST     41
#define EPD_PIN_BUSY    42

void app_main(void)
{
    static epaper_t epd;

    epaper_config_t cfg = {
        .spi_host = SPI3_HOST,
        .pin_mosi = EPD_PIN_MOSI,
        .pin_sck  = EPD_PIN_SCK,
        .pin_cs   = EPD_PIN_CS,
        .pin_dc   = EPD_PIN_DC,
        .pin_rst  = EPD_PIN_RST,
        .pin_busy = EPD_PIN_BUSY,
    };

    esp_err_t ret = epaper_init(&epd, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "E-paper init failed: %s", esp_err_to_name(ret));
        return;
    }

    epaper_clear(&epd, 1);
    epaper_draw_string(&epd, 20, 56, "Hello World");
    epaper_refresh(&epd);
    epaper_sleep(&epd);

    ESP_LOGI(TAG, "Hello World displayed on e-paper");
}
