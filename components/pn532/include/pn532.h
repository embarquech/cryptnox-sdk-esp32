// PN532 driver for ESP-IDF — supports SPI and I2C transports
// Ported from Adafruit/Seeed PN532 Arduino library (MIT license)

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* cppcheck-suppress misra-c2012-2.5 */
#define PN532_MIFARE_ISO14443A  (0x00U)
#define PN532_MAX_APDU_LEN      (253U)  /* max APDU bytes for pn532_send_apdu */

/* PN532 7-bit I2C address */
#define PN532_I2C_ADDRESS       (0x24U)

typedef enum {
    PN532_TRANSPORT_SPI,
    PN532_TRANSPORT_I2C,
} pn532_transport_t;

typedef struct {
    pn532_transport_t transport;

    /* SPI fields (used when transport == PN532_TRANSPORT_SPI) */
    spi_host_device_t spi_host;
    int  pin_mosi;
    int  pin_miso;
    int  pin_sclk;
    int  pin_cs;
    bool skip_bus_init;       /* skip SPI bus init (already initialised) */

    /* I2C fields (used when transport == PN532_TRANSPORT_I2C) */
    int  i2c_port;            /* I2C_NUM_0 or I2C_NUM_1 */
    int  pin_sda;
    int  pin_scl;
    int  pin_irq;             /* -1 = unused */
    int  pin_rst;             /* -1 = unused */
    uint32_t i2c_clock_hz;    /* 100000 (standard) or 400000 (fast) */
} pn532_config_t;

typedef struct {
    pn532_transport_t       transport;

    /* SPI state */
    spi_device_handle_t     spi;
    int                     pin_cs;

    /* I2C state */
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t i2c_dev;
    int                     pin_irq;
    int                     pin_rst;
} pn532_t;

esp_err_t pn532_init(pn532_t *dev, const pn532_config_t *config);
uint32_t  pn532_get_firmware_version(pn532_t *dev);
bool      pn532_sam_config(pn532_t *dev);
uint32_t  pn532_read_passive_target_id(pn532_t *dev, uint8_t cardbaudrate);
bool      pn532_send_apdu(pn532_t *dev, const uint8_t *apdu, uint8_t apdu_len,
                          uint8_t *response, uint16_t *response_len);
bool      pn532_release_target(pn532_t *dev);

#ifdef __cplusplus
}
#endif
