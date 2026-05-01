#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "epd.h"
#include "logo.h"
#include "CW_Utils.h"
#include "CryptnoxWallet.h"
#include "Pn532NfcTransport.h"
#include "ESP32Logger.h"
#include "esp32_crypto_provider.h"

static const char *const TAG = "main";
static const size_t RND_BUF_SIZE = 16U;
static const uint32_t POLL_DELAY_MS = 500U;

/* Shared SPI bus: MOSI=IO11, SCLK=IO12, MISO=IO13 */
#define SPI_MOSI            11
#define SPI_MISO            13
#define SPI_SCLK            12
#define SPI_MAX_TRANSFER_SZ 4096
#define SPI_PIN_UNUSED      (-1)

/* PN532 NFC reader */
#define NFC_CS  10

/* E-paper display pins */
#define EPD_PIN_CS   38
#define EPD_PIN_DC   40
#define EPD_PIN_RST  41
#define EPD_PIN_BUSY 42

/* 4.2" display resolution */
#define EPD_WIDTH  400U
#define EPD_HEIGHT 300U

/* Bitmap bit-packing constants */
#define BITS_PER_BYTE    8U
#define BYTE_MSB         0x80U
#define CENTER_DIVISOR   2U  /* halves a pixel dimension to compute a centred offset */
#define LAST_IDX_OFFSET  1U  /* subtracts from an inclusive count to give the last index */
#define BIT_EXTRACT_MASK 1U  /* isolates the single shifted bit from a byte */

static uint8_t image_bw[EPD_WIDTH / BITS_PER_BYTE * EPD_HEIGHT];

static void draw_logo(void)
{
    uint16_t buf_stride = static_cast<uint16_t>(EPD_WIDTH / BITS_PER_BYTE);
    uint16_t off_x_px = static_cast<uint16_t>((EPD_WIDTH - LOGO_H) / CENTER_DIVISOR);
    uint16_t off_y = static_cast<uint16_t>((EPD_HEIGHT - LOGO_W) / CENTER_DIVISOR);

    for (uint16_t sy = 0U; sy < static_cast<uint16_t>(LOGO_H); sy++) {
        for (uint16_t sx = 0U; sx < static_cast<uint16_t>(LOGO_W); sx++) {
            uint32_t src_idx = static_cast<uint32_t>(sy) * static_cast<uint32_t>(LOGO_W / BITS_PER_BYTE)
                               + static_cast<uint32_t>(sx / BITS_PER_BYTE);
            uint8_t src_byte = logo_cryptnox[src_idx];
            uint8_t sx_bit = static_cast<uint8_t>(sx % BITS_PER_BYTE);
            uint8_t shift = static_cast<uint8_t>((BITS_PER_BYTE - LAST_IDX_OFFSET) - static_cast<unsigned int>(sx_bit));
            uint8_t src_bit = static_cast<uint8_t>((src_byte >> shift) & BIT_EXTRACT_MASK);
            bool pix_set = (src_bit == 0U);
            uint16_t dx = sy;
            uint16_t dy = static_cast<uint16_t>(static_cast<uint16_t>(LOGO_W - LAST_IDX_OFFSET) - sx);
            uint16_t bx = static_cast<uint16_t>(off_x_px + dx);
            uint16_t by = static_cast<uint16_t>(off_y + dy);
            uint32_t addr = static_cast<uint32_t>(by) * static_cast<uint32_t>(buf_stride)
                            + static_cast<uint32_t>(bx / BITS_PER_BYTE);
            uint8_t bx_bit = static_cast<uint8_t>(bx % BITS_PER_BYTE);
            uint8_t bit_mask = static_cast<uint8_t>(BYTE_MSB >> bx_bit);
            if (pix_set) {
                image_bw[addr] = static_cast<uint8_t>(image_bw[addr] | bit_mask);
            } else {
                image_bw[addr] = static_cast<uint8_t>(
                    image_bw[addr] & static_cast<uint8_t>(~static_cast<unsigned int>(bit_mask)));
            }
        }
    }
}

static void run_wallet_loop(CryptnoxWallet &wallet)
{
    /* SHA-256 of "CryptnoxTest\0" padded to 32 bytes — deterministic test vector. */
    static const uint8_t TEST_HASH[CW_HASH_SIZE] = {
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U,
    };
    /* Replace with the card's actual PIN (ASCII digits, 4-9 characters). */
    static const uint8_t TEST_PIN[CW_MAX_PIN_LENGTH] = {
        '0', '0', '0', '0', '0', '0', '0', '0', '0'
    };

    while (true) {
        CW_SecureSession session;
        bool connected = wallet.connect(session);

        if (!connected) {
            ESP_LOGW(TAG, "Card not found or secure channel failed - retrying");
        }

        if (connected) {
            CW_SignRequest req(session,
                              CW_SIGN_CURR_K1,
                              CW_SIGN_SIG_ECDSA_LOW_S,
                              CW_SIGN_WITH_PIN);
            req.hash = TEST_HASH;
            req.hashLength = static_cast<uint8_t>(CW_HASH_SIZE);
            (void)memcpy(req.pin, TEST_PIN, CW_MAX_PIN_LENGTH);

            CW_SignResult result = wallet.sign(req);

            if (result.errorCode == CW_OK) {
                ESP_LOGI(TAG, "Sign OK - r:");
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, &result.signature[CW_SIG_R_OFFSET],
                                         CW_HASH_SIZE, ESP_LOG_INFO);
                ESP_LOGI(TAG, "s:");
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, &result.signature[CW_SIG_S_OFFSET],
                                         CW_HASH_SIZE, ESP_LOG_INFO);
            } else {
                ESP_LOGE(TAG, "Sign failed: 0x%02X",
                         static_cast<unsigned int>(result.errorCode));
            }
        }

        wallet.disconnect(session);
        vTaskDelay(pdMS_TO_TICKS(POLL_DELAY_MS));
    }
}

extern "C" void app_main(void)
{
    uint8_t rnd[RND_BUF_SIZE] = { 0U };
    bool rnd_result = CW_Utils::fill_secure_random(rnd, sizeof(rnd));
    if (rnd_result) {
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
        .mosi_io_num = SPI_MOSI,
        .miso_io_num = SPI_MISO,
        .sclk_io_num = SPI_SCLK,
        .quadwp_io_num = SPI_PIN_UNUSED,
        .quadhd_io_num = SPI_PIN_UNUSED,
        .max_transfer_sz = SPI_MAX_TRANSFER_SZ,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    epd_config_t epd_cfg = {
        .spi_host = SPI2_HOST,
        .pin_cs = EPD_PIN_CS,
        .pin_dc = EPD_PIN_DC,
        .pin_rst = EPD_PIN_RST,
        .pin_busy = EPD_PIN_BUSY,
        .skip_bus_init = true,
    };
    esp_err_t epd_ret = epd_io_init(&epd_cfg);

    if (epd_ret == ESP_OK) {
        epd_set_panel(static_cast<uint8_t>(EPD420),
                      static_cast<uint16_t>(EPD_WIDTH),
                      static_cast<uint16_t>(EPD_HEIGHT));
        epd_paint_newimage(image_bw,
                           static_cast<uint16_t>(EPD_WIDTH),
                           static_cast<uint16_t>(EPD_HEIGHT),
                           static_cast<uint16_t>(EPD_ROTATE_0),
                           static_cast<uint16_t>(EPD_COLOR_WHITE));
        epd_paint_clear(static_cast<uint16_t>(EPD_COLOR_BLACK));
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

    pn532_t nfc = {};
    pn532_config_t nfc_cfg = {
        .spi_host = SPI2_HOST,
        .pin_cs = NFC_CS,
        .skip_bus_init = true,
    };
    esp_err_t nfc_ret = pn532_init(&nfc, &nfc_cfg);

    if (nfc_ret == ESP_OK) {
        ESP32Logger logger;
        (void)logger.begin(115200UL);

        ESP32CryptoProvider cryptoProvider;
        Pn532NfcTransport nfcTransport(&nfc, logger);
        CryptnoxWallet wallet(nfcTransport, logger, cryptoProvider);

        if (wallet.begin()) {
            (void)nfcTransport.printFirmwareVersion();
            ESP_LOGI(TAG, "Wallet ready - hold card to scan");
            run_wallet_loop(wallet);
        } else {
            ESP_LOGE(TAG, "Wallet init failed (SAMConfig)");
        }
    } else {
        ESP_LOGE(TAG, "PN532 init failed");
    }
}
