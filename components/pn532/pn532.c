// PN532 SPI driver for ESP-IDF
// Ported from Adafruit/Seeed PN532 Arduino library (MIT license)

#include "pn532.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *const PN532_LOG_TAG = "pn532";

/******************************************************************
 * PN532 frame constants
 ******************************************************************/

#define PN532_PREAMBLE     (0x00U)
/* cppcheck-suppress misra-c2012-2.5 */
#define PN532_STARTCODE1   (0x00U)
#define PN532_STARTCODE2   (0xFFU)
#define PN532_POSTAMBLE    (0x00U)
#define PN532_HOSTTOPN532  (0xD4U)

/******************************************************************
 * PN532 command codes
 ******************************************************************/

#define PN532_FIRMWAREVERSION      (0x02U)
#define PN532_SAMCONFIGURATION     (0x14U)
#define PN532_INLISTPASSIVETARGET  (0x4AU)
#define PN532_INDATAEXCHANGE       (0x40U)
#define PN532_INRELEASE            (0x52U)

/******************************************************************
 * SPI protocol constants
 ******************************************************************/

#define PN532_SPI_STATREAD    (0x02U)
#define PN532_SPI_DATAWRITE   (0x01U)
#define PN532_SPI_DATAREAD    (0x03U)
#define PN532_SPI_READY       (0x01U)
#define PN532_SPI_CLOCK_HZ    (1000000U)
#define PN532_SPI_MODE        (0U)
#define PN532_SPI_NO_PIN      (-1)
#define PN532_SPI_QUEUE_SIZE  (1U)
#define PN532_SPI_BITS        (8U)

/******************************************************************
 * Timing constants (milliseconds)
 ******************************************************************/

#define PN532_CS_TOGGLE_DELAY_MS  (2U)
#define PN532_WAKEUP_DELAY_MS     (1000U)
#define PN532_SYNC_DELAY_MS       (100U)
#define PN532_POLL_INTERVAL_MS    (10U)
#define PN532_CMD_TIMEOUT_MS      (1000U)
/* Card ECDSA in getCardCertificate takes up to 3 s; use 5 s margin. */
#define PN532_APDU_TIMEOUT_MS     (5000U)
#define PN532_BYTE_DELAY_MS       (1U)

/******************************************************************
 * GPIO level constants
 ******************************************************************/

#define GPIO_LEVEL_LOW        (0U)    /* drive pin low  */
#define GPIO_LEVEL_HIGH       (1U)    /* drive pin high */
#define GPIO_PIN_BITMASK_BASE (1ULL)  /* base bit for gpio_config_t.pin_bit_mask */

/******************************************************************
 * Wakeup sequence constants
 ******************************************************************/

#define PN532_WAKEUP_BYTE  (0x55U)

/******************************************************************
 * ACK frame
 ******************************************************************/

#define PN532_ACK_LEN  (6U)

/******************************************************************
 * Frame structure constants
 ******************************************************************/

/* HOSTTOPN532 TFI byte added to cmd_len when building a frame. */
#define PN532_FRAME_TFI_OVERHEAD  (1U)

/******************************************************************
 * Firmware version response offsets and lengths
 ******************************************************************/

#define PN532_FIRMWARE_CMD_LEN     (1U)
#define PN532_FIRMWARE_RESP_LEN    (12U)
#define PN532_FIRMWARE_HDR_LEN     (7U)
#define PN532_FW_IC_OFFSET         (7U)
#define PN532_FW_VER_OFFSET        (8U)
#define PN532_FW_REV_OFFSET        (9U)
#define PN532_FW_SUPPORT_OFFSET    (10U)

/******************************************************************
 * SAMConfiguration constants
 ******************************************************************/

#define PN532_SAM_CMD_LEN           (4U)
#define PN532_SAM_RESP_LEN          (9U)
#define PN532_SAM_RESP_CODE_OFFSET  (6U)
#define PN532_SAM_RESP_CODE         (0x15U)  /* SAMCONFIGURATION + 1 */
#define PN532_SAM_NORMAL_MODE       (0x01U)
#define PN532_SAM_TIMEOUT           (0x14U)  /* 50 ms x 20 = 1 s */
#define PN532_SAM_USE_IRQ           (0x01U)

/******************************************************************
 * InListPassiveTarget constants
 ******************************************************************/

#define PN532_PASSIVE_CMD_LEN             (3U)
/* ISO 14443-4 responses include ATS after the UID; 64 bytes covers
 * the full frame (header 5 + D5+4B 2 + NbTg+Tg+ATQA+SAK 5 + UID 7
 * + AtsLength+Ats up to ~20 + DCS+postamble 2 = ~41 bytes max). */
#define PN532_PASSIVE_RESP_LEN            (64U)
#define PN532_PASSIVE_MAX_TARGETS         (1U)
#define PN532_PASSIVE_NUM_TARGETS_OFFSET  (7U)
#define PN532_PASSIVE_EXPECTED_TARGETS    (1U)
#define PN532_PASSIVE_UID_LEN_OFFSET      (12U)
#define PN532_PASSIVE_UID_DATA_OFFSET     (13U)
#define PN532_BYTE_SHIFT_BITS             (8U)  /* bits to shift when packing/unpacking bytes */

/******************************************************************
 * InDataExchange constants
 ******************************************************************/

#define PN532_EXCHANGE_CMD_OVERHEAD   (2U)    /* cmd byte + Tg byte */
#define PN532_EXCHANGE_TG             (0x01U)
#define PN532_EXCHANGE_FRAME_MAX      (200U)  /* covers up to 189-byte card responses */
#define PN532_EXCHANGE_LEN_OFFSET     (3U)
#define PN532_EXCHANGE_STATUS_OFFSET  (7U)
#define PN532_EXCHANGE_DATA_OFFSET    (8U)    /* frame[8] = first DataOut byte (after D5+41+ERR) */
#define PN532_EXCHANGE_LEN_BIAS       (3U)    /* LEN covers D5 + CMD + ERR; DataOut = LEN - 3 */
#define PN532_EXCHANGE_STATUS_OK      (0x00U)

/******************************************************************
 * InRelease constants
 ******************************************************************/

#define PN532_INRELEASE_CMD_LEN   (2U)
#define PN532_INRELEASE_RESP_LEN  (10U)

/******************************************************************
 * Module-level static data
 ******************************************************************/

/* cppcheck-suppress misra-c2012-8.9 */
static const uint8_t pn532_ack[PN532_ACK_LEN] = {
    0x00U, 0x00U, 0xFFU, 0x00U, 0xFFU, 0x00U
};

/* cppcheck-suppress misra-c2012-8.9 */
static const uint8_t pn532_response_fw[PN532_FIRMWARE_HDR_LEN] = {
    0x00U, 0x00U, 0xFFU, 0x06U, 0xFAU, 0xD5U, 0x03U
};

/******************************************************************
 * Forward declarations
 ******************************************************************/

static bool pn532_buffer_equal(const uint8_t *lhs, const uint8_t *rhs, uint8_t len);

/******************************************************************
 * Low-level SPI helpers
 ******************************************************************/

static void spi_write_byte(pn532_t *dev, uint8_t data)
{
    spi_transaction_t t;
    (void)memset(&t, 0, sizeof(t));
    t.length = PN532_SPI_BITS;
    t.tx_buffer = &data;
    (void)spi_device_transmit(dev->spi, &t);
}

static uint8_t spi_read_byte(pn532_t *dev)
{
    uint8_t rx = 0U;
    uint8_t tx = 0x00U;
    spi_transaction_t t;
    (void)memset(&t, 0, sizeof(t));
    t.length = PN532_SPI_BITS;
    t.rxlength = PN532_SPI_BITS;
    t.tx_buffer = &tx;
    t.rx_buffer = &rx;
    (void)spi_device_transmit(dev->spi, &t);
    return rx;
}

static uint8_t read_spi_status(pn532_t *dev)
{
    uint8_t status = 0U;
    (void)gpio_set_level(dev->pin_cs, GPIO_LEVEL_LOW);
    vTaskDelay(pdMS_TO_TICKS(PN532_CS_TOGGLE_DELAY_MS));
    spi_write_byte(dev, PN532_SPI_STATREAD);
    status = spi_read_byte(dev);
    (void)gpio_set_level(dev->pin_cs, GPIO_LEVEL_HIGH);
    return status;
}

static void read_data(pn532_t *dev, uint8_t *buff, uint8_t n)
{
    uint8_t i = 0U;
    (void)gpio_set_level(dev->pin_cs, GPIO_LEVEL_LOW);
    vTaskDelay(pdMS_TO_TICKS(PN532_CS_TOGGLE_DELAY_MS));
    spi_write_byte(dev, PN532_SPI_DATAREAD);
    for (i = 0U; i < n; i++) {
        vTaskDelay(pdMS_TO_TICKS(PN532_BYTE_DELAY_MS));
        buff[i] = spi_read_byte(dev);
    }
    (void)gpio_set_level(dev->pin_cs, GPIO_LEVEL_HIGH);
}

static bool check_spi_ack(pn532_t *dev)
{
    uint8_t ackbuff[PN532_ACK_LEN];
    bool result = false;
    (void)memset(ackbuff, 0, sizeof(ackbuff));
    read_data(dev, ackbuff, PN532_ACK_LEN);
    result = pn532_buffer_equal(ackbuff, pn532_ack, PN532_ACK_LEN);
    return result;
}

static bool pn532_buffer_equal(const uint8_t *lhs, const uint8_t *rhs, uint8_t len)
{
    bool equal = true;
    uint8_t i = 0U;

    for (i = 0U; i < len; i++) {
        if (lhs[i] != rhs[i]) {
            equal = false;
        }
    }

    return equal;
}

static void write_command(pn532_t *dev, const uint8_t *cmd, uint8_t cmd_len)
{
    uint8_t frame_len = (uint8_t)(cmd_len + PN532_FRAME_TFI_OVERHEAD);
    uint8_t checksum = (uint8_t)(PN532_PREAMBLE + PN532_PREAMBLE + PN532_STARTCODE2);
    uint8_t i = 0U;

    (void)gpio_set_level(dev->pin_cs, GPIO_LEVEL_LOW);
    vTaskDelay(pdMS_TO_TICKS(PN532_CS_TOGGLE_DELAY_MS));
    spi_write_byte(dev, PN532_SPI_DATAWRITE);

    spi_write_byte(dev, PN532_PREAMBLE);
    spi_write_byte(dev, PN532_PREAMBLE);
    spi_write_byte(dev, PN532_STARTCODE2);

    spi_write_byte(dev, frame_len);
    spi_write_byte(dev, (uint8_t)((uint8_t)(~frame_len) + 1U));

    spi_write_byte(dev, PN532_HOSTTOPN532);
    checksum = (uint8_t)(checksum + PN532_HOSTTOPN532);

    for (i = 0U; i < cmd_len; i++) {
        spi_write_byte(dev, cmd[i]);
        checksum = (uint8_t)(checksum + cmd[i]);
    }
    spi_write_byte(dev, (uint8_t)(~checksum));
    spi_write_byte(dev, PN532_POSTAMBLE);
    (void)gpio_set_level(dev->pin_cs, GPIO_LEVEL_HIGH);
}

static bool send_command_check_ack(pn532_t *dev, const uint8_t *cmd,
                                   uint8_t cmd_len, uint16_t timeout)
{
    uint16_t timer = 0U;
    bool timed_out = false;
    bool result = false;

    write_command(dev, cmd, cmd_len);

    /* Wait until the PN532 signals it is ready to send the ACK frame. */
    while ((!timed_out) && (read_spi_status(dev) != PN532_SPI_READY)) {
        if (timeout != 0U) {
            timer = (uint16_t)(timer + PN532_POLL_INTERVAL_MS);
            if (timer > timeout) {
                timed_out = true;
            }
        }
        if (!timed_out) {
            vTaskDelay(pdMS_TO_TICKS(PN532_POLL_INTERVAL_MS));
        }
    }

    if (!timed_out && check_spi_ack(dev)) {
        timer = 0U;
        timed_out = false;

        /* Wait until the PN532 signals it is ready to send the response. */
        while ((!timed_out) && (read_spi_status(dev) != PN532_SPI_READY)) {
            if (timeout != 0U) {
                timer = (uint16_t)(timer + PN532_POLL_INTERVAL_MS);
                if (timer > timeout) {
                    timed_out = true;
                }
            }
            if (!timed_out) {
                vTaskDelay(pdMS_TO_TICKS(PN532_POLL_INTERVAL_MS));
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
    gpio_config_t io_conf;
    spi_bus_config_t buscfg;
    spi_device_interface_config_t devcfg;
    esp_err_t ret = ESP_FAIL;

    (void)memset(&io_conf, 0, sizeof(io_conf));
    io_conf.pin_bit_mask = (GPIO_PIN_BITMASK_BASE << (uint32_t)config->pin_cs);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    (void)gpio_config(&io_conf);
    (void)gpio_set_level(config->pin_cs, GPIO_LEVEL_HIGH);
    dev->pin_cs = config->pin_cs;

    if (config->skip_bus_init) {
        ret = ESP_OK;
    } else {
        (void)memset(&buscfg, 0, sizeof(buscfg));
        buscfg.mosi_io_num = config->pin_mosi;
        buscfg.miso_io_num = config->pin_miso;
        buscfg.sclk_io_num = config->pin_sclk;
        buscfg.quadwp_io_num = PN532_SPI_NO_PIN;
        buscfg.quadhd_io_num = PN532_SPI_NO_PIN;
        buscfg.max_transfer_sz = 0;
        ret = spi_bus_initialize(config->spi_host, &buscfg, SPI_DMA_CH_AUTO);
    }

    if (ret == ESP_OK) {
        (void)memset(&devcfg, 0, sizeof(devcfg));
        devcfg.mode = PN532_SPI_MODE;
        devcfg.clock_speed_hz = (int)PN532_SPI_CLOCK_HZ;
        devcfg.spics_io_num = PN532_SPI_NO_PIN;
        devcfg.queue_size = PN532_SPI_QUEUE_SIZE;
        devcfg.flags = SPI_DEVICE_BIT_LSBFIRST;
        ret = spi_bus_add_device(config->spi_host, &devcfg, &dev->spi);

        if ((ret != ESP_OK) && (!config->skip_bus_init)) {
            (void)spi_bus_free(config->spi_host);
        }
    }

    if (ret == ESP_OK) {
        uint8_t init_cmd[PN532_FIRMWARE_CMD_LEN];
        (void)memset(init_cmd, 0, sizeof(init_cmd));

        (void)gpio_set_level(dev->pin_cs, GPIO_LEVEL_LOW);
        vTaskDelay(pdMS_TO_TICKS(PN532_CS_TOGGLE_DELAY_MS));
        spi_write_byte(dev, PN532_WAKEUP_BYTE);
        spi_write_byte(dev, PN532_WAKEUP_BYTE);
        spi_write_byte(dev, PN532_PREAMBLE);
        spi_write_byte(dev, PN532_PREAMBLE);
        spi_write_byte(dev, PN532_PREAMBLE);
        (void)gpio_set_level(dev->pin_cs, GPIO_LEVEL_HIGH);
        vTaskDelay(pdMS_TO_TICKS(PN532_WAKEUP_DELAY_MS));

        init_cmd[0] = PN532_FIRMWAREVERSION;
        (void)send_command_check_ack(dev, init_cmd,
                                     PN532_FIRMWARE_CMD_LEN, PN532_CMD_TIMEOUT_MS);
        vTaskDelay(pdMS_TO_TICKS(PN532_SYNC_DELAY_MS));

        ESP_LOGI(PN532_LOG_TAG, "PN532 initialized");
    }

    return ret;
}

uint32_t pn532_get_firmware_version(pn532_t *dev)
{
    uint8_t pn532_packetbuffer[PN532_FIRMWARE_RESP_LEN];
    uint32_t response = 0U;
    bool ack_received = false;

    (void)memset(pn532_packetbuffer, 0, sizeof(pn532_packetbuffer));
    pn532_packetbuffer[0] = PN532_FIRMWAREVERSION;
    ack_received = send_command_check_ack(dev, pn532_packetbuffer,
                                          PN532_FIRMWARE_CMD_LEN, PN532_CMD_TIMEOUT_MS);

    if (!ack_received) {
        ESP_LOGE(PN532_LOG_TAG, "No ACK from PN532");
    } else {
        read_data(dev, pn532_packetbuffer, PN532_FIRMWARE_RESP_LEN);
        ESP_LOG_BUFFER_HEX_LEVEL(PN532_LOG_TAG, pn532_packetbuffer,
                                 PN532_FIRMWARE_RESP_LEN, ESP_LOG_INFO);

        if (!pn532_buffer_equal(pn532_packetbuffer, pn532_response_fw, PN532_FIRMWARE_HDR_LEN)) {
            ESP_LOGE(PN532_LOG_TAG, "Unexpected firmware response");
        } else {
            response = (uint32_t)pn532_packetbuffer[PN532_FW_IC_OFFSET];
            response <<= PN532_BYTE_SHIFT_BITS;
            response |= (uint32_t)pn532_packetbuffer[PN532_FW_VER_OFFSET];
            response <<= PN532_BYTE_SHIFT_BITS;
            response |= (uint32_t)pn532_packetbuffer[PN532_FW_REV_OFFSET];
            response <<= PN532_BYTE_SHIFT_BITS;
            response |= (uint32_t)pn532_packetbuffer[PN532_FW_SUPPORT_OFFSET];
        }
    }

    return response;
}

bool pn532_sam_config(pn532_t *dev)
{
    uint8_t pn532_packetbuffer[PN532_SAM_RESP_LEN];
    bool ack_received = false;
    bool result = false;

    (void)memset(pn532_packetbuffer, 0, sizeof(pn532_packetbuffer));
    pn532_packetbuffer[0] = PN532_SAMCONFIGURATION;
    pn532_packetbuffer[1] = PN532_SAM_NORMAL_MODE;
    pn532_packetbuffer[2] = PN532_SAM_TIMEOUT;
    pn532_packetbuffer[3] = PN532_SAM_USE_IRQ;

    ack_received = send_command_check_ack(dev, pn532_packetbuffer,
                                          PN532_SAM_CMD_LEN, PN532_CMD_TIMEOUT_MS);

    if (ack_received) {
        read_data(dev, pn532_packetbuffer, PN532_SAM_RESP_LEN);
        result = (pn532_packetbuffer[PN532_SAM_RESP_CODE_OFFSET] == PN532_SAM_RESP_CODE);
    }

    return result;
}

uint32_t pn532_read_passive_target_id(pn532_t *dev, uint8_t cardbaudrate)
{
    uint8_t pn532_packetbuffer[PN532_PASSIVE_RESP_LEN];
    uint32_t cid = 0U;
    bool ack_received = false;

    (void)memset(pn532_packetbuffer, 0, sizeof(pn532_packetbuffer));
    pn532_packetbuffer[0] = PN532_INLISTPASSIVETARGET;
    pn532_packetbuffer[1] = PN532_PASSIVE_MAX_TARGETS;
    pn532_packetbuffer[2] = cardbaudrate;

    ack_received = send_command_check_ack(dev, pn532_packetbuffer,
                                          PN532_PASSIVE_CMD_LEN, PN532_CMD_TIMEOUT_MS);

    if (ack_received) {
        read_data(dev, pn532_packetbuffer, PN532_PASSIVE_RESP_LEN);

        if (pn532_packetbuffer[PN532_PASSIVE_NUM_TARGETS_OFFSET] == PN532_PASSIVE_EXPECTED_TARGETS) {
            uint8_t uid_len = pn532_packetbuffer[PN532_PASSIVE_UID_LEN_OFFSET];
            uint8_t i = 0U;
            for (i = 0U; i < uid_len; i++) {
                cid <<= PN532_BYTE_SHIFT_BITS;
                cid |= (uint32_t)pn532_packetbuffer[PN532_PASSIVE_UID_DATA_OFFSET + i];
            }
        }
    }

    return cid;
}

bool pn532_send_apdu(pn532_t *dev, const uint8_t *apdu, uint8_t apdu_len,
                     uint8_t *response, uint8_t *response_len)
{
    uint8_t cmd[PN532_MAX_APDU_LEN + PN532_EXCHANGE_CMD_OVERHEAD];
    uint8_t frame[PN532_EXCHANGE_FRAME_MAX];
    bool result = false;

    (void)memset(cmd, 0, sizeof(cmd));
    (void)memset(frame, 0, sizeof(frame));

    if (apdu_len <= PN532_MAX_APDU_LEN) {
        uint8_t cmd_total_len = 0U;
        bool ack_received = false;

        cmd[0] = PN532_INDATAEXCHANGE;
        cmd[1] = PN532_EXCHANGE_TG;
        (void)memcpy(&cmd[PN532_EXCHANGE_CMD_OVERHEAD], apdu, apdu_len);
        cmd_total_len = (uint8_t)(apdu_len + PN532_EXCHANGE_CMD_OVERHEAD);

        ack_received = send_command_check_ack(dev, cmd, cmd_total_len, PN532_APDU_TIMEOUT_MS);

        if (ack_received) {
            read_data(dev, frame, PN532_EXCHANGE_FRAME_MAX);

            if ((frame[PN532_EXCHANGE_STATUS_OFFSET] == PN532_EXCHANGE_STATUS_OK) &&
                (frame[PN532_EXCHANGE_LEN_OFFSET] >= PN532_EXCHANGE_LEN_BIAS)) {
                uint8_t data_len = (uint8_t)(frame[PN532_EXCHANGE_LEN_OFFSET] - PN532_EXCHANGE_LEN_BIAS);
                (void)memcpy(response, &frame[PN532_EXCHANGE_DATA_OFFSET], data_len);
                *response_len = data_len;
                result = true;
            }
        }
    }

    return result;
}

bool pn532_release_target(pn532_t *dev)
{
    uint8_t cmd[PN532_INRELEASE_CMD_LEN];
    uint8_t resp[PN532_INRELEASE_RESP_LEN];
    bool ack_received = false;
    bool result = false;

    (void)memset(cmd, 0, sizeof(cmd));
    (void)memset(resp, 0, sizeof(resp));

    cmd[0] = PN532_INRELEASE;
    cmd[1] = PN532_EXCHANGE_TG;

    ack_received = send_command_check_ack(dev, cmd, PN532_INRELEASE_CMD_LEN, PN532_CMD_TIMEOUT_MS);

    if (ack_received) {
        read_data(dev, resp, PN532_INRELEASE_RESP_LEN);
        result = (resp[PN532_EXCHANGE_STATUS_OFFSET] == PN532_EXCHANGE_STATUS_OK);
    }

    return result;
}
