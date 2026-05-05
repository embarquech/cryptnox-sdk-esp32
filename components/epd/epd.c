/*---------------------------------------
- WeAct Studio Official Link
- taobao: weactstudio.taobao.com
- aliexpress: weactstudio.aliexpress.com
- github: github.com/WeActTC
- Ported to ESP-IDF from Raspberry Pi version
---------------------------------------*/

#include "epd.h"
#include "epdfont.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *const EPD_LOG_TAG = "epd";

static spi_device_handle_t epd_spi;
static int epdepd_pin_dc;
static int epdepd_pin_rst;
static int epdepd_pin_busy;
static int epdepd_pin_cs;

uint16_t EPD_H;
uint16_t EPD_W;
EPD_PAINT EPD_Paint;
uint8_t epd_type = 0U;

static uint8_t epdepd_hibernating = 1U;
static uint8_t old_data[30U * 416U];

/* Maximum display buffer size — matches old_data. */
#define EPD_MAX_BUFF_SIZE    (30U * 416U)

/******************************************************************
 * 1. Module constants
 ******************************************************************/

#define EPD_SPI_CHUNK_SIZE       (4096U)
#define EPD_BUSY_TIMEOUT_COUNT   (40000U)
#define EPD_RESET_DELAY_MS       (50U)
#define EPD_SWRESET_DELAY_MS     (100U)
#define EPD_DELAY_1MS            (1U)
#define EPD_BITS_PER_BYTE        (8U)
#define EPD_PIXEL_BITS           (8U)
#define EPD_BYTE_MASK            (0xFFU)
#define EPD_HIGH_BIT_MASK        (0x80U)
#define EPD_LOW_BIT_MASK         (0x01U)
#define EPD_ADDR_MASK_1BIT       (0x01U)
#define EPD_X_SHIFT              (3U)
#define EPD_Y_SHIFT_8            (8U)
#define EPD_CHAR_SIZE_8          (8U)
#define EPD_CHAR_SIZE_12         (12U)
#define EPD_CHAR_SIZE_16         (16U)
#define EPD_CHAR_SIZE_24         (24U)
#define EPD_CHAR_SIZE_32         (32U)
#define EPD_CHAR_SIZE_64         (64U)
#define EPD_CHAR_COL_6           (6U)
#define EPD_CHAR_COL_8           (8U)
#define EPD_FONT8_SIZE2          (6U)
#define EPD_EPD154_Y_OFFSET      (199U)
#define EPD_EPD213_Y_OFFSET      (295U)
#define EPD_RESULT_OK            (0U)
#define EPD_RESULT_ERR           (1U)

/* SPI bus / device configuration constants */
#define EPD_SPI_BUS_MAX_TRANSFER (4096)
#define EPD_SPI_PIN_UNUSED       (-1)
#define EPD_SPI_MODE             (3U)
#define EPD_SPI_CLOCK_HZ         (4000000U)
#define EPD_SPI_QUEUE_SIZE       (1U)

/******************************************************************
 * 2. HAL: GPIO and SPI helpers
 ******************************************************************/

static void epd_delay(uint16_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void epd_res_set(void)
{
    (void)gpio_set_level(epd_pin_rst, 1);
}

static void epd_res_reset(void)
{
    (void)gpio_set_level(epd_pin_rst, 0);
}

static void epd_dc_set(void)
{
    (void)gpio_set_level(epd_pin_dc, 1);
}

static void epd_dc_reset(void)
{
    (void)gpio_set_level(epd_pin_dc, 0);
}

static void epd_cs_set(void)
{
    (void)gpio_set_level(epd_pin_cs, 1);
}

static void epd_cs_reset(void)
{
    (void)gpio_set_level(epd_pin_cs, 0);
}

static uint8_t epd_is_busy(void)
{
    uint8_t result = 0U;
    if (epd_type == EPD370_UC8253) {
        result = (gpio_get_level(epd_pin_busy) != 0) ? 0U : 1U;
    } else {
        result = (gpio_get_level(epd_pin_busy) != 0) ? 1U : 0U;
    }
    return result;
}

static void spi_send(const uint8_t *data, uint32_t len)
{
    spi_transaction_t t = {
        .length    = len * EPD_BITS_PER_BYTE,
        .tx_buffer = data,
    };
    (void)spi_device_transmit(epd_spi, &t);
}

static void epd_write_data(uint8_t data)
{
    epd_cs_reset();
    spi_send(&data, 1U);
    epd_cs_set();
}

static void epd_write_bulk(const uint8_t *data, uint32_t len)
{
    uint32_t remaining = len;
    uint32_t offset    = 0U;

    while (remaining > 0U) {
        uint32_t current_chunk = (remaining > EPD_SPI_CHUNK_SIZE) ?
                                  EPD_SPI_CHUNK_SIZE : remaining;
        spi_send(&data[offset], current_chunk);
        offset    += current_chunk;
        remaining -= current_chunk;
    }
}

/******************************************************************
 * 3. Public HAL init
 ******************************************************************/

esp_err_t epd_io_init(const epd_config_t *config)
{
    epd_pin_dc   = config->pin_dc;
    epd_pin_rst  = config->pin_rst;
    epd_pin_busy = config->pin_busy;
    epd_pin_cs   = config->pin_cs;

    uint64_t out_mask = ((1ULL << (uint32_t)epd_pin_dc)  |
                         (1ULL << (uint32_t)epd_pin_rst)  |
                         (1ULL << (uint32_t)epd_pin_cs));
    gpio_config_t io_out = {
        .pin_bit_mask = out_mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io_out);

    gpio_config_t io_in = {
        .pin_bit_mask = (1ULL << (uint32_t)epd_pin_busy),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io_in);

    epd_cs_set();
    epd_dc_set();
    epd_res_set();

    esp_err_t result = ESP_OK;

    if (!config->skip_bus_init) {
        spi_bus_config_t buscfg = {
            .mosi_io_num     = config->pin_mosi,
            .miso_io_num     = EPD_SPI_PIN_UNUSED,
            .sclk_io_num     = config->pin_sclk,
            .quadwp_io_num   = EPD_SPI_PIN_UNUSED,
            .quadhd_io_num   = EPD_SPI_PIN_UNUSED,
            .max_transfer_sz = EPD_SPI_BUS_MAX_TRANSFER,
        };
        result = spi_bus_initialize(config->spi_host, &buscfg, SPI_DMA_CH_AUTO);
    }

    if (result == ESP_OK) {
        spi_device_interface_config_t devcfg = {
            .mode           = EPD_SPI_MODE,
            .clock_speed_hz = EPD_SPI_CLOCK_HZ,
            .spics_io_num   = EPD_SPI_PIN_UNUSED,
            .queue_size     = EPD_SPI_QUEUE_SIZE,
        };
        result = spi_bus_add_device(config->spi_host, &devcfg, &epd_spi);
    }

    if (result == ESP_OK) {
        ESP_LOGI(EPD_LOG_TAG, "EPD SPI initialized");
    }

    return result;
}

void epd_io_deinit(void)
{
    (void)spi_bus_remove_device(epd_spi);
}

/******************************************************************
 * 4. Core EPD functions
 ******************************************************************/

void epd_write_reg(uint8_t reg)
{
    epd_dc_reset();
    epd_cs_reset();
    spi_send(&reg, 1U);
    epd_cs_set();
    epd_dc_set();
}

static uint8_t epd_wait_busy(void)
{
    uint32_t timeout = 0U;
    uint8_t  result  = EPD_RESULT_OK;
    int      level   = gpio_get_level(epd_pin_busy);
    ESP_LOGI(EPD_LOG_TAG, "wait_busy: BUSY pin=%d (raw level=%d)", epd_pin_busy, level);

    while ((epd_is_busy() != 0U) && (result == EPD_RESULT_OK)) {
        timeout++;
        if (timeout > EPD_BUSY_TIMEOUT_COUNT) {
            result = EPD_RESULT_ERR;
        }
        if (result == EPD_RESULT_OK) {
            epd_delay(EPD_DELAY_1MS);
        }
    }

    ESP_LOGI(EPD_LOG_TAG, "wait_busy: done after %lu ms", (unsigned long)timeout);
    return result;
}

static void epd_reset(void)
{
    epd_res_reset();
    epd_delay(EPD_RESET_DELAY_MS);
    epd_res_set();
    epd_delay(EPD_RESET_DELAY_MS);
    epd_hibernating = 0U;
}

void epd_set_panel(uint8_t type, uint16_t width, uint16_t height)
{
    epd_type = type;
    EPD_H = height;
    EPD_W = width;
    ESP_LOGI(EPD_LOG_TAG, "Panel: %dx%d, type: %d", EPD_W, EPD_H, epd_type);
}

uint8_t epd_init(void)
{
    uint8_t result = EPD_RESULT_OK;

    if (epd_hibernating != 0U) {
        epd_reset();
    }

    if (epd_wait_busy() != EPD_RESULT_OK) {
        result = EPD_RESULT_ERR;
    }

    if (result == EPD_RESULT_OK) {
        if (epd_type == EPD370_UC8253) {
            epd_write_reg(0x04U);
            if (epd_wait_busy() != EPD_RESULT_OK) {
                result = EPD_RESULT_ERR;
            }
            if (result == EPD_RESULT_OK) {
                epd_write_reg(0x00U);
                epd_write_data(0x1FU);
                epd_write_data(0x0DU);
                epd_write_reg(0x50U);
                epd_write_data(0x97U);
            }
        } else {
            epd_write_reg(0x12U);
            epd_delay(EPD_SWRESET_DELAY_MS);
            if (epd_wait_busy() != EPD_RESULT_OK) {
                result = EPD_RESULT_ERR;
            }

            if (result == EPD_RESULT_OK) {
                if ((epd_type == EPD213_219) || (epd_type == EPD154)) {
                    epd_write_reg(0x01U);
                    if (epd_type == EPD213_219) {
                        epd_write_data(0x27U);
                        epd_write_data(0x01U);
                        epd_write_data(0x01U);
                    } else {
                        epd_write_data(0xC7U);
                        epd_write_data(0x00U);
                        epd_write_data(0x01U);
                    }
                    epd_write_reg(0x11U);
                    epd_write_data(0x01U);
                    if (epd_type == EPD154) {
                        epd_write_reg(0x44U);
                        epd_write_data(0x00U);
                        epd_write_data(0x18U);
                        epd_write_reg(0x45U);
                        epd_write_data(0xC7U);
                        epd_write_data(0x00U);
                        epd_write_data(0x00U);
                        epd_write_data(0x00U);
                    } else {
                        epd_write_reg(0x44U);
                        epd_write_data(0x00U);
                        epd_write_data(0x0FU);
                        epd_write_reg(0x45U);
                        epd_write_data(0x27U);
                        epd_write_data(0x01U);
                        epd_write_data(0x00U);
                        epd_write_data(0x00U);
                    }
                    epd_write_reg(0x3CU);
                    epd_write_data(0x05U);
                    if (epd_type == EPD213_219) {
                        epd_write_reg(0x21U);
                        epd_write_data(0x00U);
                        epd_write_data(0x80U);
                    } else {
                        /* EPD154: no extra register */
                    }
                } else if (epd_type == EPD420) {
                    epd_write_reg(0x21U);
                    epd_write_data(0x40U);
                    epd_write_data(0x00U);
                    epd_write_reg(0x01U);
                    epd_write_data(0x2BU);
                    epd_write_data(0x01U);
                    epd_write_data(0x00U);
                    epd_write_reg(0x3CU);
                    epd_write_data(0x01U);
                    epd_write_reg(0x11U);
                    epd_write_data(0x03U);
                    epd_address_set(0U, 0U,
                                    (uint16_t)(EPD_W - 1U),
                                    (uint16_t)(EPD_H - 1U));
                } else {
                    /* other panel types: no extra init */
                }

                epd_write_reg(0x18U);
                epd_write_data(0x80U);
                epd_setpos(0U, 0U);

                if (epd_power_on() != EPD_RESULT_OK) {
                    result = EPD_RESULT_ERR;
                }
            }
        }
    }

    return result;
}

uint8_t epd_init_fast(void)
{
    uint8_t result = EPD_RESULT_OK;

    if (epd_init() != EPD_RESULT_OK) {
        result = EPD_RESULT_ERR;
    }

    if (result == EPD_RESULT_OK) {
        if (epd_type == EPD370_UC8253) {
            epd_write_reg(0xE0U);
            epd_write_data(0x02U);
            epd_write_reg(0xE5U);
            epd_write_data(0x5FU);
        } else {
            epd_write_reg(0x22U);
            epd_write_data(0xB1U);
            epd_write_reg(0x20U);
            if (epd_wait_busy() != EPD_RESULT_OK) {
                result = EPD_RESULT_ERR;
            }
            if (result == EPD_RESULT_OK) {
                epd_write_reg(0x1AU);
                epd_write_data(0x6EU);
                epd_write_reg(0x22U);
                epd_write_data(0x91U);
                epd_write_reg(0x20U);
                if (epd_wait_busy() != EPD_RESULT_OK) {
                    result = EPD_RESULT_ERR;
                }
            }
        }
    }

    return result;
}

uint8_t epd_init_partial(void)
{
    static const unsigned char lut_partial[] = {
        0x0U, 0x40U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U,
        0x0U, 0x0U, 0x80U, 0x80U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U,
        0x0U, 0x0U, 0x0U, 0x0U, 0x40U, 0x40U, 0x0U, 0x0U, 0x0U, 0x0U,
        0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x80U, 0x0U, 0x0U,
        0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U,
        0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U,
        0x0AU, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x2U, 0x1U, 0x0U, 0x0U,
        0x0U, 0x0U, 0x0U, 0x0U, 0x1U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U,
        0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U,
        0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U,
        0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U,
        0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U,
        0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U,
        0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U, 0x0U,
        0x0U, 0x0U, 0x0U, 0x0U, 0x22U, 0x22U, 0x22U, 0x22U, 0x22U,
        0x22U, 0x0U, 0x0U, 0x0U,
    };

    uint8_t result = EPD_RESULT_OK;

    if (epd_init() != EPD_RESULT_OK) {
        result = EPD_RESULT_ERR;
    }

    if (result == EPD_RESULT_OK) {
        if (epd_type == EPD213_219) {
            epd_write_reg(0x32U);
            epd_cs_reset();
            epd_write_bulk(lut_partial, sizeof(lut_partial));
            epd_cs_set();
        } else if (epd_type == EPD420) {
            epd_write_reg(0x3CU);
            epd_write_data(0x80U);
            epd_write_reg(0x21U);
            epd_write_data(0x00U);
            epd_write_data(0x00U);
        } else if (epd_type == EPD370_UC8253) {
            epd_write_reg(0xE0U);
            epd_write_data(0x02U);
            epd_write_reg(0xE5U);
            epd_write_data(0x6EU);
            epd_write_reg(0x50U);
            epd_write_data(0xD7U);
        } else {
            /* other panel types: no partial LUT */
        }
    }

    return result;
}

void epd_enter_deepsleepmode(uint8_t mode)
{
    (void)epd_power_off();
    if (epd_type == EPD370_UC8253) {
        epd_write_reg(0x07U);
        epd_write_data(0xA5U);
    } else {
        epd_write_reg(0x10U);
        epd_write_data(mode);
    }
    epd_hibernating = 1U;
}

uint8_t epd_power_on(void)
{
    uint8_t result = EPD_RESULT_OK;

    if (epd_type == EPD370_UC8253) {
        epd_write_reg(0x04U);
        result = epd_wait_busy();
    } else if (epd_type == EPD420) {
        epd_write_reg(0x22U);
        epd_write_data(0xe0U);
        epd_write_reg(0x20U);
        result = epd_wait_busy();
    } else {
        epd_write_reg(0x22U);
        epd_write_data(0xf8U);
        epd_write_reg(0x20U);
        result = epd_wait_busy();
    }

    return result;
}

uint8_t epd_power_off(void)
{
    if (epd_type == EPD370_UC8253) {
        epd_write_reg(0x02U);
    } else {
        epd_write_reg(0x22U);
        epd_write_data(0x83U);
        epd_write_reg(0x20U);
    }
    return epd_wait_busy();
}

void epd_init_internalTempSensor(void)
{
    if (epd_type != EPD370_UC8253) {
        epd_write_reg(0x18U);
        epd_write_data(0x80U);
        epd_write_reg(0x1AU);
        epd_write_data(0x7FU);
    }
}

void epd_update(void)
{
    if (epd_type == EPD370_UC8253) {
        epd_write_reg(0x12U);
        epd_delay(EPD_DELAY_1MS);
        (void)epd_wait_busy();
    } else if (epd_type == EPD154) {
        epd_write_reg(0x22U);
        epd_write_data(0xF4U);
        epd_write_reg(0x20U);
        (void)epd_wait_busy();
    } else {
        epd_write_reg(0x22U);
        epd_write_data(0xF7U);
        epd_write_reg(0x20U);
        (void)epd_wait_busy();
    }
}

void epd_update_fast(void)
{
    if (epd_type == EPD370_UC8253) {
        epd_update();
    } else {
        epd_write_reg(0x22U);
        epd_write_data(0xC7U);
        epd_write_reg(0x20U);
        (void)epd_wait_busy();
    }
}

void epd_update_partial(void)
{
    if (epd_type == EPD370_UC8253) {
        epd_update();
    } else if (epd_type == EPD154) {
        epd_write_reg(0x22U);
        epd_write_data(0xFCU);
        epd_write_reg(0x20U);
        (void)epd_wait_busy();
    } else if (epd_type == EPD420) {
        epd_write_reg(0x22U);
        epd_write_data(0xFFU);
        epd_write_reg(0x20U);
        (void)epd_wait_busy();
    } else {
        epd_write_reg(0x22U);
        epd_write_data(0xCCU);
        epd_write_reg(0x20U);
        (void)epd_wait_busy();
    }
}

void epd_address_set(uint16_t x_start, uint16_t y_start,
                     uint16_t x_end,   uint16_t y_end)
{
    if (epd_type == EPD370_UC8253) {
        epd_write_reg(0x90U);
        epd_write_data((uint8_t)((x_start >> EPD_X_SHIFT) & EPD_BYTE_MASK));
        epd_write_data((uint8_t)((x_end   >> EPD_X_SHIFT) & EPD_BYTE_MASK));
        epd_write_data((uint8_t)(y_start & EPD_BYTE_MASK));
        epd_write_data((uint8_t)((y_start >> EPD_Y_SHIFT_8) & EPD_BYTE_MASK));
        epd_write_data((uint8_t)(y_end & EPD_BYTE_MASK));
        epd_write_data((uint8_t)((y_end >> EPD_Y_SHIFT_8) & EPD_BYTE_MASK));
        epd_write_data(0x01U);
    } else {
        epd_write_reg(0x44U);
        epd_write_data((uint8_t)((x_start >> EPD_X_SHIFT) & EPD_BYTE_MASK));
        epd_write_data((uint8_t)((x_end   >> EPD_X_SHIFT) & EPD_BYTE_MASK));
        epd_write_reg(0x45U);
        epd_write_data((uint8_t)(y_start & EPD_BYTE_MASK));
        epd_write_data((uint8_t)((y_start >> EPD_Y_SHIFT_8) & EPD_BYTE_MASK));
        epd_write_data((uint8_t)(y_end & EPD_BYTE_MASK));
        epd_write_data((uint8_t)((y_end >> EPD_Y_SHIFT_8) & EPD_BYTE_MASK));
    }
}

void epd_setpos(uint16_t x, uint16_t y)
{
    uint8_t  lx = (uint8_t)(x / EPD_PIXEL_BITS);
    uint16_t ly = 0U;

    if (epd_type == EPD154) {
        ly = (uint16_t)(EPD_EPD154_Y_OFFSET - y);
    } else if (epd_type == EPD213_219) {
        ly = (uint16_t)(EPD_EPD213_Y_OFFSET - y);
    } else {
        ly = y;
    }

    if (epd_type == EPD370_UC8253) {
        epd_write_reg(0x65U);
        epd_write_data(lx);
        epd_write_data((uint8_t)((ly >> EPD_Y_SHIFT_8) & EPD_ADDR_MASK_1BIT));
        epd_write_data((uint8_t)(ly & EPD_BYTE_MASK));
    } else {
        epd_write_reg(0x4EU);
        epd_write_data(lx);
        epd_write_reg(0x4FU);
        epd_write_data((uint8_t)(ly & EPD_BYTE_MASK));
        epd_write_data((uint8_t)((ly >> EPD_Y_SHIFT_8) & EPD_ADDR_MASK_1BIT));
    }
}

void epd_write_imagedata(const uint8_t *Image1, uint32_t length)
{
    epd_cs_reset();
    epd_write_bulk(Image1, length);
    epd_cs_set();
}

static void epd_write_imagedata_invert(const uint8_t *Image1, uint32_t length)
{
    if (length <= EPD_MAX_BUFF_SIZE) {
        static uint8_t epd_invert_buf[EPD_MAX_BUFF_SIZE];
        for (uint32_t j = 0U; j < length; j++) {
            epd_invert_buf[j] = (uint8_t)(~Image1[j]);
        }
        epd_cs_reset();
        epd_write_bulk(epd_invert_buf, length);
        epd_cs_set();
    }
}

void epd_display(const uint8_t *Image1, const uint8_t *Image2)
{
    if (epd_type != EPD370_UC8253) {
        uint32_t length = (uint32_t)EPD_H * (uint32_t)EPD_W_BUFF_SIZE;

        epd_setpos(0U, 0U);
        epd_write_reg(0x24U);
        epd_write_imagedata(Image1, length);

        epd_setpos(0U, 0U);
        epd_write_reg(0x26U);
        epd_write_imagedata_invert(Image2, length);

        if (epd_type == EPD420) {
            epd_write_reg(0x21U);
            epd_write_data(0x00U);
            epd_write_data(0x00U);
        }
        epd_update();
    }
}

void epd_displayBW(const uint8_t *Image)
{
    uint32_t length = (uint32_t)EPD_H * (uint32_t)EPD_W_BUFF_SIZE;

    if (epd_type == EPD370_UC8253) {
        epd_write_reg(0x10U);
        epd_write_imagedata(old_data, length);
        epd_write_reg(0x13U);
        epd_write_imagedata(Image, length);
        (void)memcpy(old_data, Image, length);
        epd_update();
    } else {
        epd_setpos(0U, 0U);
        epd_write_reg(0x26U);
        epd_write_imagedata(Image, length);

        epd_setpos(0U, 0U);
        epd_write_reg(0x24U);
        epd_write_imagedata(Image, length);

        epd_update();
    }
}

void epd_displayBW_fast(const uint8_t *Image)
{
    uint32_t length = (uint32_t)EPD_H * (uint32_t)EPD_W_BUFF_SIZE;

    if (epd_type == EPD370_UC8253) {
        epd_write_reg(0x10U);
        epd_write_imagedata(old_data, length);
        epd_write_reg(0x13U);
        epd_write_imagedata(Image, length);
        (void)memcpy(old_data, Image, length);
        epd_update();
    } else {
        epd_setpos(0U, 0U);
        epd_write_reg(0x26U);
        epd_write_imagedata(Image, length);

        epd_setpos(0U, 0U);
        epd_write_reg(0x24U);
        epd_write_imagedata(Image, length);

        epd_update_fast();
    }
}

void epd_displayBW_partial(const uint8_t *Image)
{
    if (epd_type == EPD370_UC8253) {
        epd_displayBW(Image);
    } else {
        uint32_t length = (uint32_t)EPD_H * (uint32_t)EPD_W_BUFF_SIZE;

        epd_setpos(0U, 0U);
        epd_write_reg(0x24U);
        epd_write_imagedata(Image, length);

        epd_update_partial();

        epd_setpos(0U, 0U);
        epd_write_reg(0x26U);
        epd_write_imagedata(Image, length);
    }
}

void epd_displayRED(const uint8_t *Image)
{
    uint32_t length = (uint32_t)EPD_H * (uint32_t)EPD_W_BUFF_SIZE;

    epd_setpos(0U, 0U);
    epd_write_reg(0x26U);
    epd_write_imagedata(Image, length);

    if (epd_type == EPD420) {
        epd_write_reg(0x21U);
        epd_write_data(0x00U);
        epd_write_data(0x00U);
    }
    epd_update();
}

void epd_displayRED_invert(const uint8_t *Image)
{
    uint32_t length = (uint32_t)EPD_H * (uint32_t)EPD_W_BUFF_SIZE;

    epd_setpos(0U, 0U);
    epd_write_reg(0x26U);
    epd_write_imagedata_invert(Image, length);

    if (epd_type == EPD420) {
        epd_write_reg(0x21U);
        epd_write_data(0x00U);
        epd_write_data(0x00U);
    }
    epd_update();
}

/******************************************************************
 * 5. Paint functions
 ******************************************************************/

void epd_paint_newimage(uint8_t *image, uint16_t Width, uint16_t Height,
                        uint16_t Rotate, uint16_t Color)
{
    EPD_Paint.Image        = image;
    EPD_Paint.WidthMemory  = Width;
    EPD_Paint.HeightMemory = Height;
    EPD_Paint.Color        = Color;
    EPD_Paint.WidthByte    = ((Width % EPD_PIXEL_BITS) == 0U) ?
                              (uint16_t)(Width / EPD_PIXEL_BITS) :
                              (uint16_t)((Width / EPD_PIXEL_BITS) + 1U);
    EPD_Paint.HeightByte   = Height;
    EPD_Paint.Rotate       = Rotate;
    if ((Rotate == EPD_ROTATE_0) || (Rotate == EPD_ROTATE_180)) {
        EPD_Paint.Width  = Height;
        EPD_Paint.Height = Width;
    } else {
        EPD_Paint.Width  = Width;
        EPD_Paint.Height = Height;
    }
}

void epd_paint_setpixel(uint16_t Xpoint, uint16_t Ypoint, uint16_t Color)
{
    uint16_t X     = 0U;
    uint16_t Y     = 0U;
    bool     valid = true;

    switch (EPD_Paint.Rotate) {
    case 0U:
        X = (uint16_t)(EPD_Paint.WidthMemory - Ypoint - 1U);
        Y = Xpoint;
        break;
    case 90U:
        X = (uint16_t)(EPD_Paint.WidthMemory  - Xpoint - 1U);
        Y = (uint16_t)(EPD_Paint.HeightMemory - Ypoint - 1U);
        break;
    case 180U:
        X = Ypoint;
        Y = (uint16_t)(EPD_Paint.HeightMemory - Xpoint - 1U);
        break;
    case 270U:
        X = Xpoint;
        Y = Ypoint;
        break;
    default:
        valid = false;
        break;
    }

    if (valid) {
        uint16_t x_byte = (uint16_t)(X / EPD_PIXEL_BITS);
        uint32_t Addr   = (uint32_t)x_byte + (uint32_t)Y * (uint32_t)EPD_Paint.WidthByte;
        if (Color == EPD_COLOR_BLACK) {
            EPD_Paint.Image[Addr] = (uint8_t)(EPD_Paint.Image[Addr] &
                                    (uint8_t)(~(uint8_t)(EPD_HIGH_BIT_MASK >>
                                                         (X % EPD_PIXEL_BITS))));
        } else {
            EPD_Paint.Image[Addr] = (uint8_t)(EPD_Paint.Image[Addr] |
                                    (uint8_t)(EPD_HIGH_BIT_MASK >>
                                              (X % EPD_PIXEL_BITS)));
        }
    }
}

void epd_paint_clear(uint16_t color)
{
    for (uint16_t Y = 0U; Y < EPD_Paint.HeightByte; Y++) {
        for (uint16_t X = 0U; X < EPD_Paint.WidthByte; X++) {
            EPD_Paint.Image[(uint32_t)X + ((uint32_t)Y * (uint32_t)EPD_Paint.WidthByte)] = (uint8_t)color;
        }
    }
}

void epd_paint_selectimage(uint8_t *image)
{
    EPD_Paint.Image = image;
}

void epd_paint_drawPoint(uint16_t Xpoint, uint16_t Ypoint, uint16_t Color)
{
    epd_paint_setpixel((uint16_t)(Xpoint - 1U), (uint16_t)(Ypoint - 1U), Color);
}

void epd_paint_drawLine(uint16_t Xstart, uint16_t Ystart,
                        uint16_t Xend,   uint16_t Yend, uint16_t Color)
{
    uint16_t Xpoint = Xstart;
    uint16_t Ypoint = Ystart;

    int32_t dxraw  = (int32_t)Xend - (int32_t)Xstart;
    int32_t dyraw  = (int32_t)Yend - (int32_t)Ystart;
    int32_t dx     = (dxraw >= 0) ? dxraw : -dxraw;
    int32_t dy     = (dyraw <= 0) ? dyraw : -dyraw;
    int32_t XAddway = ((Xstart < Xend) ? (int32_t)1 : (int32_t)-1);
    int32_t YAddway = ((Ystart < Yend) ? (int32_t)1 : (int32_t)-1);
    int32_t Esp    = dx + dy;
    bool    done   = false;

    while (!done) {
        epd_paint_drawPoint(Xpoint, Ypoint, Color);
        if ((Esp + Esp) >= dy) {
            if (Xpoint == Xend) {
                done = true;
            } else {
                Esp    += dy;
                int32_t new_x = (int32_t)Xpoint + XAddway;
                Xpoint  = (uint16_t)new_x;
            }
        }
        if (!done && ((Esp + Esp) <= dx)) {
            if (Ypoint == Yend) {
                done = true;
            } else {
                Esp    += dx;
                int32_t new_y = (int32_t)Ypoint + YAddway;
                Ypoint  = (uint16_t)new_y;
            }
        }
    }
}

void epd_paint_drawRectangle(uint16_t Xstart, uint16_t Ystart,
                              uint16_t Xend,   uint16_t Yend,
                              uint16_t Color,  uint8_t mode)
{
    if (mode != 0U) {
        for (uint16_t i = Ystart; i < Yend; i++) {
            epd_paint_drawLine(Xstart, i, Xend, i, Color);
        }
    } else {
        epd_paint_drawLine(Xstart, Ystart, Xend,   Ystart, Color);
        epd_paint_drawLine(Xstart, Ystart, Xstart, Yend,   Color);
        epd_paint_drawLine(Xend,   Yend,   Xend,   Ystart, Color);
        epd_paint_drawLine(Xend,   Yend,   Xstart, Yend,   Color);
    }
}

void epd_paint_drawCircle(uint16_t X_Center, uint16_t Y_Center,
                           uint16_t Radius,   uint16_t Color, uint8_t mode)
{
    int16_t f     = (int16_t)(1 - (int16_t)Radius);
    int16_t ddF_x = (int16_t)1;
    int16_t ddF_y = (int16_t)(-2 * (int16_t)Radius);
    int16_t x     = (int16_t)0;
    int16_t y     = (int16_t)Radius;

    int32_t cx_pr = (int32_t)X_Center + (int32_t)Radius;
    int32_t cx_mr = (int32_t)X_Center - (int32_t)Radius;
    int32_t cy_pr = (int32_t)Y_Center + (int32_t)Radius;
    int32_t cy_mr = (int32_t)Y_Center - (int32_t)Radius;

    if (mode != 0U) {
        epd_paint_drawLine((uint16_t)cx_mr, Y_Center, (uint16_t)cx_pr, Y_Center, Color);
    } else {
        epd_paint_drawPoint(X_Center, (uint16_t)cy_pr, Color);
        epd_paint_drawPoint(X_Center, (uint16_t)cy_mr, Color);
        epd_paint_drawPoint((uint16_t)cx_pr, Y_Center, Color);
        epd_paint_drawPoint((uint16_t)cx_mr, Y_Center, Color);
    }

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y = (int16_t)(ddF_y + (int16_t)2);
            f     = (int16_t)(f + ddF_y);
        }
        x++;
        ddF_x = (int16_t)(ddF_x + (int16_t)2);
        f     = (int16_t)(f + ddF_x);

        int32_t cx_px = (int32_t)X_Center + (int32_t)x;
        int32_t cx_mx = (int32_t)X_Center - (int32_t)x;
        int32_t cx_py = (int32_t)X_Center + (int32_t)y;
        int32_t cx_my = (int32_t)X_Center - (int32_t)y;
        int32_t cy_px = (int32_t)Y_Center + (int32_t)x;
        int32_t cy_mx = (int32_t)Y_Center - (int32_t)x;
        int32_t cy_py = (int32_t)Y_Center + (int32_t)y;
        int32_t cy_my = (int32_t)Y_Center - (int32_t)y;

        if (mode != 0U) {
            epd_paint_drawLine((uint16_t)cx_mx, (uint16_t)cy_py, (uint16_t)cx_px, (uint16_t)cy_py, Color);
            epd_paint_drawLine((uint16_t)cx_mx, (uint16_t)cy_my, (uint16_t)cx_px, (uint16_t)cy_my, Color);
            epd_paint_drawLine((uint16_t)cx_my, (uint16_t)cy_px, (uint16_t)cx_py, (uint16_t)cy_px, Color);
            epd_paint_drawLine((uint16_t)cx_my, (uint16_t)cy_mx, (uint16_t)cx_py, (uint16_t)cy_mx, Color);
        } else {
            epd_paint_drawPoint((uint16_t)cx_px, (uint16_t)cy_py, Color);
            epd_paint_drawPoint((uint16_t)cx_mx, (uint16_t)cy_py, Color);
            epd_paint_drawPoint((uint16_t)cx_px, (uint16_t)cy_my, Color);
            epd_paint_drawPoint((uint16_t)cx_mx, (uint16_t)cy_my, Color);
            epd_paint_drawPoint((uint16_t)cx_py, (uint16_t)cy_px, Color);
            epd_paint_drawPoint((uint16_t)cx_my, (uint16_t)cy_px, Color);
            epd_paint_drawPoint((uint16_t)cx_py, (uint16_t)cy_mx, Color);
            epd_paint_drawPoint((uint16_t)cx_my, (uint16_t)cy_mx, Color);
        }
    }
}

void epd_paint_showChar(uint16_t x, uint16_t y, uint16_t chr,
                         uint16_t size1, uint16_t color)
{
    uint16_t lx   = (uint16_t)(x + 1U);
    uint16_t ly   = (uint16_t)(y + 1U);
    uint16_t lx0  = lx;
    uint16_t ly0  = ly;
    uint16_t size2 = 0U;
    uint16_t chr1 = (uint16_t)(chr - (uint16_t)' ');
    bool     proceed = true;

    if (lx >= (uint16_t)EPD_H)                                             { proceed = false; }
    if (proceed && (ly >= (uint16_t)EPD_W))                                { proceed = false; }
    if (proceed && (((uint32_t)lx + (uint32_t)size1) > (uint32_t)EPD_H))  { proceed = false; }
    if (proceed && (((uint32_t)ly + EPD_CHAR_COL_8) > (uint32_t)EPD_W))   { proceed = false; }

    if (proceed) {
        if (size1 == EPD_CHAR_SIZE_8) {
            size2 = EPD_FONT8_SIZE2;
        } else {
            size2 = (uint16_t)((uint16_t)((size1 / EPD_PIXEL_BITS) +
                               (((size1 % EPD_PIXEL_BITS) != 0U) ? 1U : 0U)) *
                               (uint16_t)(size1 / 2U));
        }
    }

    for (uint16_t i = 0U; (i < size2) && proceed; i++) {
        uint16_t temp = 0U;
        bool     size_valid = true;

        if (size1 == EPD_CHAR_SIZE_8) {
            temp = asc2_0806[chr1][i];
        } else if (size1 == EPD_CHAR_SIZE_12) {
            temp = asc2_1206[chr1][i];
        } else if (size1 == EPD_CHAR_SIZE_16) {
            temp = asc2_1608[chr1][i];
        } else if (size1 == EPD_CHAR_SIZE_24) {
            temp = asc2_2412[chr1][i];
        } else {
            proceed    = false;
            size_valid = false;
        }

        if (size_valid) {
            for (uint16_t m = 0U; m < EPD_CHAR_COL_8; m++) {
                if (((uint32_t)ly + (uint32_t)m) < (uint32_t)EPD_W) {
                    if ((temp & EPD_LOW_BIT_MASK) != 0U) {
                        epd_paint_drawPoint(lx, (uint16_t)(ly + m), color);
                    } else {
                        uint16_t inv_color = (color != 0U) ? (uint16_t)0U : (uint16_t)1U;
                        epd_paint_drawPoint(lx, (uint16_t)(ly + m), inv_color);
                    }
                }
                temp = (uint16_t)(temp >> 1U);
            }
            lx = (uint16_t)(lx + 1U);
            if ((size1 != EPD_CHAR_SIZE_8) &&
                ((uint16_t)(lx - lx0) == (uint16_t)(size1 / 2U))) {
                lx  = lx0;
                ly0 = (uint16_t)(ly0 + EPD_CHAR_COL_8);
            }
            ly = ly0;
        }
    }
}

void epd_paint_showString(uint16_t x, uint16_t y, uint8_t *chr,
                           uint16_t size1, uint16_t color)
{
    uint16_t lx      = x;
    uint8_t  *ptr    = chr;
    bool     running = true;

    while ((*ptr != (uint8_t)'\0') && running) {
        epd_paint_showChar(lx, y, (uint16_t)*ptr, size1, color);
        ptr++;
        if (size1 == EPD_CHAR_SIZE_8) {
            lx = (uint16_t)(lx + EPD_CHAR_COL_6);
            if (lx > (uint16_t)(EPD_H - EPD_CHAR_COL_6)) {
                running = false;
            }
        } else {
            lx = (uint16_t)(lx + (uint16_t)(size1 / 2U));
            if (lx > (uint16_t)(EPD_H - (uint16_t)(size1 / 2U))) {
                running = false;
            }
        }
    }
}

static uint32_t epd_pow(uint16_t m, uint16_t n)
{
    uint32_t result = 1U;
    uint16_t rem    = n;
    while (rem != 0U) {
        result *= (uint32_t)m;
        rem--;
    }
    return result;
}

void epd_paint_showNum(uint16_t x, uint16_t y, uint32_t num,
                        uint16_t len, uint16_t size1, uint16_t color)
{
    uint16_t m = 0U;
    if (size1 == EPD_CHAR_SIZE_8) {
        m = 2U;
    }
    for (uint16_t t = 0U; t < len; t++) {
        uint8_t  temp        = (uint8_t)((num / epd_pow(10U, (uint16_t)(len - t - 1U))) % 10U);
        uint16_t half_plus_m = (uint16_t)((size1 / 2U) + (uint16_t)m);
        uint32_t prod        = (uint32_t)half_plus_m * (uint32_t)t;
        epd_paint_showChar(
            (uint16_t)(x + (uint16_t)prod),
            y,
            (uint16_t)((uint16_t)temp + (uint16_t)'0'),
            size1,
            color);
    }
}

void epd_paint_showChinese(uint16_t x, uint16_t y, uint16_t num,
                            uint16_t size1, uint16_t color)
{
    uint16_t lx    = (uint16_t)(x + 1U);
    uint16_t ly    = (uint16_t)(y + 1U);
    uint16_t lx0   = lx;
    uint16_t ly0   = ly;
    uint16_t size3 = (uint16_t)((uint16_t)((size1 / EPD_PIXEL_BITS) +
                               (((size1 % EPD_PIXEL_BITS) != 0U) ? 1U : 0U)) *
                               size1);
    bool     proceed = true;

    for (uint16_t i = 0U; (i < size3) && proceed; i++) {
        uint16_t temp = 0U;
        bool     size_valid = true;

        if (size1 == EPD_CHAR_SIZE_16) {
            temp = Hzk1[num][i];
        } else if (size1 == EPD_CHAR_SIZE_24) {
            temp = Hzk2[num][i];
        } else if (size1 == EPD_CHAR_SIZE_32) {
            temp = Hzk3[num][i];
        } else if (size1 == EPD_CHAR_SIZE_64) {
            temp = Hzk4[num][i];
        } else {
            proceed    = false;
            size_valid = false;
        }

        if (size_valid) {
            for (uint16_t m = 0U; m < EPD_CHAR_COL_8; m++) {
                if ((temp & EPD_LOW_BIT_MASK) != 0U) {
                    epd_paint_drawPoint(lx, ly, color);
                } else {
                    uint16_t inv_color = (color != 0U) ? (uint16_t)0U : (uint16_t)1U;
                    epd_paint_drawPoint(lx, ly, inv_color);
                }
                temp = (uint16_t)(temp >> 1U);
                ly   = (uint16_t)(ly + 1U);
            }
            lx = (uint16_t)(lx + 1U);
            if ((uint16_t)(lx - lx0) == size1) {
                lx  = lx0;
                ly0 = (uint16_t)(ly0 + EPD_CHAR_COL_8);
            }
            ly = ly0;
        }
    }
}

void epd_paint_showPicture(uint16_t x, uint16_t y, uint16_t sizex,
                            uint16_t sizey, const uint8_t BMP[], uint16_t Color)
{
    uint16_t lx    = (uint16_t)(x + 1U);
    uint16_t ly    = (uint16_t)(y + 1U);
    uint16_t lx0   = lx;
    uint16_t ly0   = ly;
    uint16_t rows  = (uint16_t)((sizey / EPD_PIXEL_BITS) +
                               (((sizey % EPD_PIXEL_BITS) != 0U) ? 1U : 0U));
    uint16_t j     = 0U;

    for (uint16_t n = 0U; n < rows; n++) {
        for (uint16_t i = 0U; i < sizex; i++) {
            uint16_t temp = (uint16_t)BMP[j];
            j++;
            for (uint16_t m = 0U; m < EPD_CHAR_COL_8; m++) {
                if ((temp & EPD_LOW_BIT_MASK) != 0U) {
                    uint16_t inv_color = (Color != 0U) ? (uint16_t)0U : (uint16_t)1U;
                    epd_paint_drawPoint(lx, ly, inv_color);
                } else {
                    epd_paint_drawPoint(lx, ly, Color);
                }
                temp = (uint16_t)(temp >> 1U);
                ly   = (uint16_t)(ly + 1U);
            }
            lx = (uint16_t)(lx + 1U);
            if ((uint16_t)(lx - lx0) == sizex) {
                lx  = lx0;
                ly0 = (uint16_t)(ly0 + EPD_CHAR_COL_8);
            }
            ly = ly0;
        }
    }
}
