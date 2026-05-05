// PN532 SPI driver for ESP-IDF
// Ported from Adafruit/Seeed PN532 Arduino library (MIT license)

#include "pn532.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *PN532_TAG = "pn532";

#define PN532_PREAMBLE              (0x00U)
#define PN532_STARTCODE2            (0xFFU)
#define PN532_POSTAMBLE             (0x00U)
#define PN532_HOSTTOPN532           (0xD4U)

#define PN532_FIRMWAREVERSION       (0x02U)
#define PN532_SAMCONFIGURATION      (0x14U)
#define PN532_INLISTPASSIVETARGET   (0x4AU)

#define PN532_SPI_STATREAD          (0x02U)
#define PN532_SPI_DATAWRITE         (0x01U)
#define PN532_SPI_DATAREAD          (0x03U)
#define PN532_SPI_READY             (0x01U)

#define PN532_PACK_BUFF_SIZE        (64U)
#define PN532_ACK_FRAME_LEN         (6U)
#define PN532_FW_RESP_PREFIX_LEN    (7U)
#define PN532_FW_RESP_READ_LEN      (12U)
#define PN532_FW_IC_OFFSET          (7U)
#define PN532_FW_VER_OFFSET         (8U)
#define PN532_FW_REV_OFFSET         (9U)
#define PN532_FW_SUPPORT_OFFSET     (10U)
#define PN532_SAM_READ_LEN          (9U)
#define PN532_SAM_STATUS_OFFSET     (6U)
#define PN532_SAM_STATUS_OK         (0x15U)
#define PN532_CARD_READ_LEN         (21U)
#define PN532_CARD_NB_OFFSET        (8U)
#define PN532_CARD_NB_EXPECTED      (1U)
#define PN532_CARD_LEN_OFFSET       (13U)
#define PN532_CARD_ID_BASE_OFFSET   (14U)
#define PN532_TIMER_STEP_MS         (10U)
#define PN532_WAKEUP_BYTE           (0x55U)
#define PN532_WAKEUP_DELAY_MS       (1000U)
#define PN532_POST_SYNC_DELAY_MS    (100U)
#define PN532_CS_SETTLE_DELAY_MS    (2U)
#define PN532_READ_DELAY_MS         (1U)
#define PN532_BITS_PER_BYTE         (8U)
#define PN532_CID_SHIFT             (8U)
#define PN532_CMD_LEN_OFFSET        (1U)
#define PN532_CHECKSUM_INIT \
    ((uint8_t)(PN532_PREAMBLE + PN532_PREAMBLE + PN532_STARTCODE2))

/* SPI bus / device configuration constants */
#define PN532_SPI_PIN_UNUSED        (-1)
#define PN532_SPI_MAX_TRANSFER      (0)
#define PN532_SPI_MODE              (0U)
#define PN532_SPI_CLOCK_HZ          (1000000U)
#define PN532_SPI_QUEUE_SIZE        (1U)

/* Timeout used for all command/ACK exchanges (milliseconds) */
#define PN532_CMD_TIMEOUT_MS        (1000U)

static uint8_t pn532_packetbuffer[PN532_PACK_BUFF_SIZE];

/******************************************************************
 * Low-level SPI helpers
 ******************************************************************/

static void spi_write_byte(pn532_t *dev, uint8_t data)
{
    spi_transaction_t t = {
        .length    = PN532_BITS_PER_BYTE,
        .tx_buffer = &data,
    };
    (void)spi_device_transmit(dev->spi, &t);
}

static uint8_t spi_read_byte(pn532_t *dev)
{
    uint8_t rx = 0U;
    uint8_t tx = 0x00U;
    spi_transaction_t t = {
        .length    = PN532_BITS_PER_BYTE,
        .rxlength  = PN532_BITS_PER_BYTE,
        .tx_buffer = &tx,
        .rx_buffer = &rx,
    };
    (void)spi_device_transmit(dev->spi, &t);
    return rx;
}

static uint8_t read_spi_status(pn532_t *dev)
{
    (void)gpio_set_level(dev->pin_cs, 0);
    vTaskDelay(pdMS_TO_TICKS(PN532_CS_SETTLE_DELAY_MS));
    spi_write_byte(dev, PN532_SPI_STATREAD);
    uint8_t status = spi_read_byte(dev);
    (void)gpio_set_level(dev->pin_cs, 1);
    return status;
}

static void read_data(pn532_t *dev, uint8_t *buff, uint8_t n)
{
    (void)gpio_set_level(dev->pin_cs, 0);
    vTaskDelay(pdMS_TO_TICKS(PN532_CS_SETTLE_DELAY_MS));
    spi_write_byte(dev, PN532_SPI_DATAREAD);
    for (uint8_t i = 0U; i < n; i++) {
        vTaskDelay(pdMS_TO_TICKS(PN532_READ_DELAY_MS));
        buff[i] = spi_read_byte(dev);
    }
    (void)gpio_set_level(dev->pin_cs, 1);
}

static bool check_spi_ack(pn532_t *dev)
{
    static const uint8_t ack_frame[PN532_ACK_FRAME_LEN] = {
        0x00U, 0x00U, 0xFFU, 0x00U, 0xFFU, 0x00U
    };
    uint8_t ackbuff[PN532_ACK_FRAME_LEN];
    read_data(dev, ackbuff, (uint8_t)PN532_ACK_FRAME_LEN);
    return (memcmp(ackbuff, ack_frame, PN532_ACK_FRAME_LEN) == 0);
}

static void write_command(pn532_t *dev, const uint8_t *cmd, uint8_t cmd_len)
{
    uint8_t len      = (uint8_t)(cmd_len + PN532_CMD_LEN_OFFSET);
    uint8_t checksum = PN532_CHECKSUM_INIT;

    (void)gpio_set_level(dev->pin_cs, 0);
    vTaskDelay(pdMS_TO_TICKS(PN532_CS_SETTLE_DELAY_MS));
    spi_write_byte(dev, PN532_SPI_DATAWRITE);

    spi_write_byte(dev, PN532_PREAMBLE);
    spi_write_byte(dev, PN532_PREAMBLE);
    spi_write_byte(dev, PN532_STARTCODE2);

    spi_write_byte(dev, len);
    spi_write_byte(dev, (uint8_t)((uint8_t)(~len) + 1U));

    spi_write_byte(dev, PN532_HOSTTOPN532);
    checksum = (uint8_t)(checksum + PN532_HOSTTOPN532);

    for (uint8_t i = 0U; i < (uint8_t)(len - 1U); i++) {
        spi_write_byte(dev, cmd[i]);
        checksum = (uint8_t)(checksum + cmd[i]);
    }
    spi_write_byte(dev, (uint8_t)(~checksum));
    spi_write_byte(dev, PN532_POSTAMBLE);
    (void)gpio_set_level(dev->pin_cs, 1);
}

static bool send_command_check_ack(pn532_t *dev, const uint8_t *cmd,
                                   uint8_t cmd_len, uint16_t timeout)
{
    uint16_t timer     = 0U;
    bool     timed_out = false;
    bool     ack_ok    = false;
    bool     result    = false;

    write_command(dev, cmd, cmd_len);

    while ((read_spi_status(dev) != PN532_SPI_READY) && (!timed_out)) {
        if (timeout != 0U) {
            timer = (uint16_t)(timer + PN532_TIMER_STEP_MS);
            if (timer > timeout) {
                timed_out = true;
            }
        }
        if (!timed_out) {
            vTaskDelay(pdMS_TO_TICKS(PN532_TIMER_STEP_MS));
        }
    }

    if (!timed_out) {
        ack_ok = check_spi_ack(dev);
    }

    if (ack_ok) {
        timer     = 0U;
        timed_out = false;

        while ((read_spi_status(dev) != PN532_SPI_READY) && (!timed_out)) {
            if (timeout != 0U) {
                timer = (uint16_t)(timer + PN532_TIMER_STEP_MS);
                if (timer > timeout) {
                    timed_out = true;
                }
            }
            if (!timed_out) {
                vTaskDelay(pdMS_TO_TICKS(PN532_TIMER_STEP_MS));
            }
        }
        result = !timed_out;
    }

    return result;
}

/******************************************************************
 * Public API
 ******************************************************************/

esp_err_t pn532_init(pn532_t *dev, const pn532_config_t *config)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << (uint32_t)config->pin_cs),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&io_conf);
    (void)gpio_set_level(config->pin_cs, 1);
    dev->pin_cs = config->pin_cs;

    esp_err_t result = ESP_OK;

    if (!config->skip_bus_init) {
        spi_bus_config_t buscfg = {
            .mosi_io_num     = config->pin_mosi,
            .miso_io_num     = config->pin_miso,
            .sclk_io_num     = config->pin_sclk,
            .quadwp_io_num   = PN532_SPI_PIN_UNUSED,
            .quadhd_io_num   = PN532_SPI_PIN_UNUSED,
            .max_transfer_sz = PN532_SPI_MAX_TRANSFER,
        };
        result = spi_bus_initialize(config->spi_host, &buscfg, SPI_DMA_CH_AUTO);
    }

    if (result == ESP_OK) {
        spi_device_interface_config_t devcfg = {
            .mode           = PN532_SPI_MODE,
            .clock_speed_hz = PN532_SPI_CLOCK_HZ,
            .spics_io_num   = PN532_SPI_PIN_UNUSED,
            .queue_size     = PN532_SPI_QUEUE_SIZE,
            .flags          = SPI_DEVICE_BIT_LSBFIRST,
        };
        result = spi_bus_add_device(config->spi_host, &devcfg, &dev->spi);
    }

    if (result == ESP_OK) {
        (void)gpio_set_level(dev->pin_cs, 0);
        vTaskDelay(pdMS_TO_TICKS(PN532_CS_SETTLE_DELAY_MS));
        spi_write_byte(dev, PN532_WAKEUP_BYTE);
        spi_write_byte(dev, PN532_WAKEUP_BYTE);
        spi_write_byte(dev, 0x00U);
        spi_write_byte(dev, 0x00U);
        spi_write_byte(dev, 0x00U);
        (void)gpio_set_level(dev->pin_cs, 1);
        vTaskDelay(pdMS_TO_TICKS(PN532_WAKEUP_DELAY_MS));

        pn532_packetbuffer[0] = PN532_FIRMWAREVERSION;
        (void)send_command_check_ack(dev, pn532_packetbuffer, 1U, PN532_CMD_TIMEOUT_MS);
        vTaskDelay(pdMS_TO_TICKS(PN532_POST_SYNC_DELAY_MS));

        ESP_LOGI(PN532_TAG, "PN532 initialized");
    }

    return result;
}

uint32_t pn532_get_firmware_version(pn532_t *dev)
{
    static const uint8_t fw_prefix[PN532_FW_RESP_PREFIX_LEN] = {
        0x00U, 0x00U, 0xFFU, 0x06U, 0xFAU, 0xD5U, 0x03U
    };
    uint32_t result = 0U;

    pn532_packetbuffer[0] = PN532_FIRMWAREVERSION;

    bool ack = send_command_check_ack(dev, pn532_packetbuffer, 1U, PN532_CMD_TIMEOUT_MS);
    if (!ack) {
        ESP_LOGE(PN532_TAG, "No ACK from PN532");
    } else {
        read_data(dev, pn532_packetbuffer, (uint8_t)PN532_FW_RESP_READ_LEN);
        ESP_LOG_BUFFER_HEX_LEVEL(PN532_TAG, pn532_packetbuffer,
                                 PN532_FW_RESP_READ_LEN, ESP_LOG_INFO);

        if (memcmp(pn532_packetbuffer, fw_prefix, PN532_FW_RESP_PREFIX_LEN) != 0) {
            ESP_LOGE(PN532_TAG, "Unexpected firmware response");
        } else {
            result  = (uint32_t)pn532_packetbuffer[PN532_FW_IC_OFFSET];
            result  = (result << PN532_CID_SHIFT);
            result |= (uint32_t)pn532_packetbuffer[PN532_FW_VER_OFFSET];
            result  = (result << PN532_CID_SHIFT);
            result |= (uint32_t)pn532_packetbuffer[PN532_FW_REV_OFFSET];
            result  = (result << PN532_CID_SHIFT);
            result |= (uint32_t)pn532_packetbuffer[PN532_FW_SUPPORT_OFFSET];
        }
    }

    return result;
}

bool pn532_sam_config(pn532_t *dev)
{
    bool result = false;

    pn532_packetbuffer[0] = PN532_SAMCONFIGURATION;
    pn532_packetbuffer[1] = 0x01U;
    pn532_packetbuffer[2] = 0x14U;
    pn532_packetbuffer[3] = 0x01U;

    bool ack = send_command_check_ack(dev, pn532_packetbuffer, 4U, PN532_CMD_TIMEOUT_MS);
    if (ack) {
        read_data(dev, pn532_packetbuffer, (uint8_t)PN532_SAM_READ_LEN);
        result = (pn532_packetbuffer[PN532_SAM_STATUS_OFFSET] == PN532_SAM_STATUS_OK);
    }

    return result;
}

uint32_t pn532_read_passive_target_id(pn532_t *dev, uint8_t cardbaudrate)
{
    uint32_t result = 0U;

    pn532_packetbuffer[0] = PN532_INLISTPASSIVETARGET;
    pn532_packetbuffer[1] = 1U;
    pn532_packetbuffer[2] = cardbaudrate;

    bool ack = send_command_check_ack(dev, pn532_packetbuffer, 3U, PN532_CMD_TIMEOUT_MS);
    if (ack) {
        read_data(dev, pn532_packetbuffer, (uint8_t)PN532_CARD_READ_LEN);

        if (pn532_packetbuffer[PN532_CARD_NB_OFFSET] == PN532_CARD_NB_EXPECTED) {
            for (uint8_t i = 0U; i < pn532_packetbuffer[PN532_CARD_LEN_OFFSET]; i++) {
                result  = (result << PN532_CID_SHIFT);
                result |= (uint32_t)pn532_packetbuffer[PN532_CARD_ID_BASE_OFFSET + i];
            }
        }
    }

    return result;
}
