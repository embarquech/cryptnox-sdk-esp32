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

/* Named constants — no magic numbers */
#define EPD_SPI_CHUNK_SIZE      (4096U)
#define EPD_SPI_MODE            (3U)
#define EPD_SPI_CLOCK_HZ        (4000000U)
#define EPD_SPI_CS_UNUSED       (-1)
#define EPD_SPI_MISO_UNUSED     (-1)
#define EPD_SPI_QUAD_UNUSED     (-1)
#define EPD_SPI_QUEUE_SIZE      (1U)
#define EPD_SPI_BUS_MAX_TRANSFER (4096)
#define EPD_BUSY_TIMEOUT_COUNT  (40000U)
#define EPD_DELAY_1MS           (1U)
#define EPD_RESET_DELAY_MS      (50U)
#define EPD_INIT_DELAY_MS       (100U)
#define EPD_UPDATE_DELAY_MS     (1U)
#define EPD_ADDR_SHIFT_3        (3U)
#define EPD_BYTE_MASK_8         (0xFFU)
#define EPD_SHIFT_8             (8U)
#define EPD_SHIFT_1             (1U)
#define EPD_BITS_PER_BYTE       (8U)
#define EPD_EPD154_Y_OFFSET     (199U)
#define EPD_EPD213_Y_OFFSET     (295U)
#define EPD_MAX_BUFF_SIZE       (12480U)
#define EPD_CHAR_SIZE_8         (8U)
#define EPD_CHAR_SIZE_12        (12U)
#define EPD_CHAR_SIZE_16        (16U)
#define EPD_CHAR_SIZE_24        (24U)
#define EPD_CHAR_SIZE_32        (32U)
#define EPD_CHAR_SIZE_64        (64U)
#define EPD_CHAR_WIDTH_8        (6U)
#define EPD_PIXEL_BIT_MASK      (0x80U)
#define EPD_LOW_BIT_MASK        (0x01U)
#define EPD_DRAW_TWO            (2)
#define EPD_DRAW_TWO_U          (2U)

static spi_device_handle_t epd_spi;
static int _pin_dc   = 0;
static int _pin_rst  = 0;
static int _pin_busy = 0;
static int _pin_cs   = 0;

uint16_t EPD_H = 0U;
uint16_t EPD_W = 0U;
EPD_PAINT EPD_Paint;
uint8_t epd_type = 0U;

static uint8_t _hibernating = 1U;
static uint8_t old_data[EPD_MAX_BUFF_SIZE];

/* Static invert/fill buffer — replaces malloc/free (MISRA 21.3) */
static uint8_t epd_invert_buf[EPD_MAX_BUFF_SIZE];

/* cppcheck-suppress misra-c2012-8.9 */
static const unsigned char lut_partial[] = {
    0x00U,0x40U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
    0x80U,0x80U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
    0x40U,0x40U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
    0x00U,0x80U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
    0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
    0x0AU,0x00U,0x00U,0x00U,0x00U,0x00U,0x02U,0x01U,0x00U,0x00U,0x00U,0x00U,
    0x00U,0x00U,0x01U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
    0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
    0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
    0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
    0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
    0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
    0x22U,0x22U,0x22U,0x22U,0x22U,0x22U,0x00U,0x00U,0x00U,
};

/* --- HAL: ESP-IDF SPI + GPIO --- */

static void epd_delay(uint16_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static void epd_res_set(void)
{
    (void)gpio_set_level(_pin_rst, 1U);
}

static void epd_res_reset(void)
{
    (void)gpio_set_level(_pin_rst, 0U);
}

static void epd_dc_set(void)
{
    (void)gpio_set_level(_pin_dc, 1U);
}

static void epd_dc_reset(void)
{
    (void)gpio_set_level(_pin_dc, 0U);
}

static void epd_cs_set(void)
{
    (void)gpio_set_level(_pin_cs, 1U);
}

static void epd_cs_reset(void)
{
    (void)gpio_set_level(_pin_cs, 0U);
}

static uint8_t epd_is_busy(void)
{
    uint8_t result = 0U;
    if (epd_type == (uint8_t)EPD370_UC8253) {
        result = (gpio_get_level(_pin_busy) != 0) ? (uint8_t)0U : (uint8_t)1U;
    } else {
        result = (gpio_get_level(_pin_busy) != 0) ? (uint8_t)1U : (uint8_t)0U;
    }
    return result;
}

static void spi_send(const uint8_t *data, uint32_t len)
{
    spi_transaction_t t = {
        .length    = (size_t)len * (size_t)EPD_BITS_PER_BYTE,
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

static void _epd_write_data(const uint8_t *data, uint32_t len)
{
    uint32_t chunk_size = EPD_SPI_CHUNK_SIZE;
    uint32_t remaining  = len;
    uint32_t offset     = 0U;

    while (remaining > 0U) {
        uint32_t current_chunk = (remaining > chunk_size) ? chunk_size : remaining;
        spi_send(&data[offset], current_chunk);
        offset    += current_chunk;
        remaining -= current_chunk;
    }
}

/* --- Public HAL init --- */

esp_err_t epd_io_init(const epd_config_t *config)
{
    esp_err_t ret = ESP_OK;

    _pin_dc   = config->pin_dc;
    _pin_rst  = config->pin_rst;
    _pin_busy = config->pin_busy;
    _pin_cs   = config->pin_cs;

    /* Configure GPIO outputs */
    uint64_t out_mask = ((1ULL << (uint32_t)_pin_dc)
                       | (1ULL << (uint32_t)_pin_rst)
                       | (1ULL << (uint32_t)_pin_cs));
    gpio_config_t io_out = {
        .pin_bit_mask = out_mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io_out);

    /* Configure BUSY as input */
    gpio_config_t io_in = {
        .pin_bit_mask = (1ULL << (uint32_t)_pin_busy),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io_in);

    epd_cs_set();
    epd_dc_set();
    epd_res_set();

    /* SPI bus (skip if already initialized externally) */
    if (!config->skip_bus_init) {
        spi_bus_config_t buscfg = {
            .mosi_io_num     = config->pin_mosi,
            .miso_io_num     = EPD_SPI_MISO_UNUSED,
            .sclk_io_num     = config->pin_sclk,
            .quadwp_io_num   = EPD_SPI_QUAD_UNUSED,
            .quadhd_io_num   = EPD_SPI_QUAD_UNUSED,
            .max_transfer_sz = EPD_SPI_BUS_MAX_TRANSFER,
        };
        ret = spi_bus_initialize(config->spi_host, &buscfg, SPI_DMA_CH_AUTO);
    }

    if (ret == ESP_OK) {
        /* SSD1683: SPI mode 3, MSB first (per WeAct RPi reference) */
        spi_device_interface_config_t devcfg = {
            .mode           = EPD_SPI_MODE,
            .clock_speed_hz = EPD_SPI_CLOCK_HZ,
            .spics_io_num   = EPD_SPI_CS_UNUSED,
            .queue_size     = EPD_SPI_QUEUE_SIZE,
        };
        ret = spi_bus_add_device(config->spi_host, &devcfg, &epd_spi);
    }

    if (ret == ESP_OK) {
        ESP_LOGI(EPD_LOG_TAG, "EPD SPI initialized");
    }
    return ret;
}

void epd_io_deinit(void)
{
    (void)spi_bus_remove_device(epd_spi);
}

/* --- Core EPD functions (from WeAct original) --- */

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
    uint8_t  result  = 0U;
    int      level   = gpio_get_level(_pin_busy);
    ESP_LOGI(EPD_LOG_TAG, "wait_busy: BUSY pin=%d (raw level=%d)", _pin_busy, level);
    while ((epd_is_busy() != 0U) && (result == 0U)) {
        timeout++;
        if (timeout > (uint32_t)EPD_BUSY_TIMEOUT_COUNT) {
            result = 1U;
        } else {
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
    _hibernating = 0U;
}

void epd_set_panel(uint8_t type, uint16_t width, uint16_t height)
{
    epd_type = type;
    EPD_H    = height;
    EPD_W    = width;
    ESP_LOGI(EPD_LOG_TAG, "Panel: %dx%d, type: %d", EPD_W, EPD_H, epd_type);
}

uint8_t epd_init(void)
{
    uint8_t busy_result = 0U;
    uint8_t result      = 0U;
    bool    done        = false;

    if (_hibernating != 0U) {
        epd_reset();
    }

    busy_result = epd_wait_busy();
    if (busy_result != 0U) {
        result = 1U;
        done   = true;
    }

    if (!done && (epd_type == (uint8_t)EPD370_UC8253)) {
        epd_write_reg(0x04U);
        busy_result = epd_wait_busy();
        if (busy_result != 0U) {
            result = 1U;
        } else {
            epd_write_reg(0x00U);
            epd_write_data(0x1FU);
            epd_write_data(0x0DU);
            epd_write_reg(0x50U);
            epd_write_data(0x97U);
        }
        done = true;
    }

    if (!done) {
        epd_write_reg(0x12U); /* SWRESET */
        epd_delay(EPD_INIT_DELAY_MS);
        busy_result = epd_wait_busy();
        if (busy_result != 0U) {
            result = 1U;
            done   = true;
        }
    }

    if (!done) {
        if ((epd_type == (uint8_t)EPD213_219) || (epd_type == (uint8_t)EPD154)) {
            epd_write_reg(0x01U);
            if (epd_type == (uint8_t)EPD213_219) {
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
            if (epd_type == (uint8_t)EPD154) {
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
            if (epd_type == (uint8_t)EPD213_219) {
                epd_write_reg(0x21U);
                epd_write_data(0x00U);
                epd_write_data(0x80U);
            }
        } else if (epd_type == (uint8_t)EPD420) {
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
            epd_address_set(0U, 0U, (uint16_t)(EPD_W - 1U), (uint16_t)(EPD_H - 1U));
        } else {
            /* no additional init for other types */
        }

        epd_write_reg(0x18U);
        epd_write_data(0x80U);
        epd_setpos(0U, 0U);

        busy_result = epd_power_on();
        if (busy_result != 0U) {
            result = 1U;
        }
    }
    return result;
}

uint8_t epd_init_fast(void)
{
    uint8_t init_result = 0U;
    uint8_t busy_result = 0U;
    uint8_t result      = 0U;

    init_result = epd_init();
    if (init_result != 0U) {
        result = 1U;
    } else if (epd_type == (uint8_t)EPD370_UC8253) {
        epd_write_reg(0xE0U);
        epd_write_data(0x02U);
        epd_write_reg(0xE5U);
        epd_write_data(0x5FU);
    } else {
        epd_write_reg(0x22U);
        epd_write_data(0xB1U);
        epd_write_reg(0x20U);
        busy_result = epd_wait_busy();
        if (busy_result != 0U) {
            result = 1U;
        } else {
            epd_write_reg(0x1AU);
            epd_write_data(0x6EU);
            epd_write_reg(0x22U);
            epd_write_data(0x91U);
            epd_write_reg(0x20U);
            busy_result = epd_wait_busy();
            if (busy_result != 0U) {
                result = 1U;
            }
        }
    }
    return result;
}

uint8_t epd_init_partial(void)
{
    uint8_t init_result = 0U;
    uint8_t result      = 0U;

    init_result = epd_init();
    if (init_result != 0U) {
        result = 1U;
    } else {
        if (epd_type == (uint8_t)EPD213_219) {
            epd_write_reg(0x32U);
            epd_cs_reset();
            _epd_write_data(lut_partial, sizeof(lut_partial));
            epd_cs_set();
        } else if (epd_type == (uint8_t)EPD420) {
            epd_write_reg(0x3CU);
            epd_write_data(0x80U);
            epd_write_reg(0x21U);
            epd_write_data(0x00U);
            epd_write_data(0x00U);
        } else if (epd_type == (uint8_t)EPD370_UC8253) {
            epd_write_reg(0xE0U);
            epd_write_data(0x02U);
            epd_write_reg(0xE5U);
            epd_write_data(0x6EU);
            epd_write_reg(0x50U);
            epd_write_data(0xD7U);
        } else {
            /* no partial init for other types */
        }
    }
    return result;
}

void epd_enter_deepsleepmode(uint8_t mode)
{
    (void)epd_power_off();
    if (epd_type == (uint8_t)EPD370_UC8253) {
        epd_write_reg(0x07U);
        epd_write_data(0xA5U);
    } else {
        epd_write_reg(0x10U);
        epd_write_data(mode);
    }
    _hibernating = 1U;
}

uint8_t epd_power_on(void)
{
    uint8_t result = 0U;

    if (epd_type == (uint8_t)EPD370_UC8253) {
        epd_write_reg(0x04U);
        result = epd_wait_busy();
    } else {
        if (epd_type == (uint8_t)EPD420) {
            epd_write_reg(0x22U);
            epd_write_data(0xe0U);
        } else {
            epd_write_reg(0x22U);
            epd_write_data(0xf8U);
        }
        epd_write_reg(0x20U);
        result = epd_wait_busy();
    }
    return result;
}

uint8_t epd_power_off(void)
{
    uint8_t result = 0U;

    if (epd_type == (uint8_t)EPD370_UC8253) {
        epd_write_reg(0x02U);
    } else {
        epd_write_reg(0x22U);
        epd_write_data(0x83U);
        epd_write_reg(0x20U);
    }
    result = epd_wait_busy();
    return result;
}

void epd_init_internalTempSensor(void)
{
    if (epd_type != (uint8_t)EPD370_UC8253) {
        epd_write_reg(0x18U);
        epd_write_data(0x80U);
        epd_write_reg(0x1AU);
        epd_write_data(0x7FU);
    }
}

void epd_update(void)
{
    if (epd_type == (uint8_t)EPD370_UC8253) {
        epd_write_reg(0x12U);
        epd_delay(EPD_UPDATE_DELAY_MS);
        (void)epd_wait_busy();
    } else {
        if (epd_type == (uint8_t)EPD154) {
            epd_write_reg(0x22U);
            epd_write_data(0xF4U);
        } else {
            epd_write_reg(0x22U);
            epd_write_data(0xF7U);
        }
        epd_write_reg(0x20U);
        (void)epd_wait_busy();
    }
}

void epd_update_fast(void)
{
    if (epd_type == (uint8_t)EPD370_UC8253) {
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
    if (epd_type == (uint8_t)EPD370_UC8253) {
        epd_update();
    } else {
        if (epd_type == (uint8_t)EPD154) {
            epd_write_reg(0x22U);
            epd_write_data(0xFCU);
        } else if (epd_type == (uint8_t)EPD420) {
            epd_write_reg(0x22U);
            epd_write_data(0xFFU);
        } else {
            epd_write_reg(0x22U);
            epd_write_data(0xCCU);
        }
        epd_write_reg(0x20U);
        (void)epd_wait_busy();
    }
}

void epd_address_set(uint16_t x_start, uint16_t y_start, uint16_t x_end, uint16_t y_end)
{
    if (epd_type == (uint8_t)EPD370_UC8253) {
        epd_write_reg(0x90U);
        epd_write_data((uint8_t)((uint16_t)(x_start >> EPD_ADDR_SHIFT_3) & (uint16_t)EPD_BYTE_MASK_8));
        epd_write_data((uint8_t)((uint16_t)(x_end   >> EPD_ADDR_SHIFT_3) & (uint16_t)EPD_BYTE_MASK_8));
        epd_write_data((uint8_t)(y_start & (uint16_t)EPD_BYTE_MASK_8));
        epd_write_data((uint8_t)((uint16_t)(y_start >> EPD_SHIFT_8) & (uint16_t)EPD_BYTE_MASK_8));
        epd_write_data((uint8_t)(y_end   & (uint16_t)EPD_BYTE_MASK_8));
        epd_write_data((uint8_t)((uint16_t)(y_end   >> EPD_SHIFT_8) & (uint16_t)EPD_BYTE_MASK_8));
        epd_write_data(0x01U);
    } else {
        epd_write_reg(0x44U);
        epd_write_data((uint8_t)((uint16_t)(x_start >> EPD_ADDR_SHIFT_3) & (uint16_t)EPD_BYTE_MASK_8));
        epd_write_data((uint8_t)((uint16_t)(x_end   >> EPD_ADDR_SHIFT_3) & (uint16_t)EPD_BYTE_MASK_8));
        epd_write_reg(0x45U);
        epd_write_data((uint8_t)(y_start & (uint16_t)EPD_BYTE_MASK_8));
        epd_write_data((uint8_t)((uint16_t)(y_start >> EPD_SHIFT_8) & (uint16_t)EPD_BYTE_MASK_8));
        epd_write_data((uint8_t)(y_end   & (uint16_t)EPD_BYTE_MASK_8));
        epd_write_data((uint8_t)((uint16_t)(y_end   >> EPD_SHIFT_8) & (uint16_t)EPD_BYTE_MASK_8));
    }
}

void epd_setpos(uint16_t x, uint16_t y)
{
    uint8_t  _x = (uint8_t)(x / EPD_BITS_PER_BYTE);
    uint16_t _y = 0U;

    if (epd_type == (uint8_t)EPD154) {
        _y = (uint16_t)(EPD_EPD154_Y_OFFSET - (uint16_t)y);
    } else if (epd_type == (uint8_t)EPD213_219) {
        _y = (uint16_t)(EPD_EPD213_Y_OFFSET - (uint16_t)y);
    } else {
        _y = y;
    }

    if (epd_type == (uint8_t)EPD370_UC8253) {
        epd_write_reg(0x65U);
        epd_write_data(_x);
        epd_write_data((uint8_t)((_y >> EPD_SHIFT_8) & (uint16_t)EPD_SHIFT_1));
        epd_write_data((uint8_t)(_y & (uint16_t)EPD_BYTE_MASK_8));
    } else {
        epd_write_reg(0x4EU);
        epd_write_data(_x);
        epd_write_reg(0x4FU);
        epd_write_data((uint8_t)(_y & (uint16_t)EPD_BYTE_MASK_8));
        epd_write_data((uint8_t)((_y >> EPD_SHIFT_8) & (uint16_t)EPD_SHIFT_1));
    }
}

void epd_write_imagedata(const uint8_t *Image1, uint32_t length)
{
    epd_cs_reset();
    _epd_write_data(Image1, length);
    epd_cs_set();
}

static void epd_write_imagedata_invert(const uint8_t *Image1, uint32_t length)
{
    if (length <= (uint32_t)EPD_MAX_BUFF_SIZE) {
        for (uint32_t j = 0U; j < length; j++) {
            epd_invert_buf[j] = (uint8_t)(~Image1[j]);
        }
        epd_cs_reset();
        _epd_write_data(epd_invert_buf, length);
        epd_cs_set();
    }
}

static void epd_write_imagedata2(uint8_t data_byte, uint32_t length)
{
    if (length <= (uint32_t)EPD_MAX_BUFF_SIZE) {
        (void)memset(epd_invert_buf, (int)data_byte, (size_t)length);
        epd_cs_reset();
        _epd_write_data(epd_invert_buf, length);
        epd_cs_set();
    }
}

void epd_display(const uint8_t *Image1, const uint8_t *Image2)
{
    uint32_t length = 0U;

    if (epd_type != (uint8_t)EPD370_UC8253) {
        length = (uint32_t)EPD_H * (uint32_t)EPD_W_BUFF_SIZE;

        epd_setpos(0U, 0U);
        epd_write_reg(0x24U);
        epd_write_imagedata(Image1, length);

        epd_setpos(0U, 0U);
        epd_write_reg(0x26U);
        epd_write_imagedata_invert(Image2, length);

        if (epd_type == (uint8_t)EPD420) {
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

    if (epd_type == (uint8_t)EPD370_UC8253) {
        epd_write_reg(0x10U);
        epd_write_imagedata(old_data, length);
        epd_write_reg(0x13U);
        epd_write_imagedata(Image, length);
        (void)memcpy(old_data, Image, (size_t)length);
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

    if (epd_type == (uint8_t)EPD370_UC8253) {
        epd_write_reg(0x10U);
        epd_write_imagedata(old_data, length);
        epd_write_reg(0x13U);
        epd_write_imagedata(Image, length);
        (void)memcpy(old_data, Image, (size_t)length);
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
    uint32_t length = 0U;

    if (epd_type == (uint8_t)EPD370_UC8253) {
        epd_displayBW(Image);
    } else {
        length = (uint32_t)EPD_H * (uint32_t)EPD_W_BUFF_SIZE;

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

    if (epd_type == (uint8_t)EPD420) {
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

    if (epd_type == (uint8_t)EPD420) {
        epd_write_reg(0x21U);
        epd_write_data(0x00U);
        epd_write_data(0x00U);
    }
    epd_update();
}

/* --- Paint functions --- */

void epd_paint_newimage(uint8_t *image, uint16_t Width, uint16_t Height, uint16_t Rotate, uint16_t Color)
{
    EPD_Paint.Image        = image;
    EPD_Paint.WidthMemory  = Width;
    EPD_Paint.HeightMemory = Height;
    EPD_Paint.Color        = Color;
    EPD_Paint.WidthByte    = ((Width % EPD_BITS_PER_BYTE) == 0U)
                                 ? (uint16_t)(Width / EPD_BITS_PER_BYTE)
                                 : (uint16_t)((Width / EPD_BITS_PER_BYTE) + 1U);
    EPD_Paint.HeightByte   = Height;
    EPD_Paint.Rotate       = Rotate;
    if ((Rotate == (uint16_t)EPD_ROTATE_0) || (Rotate == (uint16_t)EPD_ROTATE_180)) {
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
    uint32_t Addr  = 0U;
    uint8_t  pixel = 0U;
    bool     valid = false;

    switch (EPD_Paint.Rotate) {
    case 0U:
        X     = (uint16_t)(EPD_Paint.WidthMemory - Ypoint - 1U);
        Y     = Xpoint;
        valid = true;
        break;
    case 90U:
        X     = (uint16_t)(EPD_Paint.WidthMemory - Xpoint - 1U);
        Y     = (uint16_t)(EPD_Paint.HeightMemory - Ypoint - 1U);
        valid = true;
        break;
    case 180U:
        X     = Ypoint;
        Y     = (uint16_t)(EPD_Paint.HeightMemory - Xpoint - 1U);
        valid = true;
        break;
    case 270U:
        X     = Xpoint;
        Y     = Ypoint;
        valid = true;
        break;
    default:
        break;
    }

    if (valid) {
        Addr  = ((uint32_t)X / (uint32_t)EPD_BITS_PER_BYTE) + ((uint32_t)Y * (uint32_t)EPD_Paint.WidthByte);
        pixel = (uint8_t)(EPD_PIXEL_BIT_MASK >> (uint8_t)(X % EPD_BITS_PER_BYTE));
        if (Color == (uint16_t)EPD_COLOR_BLACK) {
            EPD_Paint.Image[Addr] = (uint8_t)(EPD_Paint.Image[Addr] & (uint8_t)(~pixel));
        } else {
            EPD_Paint.Image[Addr] = (uint8_t)(EPD_Paint.Image[Addr] | pixel);
        }
    }
}

void epd_paint_clear(uint16_t color)
{
    uint16_t Y = 0U;
    uint16_t X = 0U;
    for (Y = 0U; Y < EPD_Paint.HeightByte; Y++) {
        for (X = 0U; X < EPD_Paint.WidthByte; X++) {
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

void epd_paint_drawLine(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend, uint16_t Color)
{
    uint16_t Xpoint   = Xstart;
    uint16_t Ypoint   = Ystart;
    int32_t  dx       = ((int32_t)Xend - (int32_t)Xstart >= (int32_t)0)
                            ? ((int32_t)Xend - (int32_t)Xstart)
                            : ((int32_t)Xstart - (int32_t)Xend);
    int32_t  dy       = ((int32_t)Yend - (int32_t)Ystart <= (int32_t)0)
                            ? ((int32_t)Yend - (int32_t)Ystart)
                            : ((int32_t)Ystart - (int32_t)Yend);
    int32_t  XAddway  = (Xstart < Xend) ? (int32_t)1 : (int32_t)-1;
    int32_t  YAddway  = (Ystart < Yend) ? (int32_t)1 : (int32_t)-1;
    int32_t  Esp      = dx + dy;

    bool done = false;
    while (!done) {
        epd_paint_drawPoint(Xpoint, Ypoint, Color);
        if ((EPD_DRAW_TWO * Esp) >= dy) {
            if (Xpoint == Xend) {
                done = true;
            } else {
                Esp    += dy;
                /* cppcheck-suppress misra-c2012-10.8 */
                Xpoint  = (uint16_t)((int32_t)Xpoint + XAddway);
            }
        }
        if (!done && ((EPD_DRAW_TWO * Esp) <= dx)) {
            if (Ypoint == Yend) {
                done = true;
            } else {
                Esp    += dx;
                /* cppcheck-suppress misra-c2012-10.8 */
                Ypoint  = (uint16_t)((int32_t)Ypoint + YAddway);
            }
        }
    }
}

void epd_paint_drawRectangle(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend, uint16_t Color, uint8_t mode)
{
    uint16_t i = 0U;
    if (mode != 0U) {
        for (i = Ystart; i < Yend; i++) {
            epd_paint_drawLine(Xstart, i, Xend, i, Color);
        }
    } else {
        epd_paint_drawLine(Xstart, Ystart, Xend,   Ystart, Color);
        epd_paint_drawLine(Xstart, Ystart, Xstart, Yend,   Color);
        epd_paint_drawLine(Xend,   Yend,   Xend,   Ystart, Color);
        epd_paint_drawLine(Xend,   Yend,   Xstart, Yend,   Color);
    }
}

void epd_paint_drawCircle(uint16_t X_Center, uint16_t Y_Center, uint16_t Radius, uint16_t Color, uint8_t mode)
{
    int16_t f      = (int16_t)(1 - (int16_t)Radius);
    int16_t ddF_x  = (int16_t)1;
    int16_t ddF_y  = (int16_t)(-EPD_DRAW_TWO * (int16_t)Radius);
    int16_t x      = (int16_t)0;
    int16_t y      = (int16_t)Radius;

    if (mode != 0U) {
        epd_paint_drawLine((uint16_t)(X_Center - Radius), Y_Center, (uint16_t)(X_Center + Radius), Y_Center, Color);
    } else {
        epd_paint_drawPoint(X_Center,                    (uint16_t)(Y_Center + Radius), Color);
        epd_paint_drawPoint(X_Center,                    (uint16_t)(Y_Center - Radius), Color);
        epd_paint_drawPoint((uint16_t)(X_Center + Radius), Y_Center,                   Color);
        epd_paint_drawPoint((uint16_t)(X_Center - Radius), Y_Center,                   Color);
    }

    while (x < y) {
        if (f >= (int16_t)0) {
            y--;
            ddF_y += (int16_t)EPD_DRAW_TWO;
            f     += ddF_y;
        }
        x++;
        ddF_x += (int16_t)EPD_DRAW_TWO;
        f     += ddF_x;
        if (mode != 0U) {
            epd_paint_drawLine((uint16_t)(X_Center - (uint16_t)x), (uint16_t)(Y_Center + (uint16_t)y), (uint16_t)(X_Center + (uint16_t)x), (uint16_t)(Y_Center + (uint16_t)y), Color);
            epd_paint_drawLine((uint16_t)(X_Center - (uint16_t)x), (uint16_t)(Y_Center - (uint16_t)y), (uint16_t)(X_Center + (uint16_t)x), (uint16_t)(Y_Center - (uint16_t)y), Color);
            epd_paint_drawLine((uint16_t)(X_Center - (uint16_t)y), (uint16_t)(Y_Center + (uint16_t)x), (uint16_t)(X_Center + (uint16_t)y), (uint16_t)(Y_Center + (uint16_t)x), Color);
            epd_paint_drawLine((uint16_t)(X_Center - (uint16_t)y), (uint16_t)(Y_Center - (uint16_t)x), (uint16_t)(X_Center + (uint16_t)y), (uint16_t)(Y_Center - (uint16_t)x), Color);
        } else {
            epd_paint_drawPoint((uint16_t)(X_Center + (uint16_t)x), (uint16_t)(Y_Center + (uint16_t)y), Color);
            epd_paint_drawPoint((uint16_t)(X_Center - (uint16_t)x), (uint16_t)(Y_Center + (uint16_t)y), Color);
            epd_paint_drawPoint((uint16_t)(X_Center + (uint16_t)x), (uint16_t)(Y_Center - (uint16_t)y), Color);
            epd_paint_drawPoint((uint16_t)(X_Center - (uint16_t)x), (uint16_t)(Y_Center - (uint16_t)y), Color);
            epd_paint_drawPoint((uint16_t)(X_Center + (uint16_t)y), (uint16_t)(Y_Center + (uint16_t)x), Color);
            epd_paint_drawPoint((uint16_t)(X_Center - (uint16_t)y), (uint16_t)(Y_Center + (uint16_t)x), Color);
            epd_paint_drawPoint((uint16_t)(X_Center + (uint16_t)y), (uint16_t)(Y_Center - (uint16_t)x), Color);
            epd_paint_drawPoint((uint16_t)(X_Center - (uint16_t)y), (uint16_t)(Y_Center - (uint16_t)x), Color);
        }
    }
}

void epd_paint_showChar(uint16_t x, uint16_t y, uint16_t chr, uint16_t size1, uint16_t color)
{
    uint16_t x_pos  = (uint16_t)(x + 1U);
    uint16_t y_pos  = (uint16_t)(y + 1U);
    uint16_t x0     = x_pos;
    uint16_t y0     = y_pos;
    uint16_t i      = 0U;
    uint16_t m      = 0U;
    uint16_t temp   = 0U;
    uint16_t size2  = 0U;
    uint16_t chr1   = 0U;
    bool     valid  = false;

    valid = ((x_pos < EPD_H) &&
             (y_pos < EPD_W) &&
             ((uint16_t)(x_pos + size1) <= EPD_H) &&
             ((uint16_t)(y_pos + (uint16_t)EPD_CHAR_WIDTH_8) <= EPD_W));

    if (valid) {
        if (size1 == (uint16_t)EPD_CHAR_SIZE_8) {
            size2 = (uint16_t)EPD_CHAR_WIDTH_8;
        } else {
            size2 = (uint16_t)(((size1 / EPD_CHAR_SIZE_8) + (((size1 % EPD_CHAR_SIZE_8) != 0U) ? 1U : 0U))
                                * (size1 / EPD_DRAW_TWO_U));
        }

        chr1 = (uint16_t)((uint16_t)chr - (uint16_t)' ');
        for (i = 0U; (i < size2) && valid; i++) {
            if (size1 == (uint16_t)EPD_CHAR_SIZE_8) {
                temp = (uint16_t)asc2_0806[chr1][i];
            } else if (size1 == (uint16_t)EPD_CHAR_SIZE_12) {
                temp = (uint16_t)asc2_1206[chr1][i];
            } else if (size1 == (uint16_t)EPD_CHAR_SIZE_16) {
                temp = (uint16_t)asc2_1608[chr1][i];
            } else if (size1 == (uint16_t)EPD_CHAR_SIZE_24) {
                temp = (uint16_t)asc2_2412[chr1][i];
            } else {
                valid = false;
            }

            if (valid) {
                for (m = 0U; m < (uint16_t)EPD_CHAR_SIZE_8; m++) {
                    if ((uint16_t)(y_pos + m) < EPD_W) {
                        if ((temp & (uint16_t)EPD_LOW_BIT_MASK) != 0U) {
                            epd_paint_drawPoint(x_pos, (uint16_t)(y_pos + m), color);
                        } else {
                            epd_paint_drawPoint(x_pos, (uint16_t)(y_pos + m), (uint16_t)(~color));
                        }
                        temp >>= 1U;
                    }
                }
                x_pos++;
                if ((size1 != (uint16_t)EPD_CHAR_SIZE_8) && (((uint16_t)(x_pos - x0)) == (uint16_t)(size1 / EPD_DRAW_TWO_U))) {
                    x_pos = x0;
                    y0    = (uint16_t)(y0 + (uint16_t)EPD_CHAR_SIZE_8);
                }
                y_pos = y0;
            }
        }
    }
}

void epd_paint_showString(uint16_t x, uint16_t y, uint8_t *chr, uint16_t size1, uint16_t color)
{
    uint8_t *chr_ptr = chr;
    uint16_t x_cur  = x;
    bool     done   = false;

    while ((*chr_ptr != (uint8_t)'\0') && (!done)) {
        epd_paint_showChar(x_cur, y, (uint16_t)*chr_ptr, size1, color);
        chr_ptr++;
        if (size1 == (uint16_t)EPD_CHAR_SIZE_8) {
            x_cur = (uint16_t)(x_cur + (uint16_t)EPD_CHAR_WIDTH_8);
            if (x_cur > (uint16_t)(EPD_H - (uint16_t)EPD_CHAR_WIDTH_8)) {
                done = true;
            }
        } else {
            x_cur = (uint16_t)(x_cur + (uint16_t)(size1 / EPD_DRAW_TWO_U));
            if (x_cur > (uint16_t)(EPD_H - (uint16_t)(size1 / EPD_DRAW_TWO_U))) {
                done = true;
            }
        }
    }
}

static uint32_t epd_pow(uint16_t m, uint16_t n)
{
    uint32_t result = 1U;
    uint16_t count  = n;
    while (count != 0U) {
        result *= (uint32_t)m;
        count--;
    }
    return result;
}

void epd_paint_showNum(uint16_t x, uint16_t y, uint32_t num, uint16_t len, uint16_t size1, uint16_t color)
{
    uint16_t t    = 0U;
    uint8_t  temp = 0U;
    uint8_t  m    = 0U;

    if (size1 == (uint16_t)EPD_CHAR_SIZE_8) {
        m = (uint8_t)EPD_DRAW_TWO_U;
    }
    for (t = 0U; t < len; t++) {
        temp = (uint8_t)((num / epd_pow((uint16_t)10U, (uint16_t)(len - t - 1U))) % (uint32_t)10U);
        epd_paint_showChar(
            (uint16_t)(x + ((uint16_t)(size1 / (uint16_t)EPD_DRAW_TWO_U) + (uint16_t)m) * t),
            y,
            (uint16_t)((uint16_t)temp + (uint16_t)'0'),
            size1,
            color);
    }
}

void epd_paint_showChinese(uint16_t x, uint16_t y, uint16_t num, uint16_t size1, uint16_t color)
{
    uint16_t x_pos  = (uint16_t)(x + 1U);
    uint16_t y_pos  = (uint16_t)(y + 1U);
    uint16_t x0     = x_pos;
    uint16_t y0     = y_pos;
    uint16_t m      = 0U;
    uint16_t temp   = 0U;
    uint16_t i      = 0U;
    uint16_t size3  = (uint16_t)(((size1 / EPD_CHAR_SIZE_8) + (((size1 % EPD_CHAR_SIZE_8) != 0U) ? 1U : 0U)) * size1);
    bool     valid  = true;

    for (i = 0U; (i < size3) && valid; i++) {
        if (size1 == (uint16_t)EPD_CHAR_SIZE_16) {
            temp = (uint16_t)Hzk1[num][i];
        } else if (size1 == (uint16_t)EPD_CHAR_SIZE_24) {
            temp = (uint16_t)Hzk2[num][i];
        } else if (size1 == (uint16_t)EPD_CHAR_SIZE_32) {
            temp = (uint16_t)Hzk3[num][i];
        } else if (size1 == (uint16_t)EPD_CHAR_SIZE_64) {
            temp = (uint16_t)Hzk4[num][i];
        } else {
            valid = false;
        }
        if (valid) {
            for (m = 0U; m < (uint16_t)EPD_CHAR_SIZE_8; m++) {
                if ((temp & (uint16_t)EPD_LOW_BIT_MASK) != 0U) {
                    epd_paint_drawPoint(x_pos, y_pos, color);
                } else {
                    epd_paint_drawPoint(x_pos, y_pos, (uint16_t)(~color));
                }
                temp >>= 1U;
                y_pos++;
            }
            x_pos++;
            if ((uint16_t)(x_pos - x0) == size1) {
                x_pos = x0;
                y0    = (uint16_t)(y0 + (uint16_t)EPD_CHAR_SIZE_8);
            }
            y_pos = y0;
        }
    }
}

void epd_paint_showPicture(uint16_t x, uint16_t y, uint16_t sizex, uint16_t sizey, const uint8_t BMP[], uint16_t Color)
{
    uint16_t x_pos  = (uint16_t)(x + 1U);
    uint16_t y_pos  = (uint16_t)(y + 1U);
    uint16_t x0     = x_pos;
    uint16_t y0     = y_pos;
    uint16_t j      = 0U;
    uint16_t i      = 0U;
    uint16_t n      = 0U;
    uint16_t temp   = 0U;
    uint16_t m      = 0U;
    uint16_t rows   = (uint16_t)((sizey / (uint16_t)EPD_CHAR_SIZE_8)
                                 + (((sizey % (uint16_t)EPD_CHAR_SIZE_8) != 0U) ? 1U : 0U));

    for (n = 0U; n < rows; n++) {
        for (i = 0U; i < sizex; i++) {
            temp = (uint16_t)BMP[j];
            j++;
            for (m = 0U; m < (uint16_t)EPD_CHAR_SIZE_8; m++) {
                if ((temp & (uint16_t)EPD_LOW_BIT_MASK) != 0U) {
                    epd_paint_drawPoint(x_pos, y_pos, (uint16_t)(~Color));
                } else {
                    epd_paint_drawPoint(x_pos, y_pos, Color);
                }
                temp >>= 1U;
                y_pos++;
            }
            x_pos++;
            if ((uint16_t)(x_pos - x0) == sizex) {
                x_pos = x0;
                y0    = (uint16_t)(y0 + (uint16_t)EPD_CHAR_SIZE_8);
            }
            y_pos = y0;
        }
    }
}
