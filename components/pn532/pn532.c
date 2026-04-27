// PN532 SPI driver for ESP-IDF
// Ported from Adafruit/Seeed PN532 Arduino library (MIT license)

#include "pn532.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "pn532";

#define PN532_PREAMBLE      0x00
#define PN532_STARTCODE1    0x00
#define PN532_STARTCODE2    0xFF
#define PN532_POSTAMBLE     0x00
#define PN532_HOSTTOPN532   0xD4

#define PN532_FIRMWAREVERSION       0x02
#define PN532_SAMCONFIGURATION      0x14
#define PN532_INLISTPASSIVETARGET   0x4A

#define PN532_SPI_STATREAD   0x02
#define PN532_SPI_DATAWRITE  0x01
#define PN532_SPI_DATAREAD   0x03
#define PN532_SPI_READY      0x01

#define PN532_PACK_BUFF_SIZE 64

static uint8_t pn532_ack[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
static uint8_t pn532_response_fw[] = {0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5, 0x03};
static uint8_t pn532_packetbuffer[PN532_PACK_BUFF_SIZE];

// --- Low-level SPI ---

static void spi_write_byte(pn532_t *dev, uint8_t data)
{
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &data,
    };
    spi_device_transmit(dev->spi, &t);
}

static uint8_t spi_read_byte(pn532_t *dev)
{
    uint8_t rx = 0;
    spi_transaction_t t = {
        .length = 8,
        .rxlength = 8,
        .tx_buffer = NULL,
        .rx_buffer = &rx,
    };
    // Send 0x00 while reading
    uint8_t tx = 0x00;
    t.tx_buffer = &tx;
    spi_device_transmit(dev->spi, &t);
    return rx;
}

static uint8_t read_spi_status(pn532_t *dev)
{
    gpio_set_level(dev->pin_cs, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    spi_write_byte(dev, PN532_SPI_STATREAD);
    uint8_t status = spi_read_byte(dev);
    gpio_set_level(dev->pin_cs, 1);
    return status;
}

static void read_data(pn532_t *dev, uint8_t *buff, uint8_t n)
{
    gpio_set_level(dev->pin_cs, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    spi_write_byte(dev, PN532_SPI_DATAREAD);
    for (uint8_t i = 0; i < n; i++) {
        vTaskDelay(pdMS_TO_TICKS(1));
        buff[i] = spi_read_byte(dev);
    }
    gpio_set_level(dev->pin_cs, 1);
}

static bool check_spi_ack(pn532_t *dev)
{
    uint8_t ackbuff[6];
    read_data(dev, ackbuff, 6);
    return (memcmp(ackbuff, pn532_ack, 6) == 0);
}

static void write_command(pn532_t *dev, uint8_t *cmd, uint8_t cmd_len)
{
    uint8_t checksum;
    cmd_len++;

    gpio_set_level(dev->pin_cs, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    spi_write_byte(dev, PN532_SPI_DATAWRITE);

    checksum = PN532_PREAMBLE + PN532_PREAMBLE + PN532_STARTCODE2;
    spi_write_byte(dev, PN532_PREAMBLE);
    spi_write_byte(dev, PN532_PREAMBLE);
    spi_write_byte(dev, PN532_STARTCODE2);

    spi_write_byte(dev, cmd_len);
    spi_write_byte(dev, ~cmd_len + 1);

    spi_write_byte(dev, PN532_HOSTTOPN532);
    checksum += PN532_HOSTTOPN532;

    for (uint8_t i = 0; i < cmd_len - 1; i++) {
        spi_write_byte(dev, cmd[i]);
        checksum += cmd[i];
    }
    spi_write_byte(dev, ~checksum);
    spi_write_byte(dev, PN532_POSTAMBLE);
    gpio_set_level(dev->pin_cs, 1);
}

static bool send_command_check_ack(pn532_t *dev, uint8_t *cmd, uint8_t cmd_len, uint16_t timeout)
{
    uint16_t timer = 0;

    write_command(dev, cmd, cmd_len);

    while (read_spi_status(dev) != PN532_SPI_READY) {
        if (timeout != 0) {
            timer += 10;
            if (timer > timeout) return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!check_spi_ack(dev)) return false;

    timer = 0;
    while (read_spi_status(dev) != PN532_SPI_READY) {
        if (timeout != 0) {
            timer += 10;
            if (timer > timeout) return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return true;
}

// --- Public API ---

esp_err_t pn532_init(pn532_t *dev, const pn532_config_t *config)
{
    // Configure CS pin as GPIO output (managed manually)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << config->pin_cs),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(config->pin_cs, 1);
    dev->pin_cs = config->pin_cs;

    // Configure SPI bus (skip if already initialized externally)
    if (!config->skip_bus_init) {
        spi_bus_config_t buscfg = {
            .mosi_io_num = config->pin_mosi,
            .miso_io_num = config->pin_miso,
            .sclk_io_num = config->pin_sclk,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 0,
        };
        esp_err_t ret = spi_bus_initialize(config->spi_host, &buscfg, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK) return ret;
    }

    // Add PN532 device: SPI mode 0, LSB first, 1 MHz
    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = 1000000,
        .spics_io_num = -1,  // CS managed manually
        .queue_size = 1,
        .flags = SPI_DEVICE_BIT_LSBFIRST,
    };
    esp_err_t ret = spi_bus_add_device(config->spi_host, &devcfg, &dev->spi);
    if (ret != ESP_OK) return ret;

    // Wake up PN532: toggle CS and send wakeup bytes
    gpio_set_level(dev->pin_cs, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    spi_write_byte(dev, 0x55);  // PN532 wakeup
    spi_write_byte(dev, 0x55);
    spi_write_byte(dev, 0x00);
    spi_write_byte(dev, 0x00);
    spi_write_byte(dev, 0x00);
    gpio_set_level(dev->pin_cs, 1);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Dummy command to sync (ignore response)
    pn532_packetbuffer[0] = PN532_FIRMWAREVERSION;
    send_command_check_ack(dev, pn532_packetbuffer, 1, 1000);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "PN532 initialized");
    return ESP_OK;
}

uint32_t pn532_get_firmware_version(pn532_t *dev)
{
    pn532_packetbuffer[0] = PN532_FIRMWAREVERSION;

    if (!send_command_check_ack(dev, pn532_packetbuffer, 1, 1000)) {
        ESP_LOGE(TAG, "No ACK from PN532");
        return 0;
    }

    read_data(dev, pn532_packetbuffer, 12);

    ESP_LOG_BUFFER_HEX_LEVEL(TAG, pn532_packetbuffer, 12, ESP_LOG_INFO);

    if (memcmp(pn532_packetbuffer, pn532_response_fw, 7) != 0) {
        ESP_LOGE(TAG, "Unexpected firmware response");
        return 0;
    }

    uint32_t response = pn532_packetbuffer[7];
    response <<= 8;
    response |= pn532_packetbuffer[8];
    response <<= 8;
    response |= pn532_packetbuffer[9];
    response <<= 8;
    response |= pn532_packetbuffer[10];

    return response;
}

bool pn532_sam_config(pn532_t *dev)
{
    pn532_packetbuffer[0] = PN532_SAMCONFIGURATION;
    pn532_packetbuffer[1] = 0x01; // normal mode
    pn532_packetbuffer[2] = 0x14; // timeout 50ms * 20 = 1s
    pn532_packetbuffer[3] = 0x01; // use IRQ pin

    if (!send_command_check_ack(dev, pn532_packetbuffer, 4, 1000))
        return false;

    read_data(dev, pn532_packetbuffer, 9);
    return (pn532_packetbuffer[6] == 0x15);
}

uint32_t pn532_read_passive_target_id(pn532_t *dev, uint8_t cardbaudrate)
{
    pn532_packetbuffer[0] = PN532_INLISTPASSIVETARGET;
    pn532_packetbuffer[1] = 1;  // max 1 card
    pn532_packetbuffer[2] = cardbaudrate;

    if (!send_command_check_ack(dev, pn532_packetbuffer, 3, 1000))
        return 0;

    read_data(dev, pn532_packetbuffer, 21);

    if (pn532_packetbuffer[8] != 1)
        return 0;

    uint32_t cid = 0;
    for (uint8_t i = 0; i < pn532_packetbuffer[13]; i++) {
        cid <<= 8;
        cid |= pn532_packetbuffer[14 + i];
    }

    return cid;
}
