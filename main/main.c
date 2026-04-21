#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "epd.h"
#include "logo.h"

static const char *TAG = "main";

#define SPI_MOSI  11
#define SPI_MISO  13
#define SPI_SCLK  12

#define EPD_PIN_CS    38
#define EPD_PIN_DC    40
#define EPD_PIN_RST   41
#define EPD_PIN_BUSY  42

// WeAct 1.54" B/W (SSD1681): 200x200
#define EPD_WIDTH  200
#define EPD_HEIGHT 200
#define EPD_STRIDE (EPD_WIDTH / 8)

static uint8_t image_bw[EPD_STRIDE * EPD_HEIGHT];

static void __attribute__((unused)) draw_logo(void)
{
    uint16_t off_x_px = (EPD_WIDTH - LOGO_H) / 2;
    uint16_t off_y    = (EPD_HEIGHT - LOGO_W) / 2;

    for (uint16_t sy = 0; sy < LOGO_H; sy++) {
        for (uint16_t sx = 0; sx < LOGO_W; sx++) {
            uint8_t src_byte = logo_cryptnox[sy * (LOGO_W / 8) + sx / 8];
            uint8_t src_bit = (src_byte >> (7 - (sx % 8))) & 1;
            uint16_t dx = sy;
            uint16_t dy = (LOGO_W - 1) - sx;
            uint8_t pixel = src_bit ? 0 : 1;
            uint16_t bx = off_x_px + dx;
            uint16_t by = off_y + dy;
            uint32_t addr = by * EPD_STRIDE + bx / 8;
            if (pixel)
                image_bw[addr] |= (0x80 >> (bx % 8));
            else
                image_bw[addr] &= ~(0x80 >> (bx % 8));
        }
    }
}

void app_main(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = SPI_MOSI,
        .miso_io_num = SPI_MISO,
        .sclk_io_num = SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

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
    epd_set_panel(EPD154, EPD_WIDTH, EPD_HEIGHT);

    // Init buffer to white background (ROTATE_270 = identity: paint x,y = memory x,y)
    epd_paint_newimage(image_bw, EPD_WIDTH, EPD_HEIGHT, EPD_ROTATE_270, EPD_COLOR_WHITE);
    epd_paint_clear(EPD_COLOR_WHITE);

    if (epd_init()) {
        ESP_LOGE(TAG, "EPD init failed (busy timeout?)");
        return;
    }
    epd_displayBW(image_bw);
    ESP_LOGI(TAG, "EPD: white background set");

    epd_init_partial();

    // Clock box centered on 200×200: HH:MM:SS font 24×12 → 96 × 24
    const uint16_t clk_x = (EPD_WIDTH - 96) / 2;   // 52
    const uint16_t clk_y = (EPD_HEIGHT - 24) / 2;  // 88
    const uint16_t clk_w = 96, clk_h = 24;

    uint32_t seconds = 0;
    char time_buf[16];

    while (1) {
        uint32_t h = (seconds / 3600) % 24;
        uint32_t m = (seconds / 60) % 60;
        uint32_t s = seconds % 60;
        snprintf(time_buf, sizeof(time_buf), "%02lu:%02lu:%02lu",
                 (unsigned long)h, (unsigned long)m, (unsigned long)s);

        // Erase the clock box to white, draw black digits
        epd_paint_drawRectangle(clk_x, clk_y, clk_x + clk_w, clk_y + clk_h,
                                EPD_COLOR_WHITE, 1);
        epd_paint_showString(clk_x, clk_y, (uint8_t *)time_buf,
                             EPD_FONT_SIZE24x12, EPD_COLOR_BLACK);

        // Every 30s: fast refresh (cmd 0xC7) to clear ghosting with reduced flash
        if (seconds > 0 && seconds % 30 == 0) {
            epd_init_fast();
            epd_displayBW_fast(image_bw);
            epd_init_partial();
        } else {
            epd_displayBW_partial(image_bw);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
        seconds++;
    }
}
