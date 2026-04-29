#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "epd.h"
#include "logo.h"
#include "CryptnoxUtils.h"

extern "C" {
#include "pn532.h"
}

static const char    *TAG                = "main";
static const size_t   RND_BUF_SIZE       = 16U;
static const uint32_t PN532_FW_IC_SHIFT  = 24U;
static const uint32_t PN532_FW_VER_SHIFT = 16U;
static const uint32_t PN532_FW_REV_SHIFT = 8U;
static const uint32_t BYTE_MASK          = 0xFFU;
static const uint32_t POLL_DELAY_MS      = 500U;

/* Shared SPI bus: MOSI=IO11, SCLK=IO12, MISO=IO13 */
#define SPI_MOSI             11
#define SPI_MISO             13
#define SPI_SCLK             12
#define SPI_MAX_TRANSFER_SZ  4096
#define SPI_PIN_UNUSED       (-1)

/* PN532 NFC reader */
#define NFC_CS   10

/* E-paper display pins */
#define EPD_PIN_CS    38
#define EPD_PIN_DC    40
#define EPD_PIN_RST   41
#define EPD_PIN_BUSY  42

/* 4.2" display resolution */
#define EPD_WIDTH   400
#define EPD_HEIGHT  300

/* Bitmap bit-packing constants */
#define BITS_PER_BYTE   8U
#define BYTE_MSB        0x80U

/* UID text overlay position on display */
#define UID_TEXT_X  10U
#define UID_TEXT_Y  10U

/* UID string buffer length (8 hex chars + "UID: " prefix + NUL) */
#define UID_STR_LEN  32U

static uint8_t image_bw[EPD_WIDTH / BITS_PER_BYTE * EPD_HEIGHT];

static void draw_logo(void) {
    uint16_t buf_stride = (uint16_t)(EPD_WIDTH  / BITS_PER_BYTE);
    uint16_t off_x_px   = (uint16_t)((EPD_WIDTH  - LOGO_H) / 2U);
    uint16_t off_y      = (uint16_t)((EPD_HEIGHT - LOGO_W) / 2U);

    for (uint16_t sy = 0U; sy < (uint16_t)LOGO_H; sy++) {
        for (uint16_t sx = 0U; sx < (uint16_t)LOGO_W; sx++) {
            uint32_t src_idx   = (uint32_t)sy * (uint32_t)(LOGO_W / BITS_PER_BYTE)
                                 + (uint32_t)(sx / BITS_PER_BYTE);
            uint8_t  src_byte  = logo_cryptnox[src_idx];
            uint8_t  sx_bit    = (uint8_t)(sx % BITS_PER_BYTE);
            uint8_t  shift     = (uint8_t)((BITS_PER_BYTE - 1U) - (unsigned int)sx_bit);
            uint8_t  src_bit   = (uint8_t)((src_byte >> shift) & 1U);
            bool     pix_set   = (src_bit == 0U);
            uint16_t dx        = sy;
            uint16_t dy        = (uint16_t)((uint16_t)(LOGO_W - 1U) - sx);
            uint16_t bx        = (uint16_t)(off_x_px + dx);
            uint16_t by        = (uint16_t)(off_y    + dy);
            uint32_t addr      = (uint32_t)by * (uint32_t)buf_stride
                                 + (uint32_t)(bx / BITS_PER_BYTE);
            uint8_t  bx_bit    = (uint8_t)(bx % BITS_PER_BYTE);
            uint8_t  bit_mask  = (uint8_t)(BYTE_MSB >> bx_bit);
            if (pix_set) {
                image_bw[addr] = (uint8_t)(image_bw[addr] | bit_mask);
            } else {
                image_bw[addr] = (uint8_t)(image_bw[addr]
                                           & (uint8_t)(~(unsigned int)bit_mask));
            }
        }
    }
}

static void show_uid_on_epd(uint32_t uid) {
    char uid_str[UID_STR_LEN];
    (void)memset(uid_str, 0, sizeof(uid_str));
    (void)snprintf(uid_str, sizeof(uid_str), "UID: %08lX", (unsigned long)uid);

    epd_paint_selectimage(image_bw);
    epd_paint_clear((uint16_t)EPD_COLOR_BLACK);
    draw_logo();
    epd_paint_showString((uint16_t)UID_TEXT_X, (uint16_t)UID_TEXT_Y,
                         (uint8_t *)uid_str,
                         (uint16_t)EPD_FONT_SIZE24x12, (uint16_t)EPD_COLOR_WHITE);
    (void)epd_init_fast();
    epd_displayBW_fast(image_bw);
    epd_enter_deepsleepmode(EPD_DEEPSLEEP_MODE1);
}

extern "C" void app_main(void) {
    uint8_t rnd[RND_BUF_SIZE] = { 0U };
    bool    rnd_ok = CryptnoxUtils::fill_secure_random(rnd, sizeof(rnd));
    if (rnd_ok) {
        ESP_LOGI(TAG, "TRNG: %02X%02X%02X%02X%02X%02X%02X%02X"
                      "%02X%02X%02X%02X%02X%02X%02X%02X",
                 rnd[0],  rnd[1],  rnd[2],  rnd[3],
                 rnd[4],  rnd[5],  rnd[6],  rnd[7],
                 rnd[8],  rnd[9],  rnd[10], rnd[11],
                 rnd[12], rnd[13], rnd[14], rnd[15]);
    }

    volatile uint8_t *rnd_ptr = rnd;
    for (size_t i = 0U; i < sizeof(rnd); i++) {
        rnd_ptr[i] = 0U;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num     = SPI_MOSI,
        .miso_io_num     = SPI_MISO,
        .sclk_io_num     = SPI_SCLK,
        .quadwp_io_num   = SPI_PIN_UNUSED,
        .quadhd_io_num   = SPI_PIN_UNUSED,
        .max_transfer_sz = SPI_MAX_TRANSFER_SZ,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    epd_config_t epd_cfg = {
        .spi_host      = SPI2_HOST,
        .pin_cs        = EPD_PIN_CS,
        .pin_dc        = EPD_PIN_DC,
        .pin_rst       = EPD_PIN_RST,
        .pin_busy      = EPD_PIN_BUSY,
        .skip_bus_init = true,
    };
    esp_err_t epd_ret = epd_io_init(&epd_cfg);
    bool      epd_ok  = (epd_ret == ESP_OK);

    if (epd_ok) {
        epd_set_panel((uint8_t)EPD420,
                      (uint16_t)EPD_WIDTH, (uint16_t)EPD_HEIGHT);
        epd_paint_newimage(image_bw,
                           (uint16_t)EPD_WIDTH, (uint16_t)EPD_HEIGHT,
                           (uint16_t)EPD_ROTATE_0, (uint16_t)EPD_COLOR_WHITE);
        epd_paint_clear((uint16_t)EPD_COLOR_BLACK);
        draw_logo();

        bool epd_busy = (epd_init() != 0U);
        if (epd_busy) {
            ESP_LOGE(TAG, "EPD init failed (busy timeout)");
        } else {
            epd_displayBW(image_bw);
            ESP_LOGI(TAG, "EPD: logo displayed");
            epd_enter_deepsleepmode(EPD_DEEPSLEEP_MODE1);
        }
    } else {
        ESP_LOGE(TAG, "EPD SPI init failed");
    }

    pn532_t        nfc     = { 0 };
    pn532_config_t nfc_cfg = {
        .spi_host      = SPI2_HOST,
        .pin_cs        = NFC_CS,
        .skip_bus_init = true,
    };
    esp_err_t nfc_ret = pn532_init(&nfc, &nfc_cfg);
    bool      nfc_ok  = (nfc_ret == ESP_OK);

    if (nfc_ok) {
        uint32_t version = pn532_get_firmware_version(&nfc);
        bool     fw_ok   = (version != 0U);
        if (fw_ok) {
            ESP_LOGI(TAG, "PN5%02X firmware v%u.%u",
                     (unsigned int)((version >> PN532_FW_IC_SHIFT)  & BYTE_MASK),
                     (unsigned int)((version >> PN532_FW_VER_SHIFT) & BYTE_MASK),
                     (unsigned int)((version >> PN532_FW_REV_SHIFT) & BYTE_MASK));

            bool sam_ok = pn532_sam_config(&nfc);
            if (sam_ok) {
                ESP_LOGI(TAG, "Ready — scan a tag");
                uint32_t last_uid = 0U;
                while (true) {
                    uint32_t uid     = pn532_read_passive_target_id(
                                           &nfc, PN532_MIFARE_ISO14443A);
                    bool     new_tag = ((uid != 0U) && (uid != last_uid));
                    if (new_tag) {
                        ESP_LOGI(TAG, "Tag: 0x%08lX", (unsigned long)uid);
                        show_uid_on_epd(uid);
                        last_uid = uid;
                    }
                    vTaskDelay(pdMS_TO_TICKS(POLL_DELAY_MS));
                }
            } else {
                ESP_LOGE(TAG, "SAMConfig failed");
            }
        } else {
            ESP_LOGE(TAG, "PN532 not found");
        }
    } else {
        ESP_LOGE(TAG, "PN532 init failed");
    }
}
