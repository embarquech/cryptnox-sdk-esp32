// PN532 SPI driver for ESP-IDF
// Ported from Adafruit/Seeed PN532 Arduino library (MIT license)

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"

#define PN532_MIFARE_ISO14443A  (0x00U)

typedef struct {
    spi_host_device_t spi_host;
    int               pin_mosi;
    int               pin_miso;
    int               pin_sclk;
    int               pin_cs;
    bool              skip_bus_init;  /* set true if SPI bus already initialized */
} pn532_config_t;

typedef struct {
    spi_device_handle_t spi;
    int                 pin_cs;
} pn532_t;

esp_err_t pn532_init(pn532_t *dev, const pn532_config_t *config);
uint32_t  pn532_get_firmware_version(pn532_t *dev);
bool      pn532_sam_config(pn532_t *dev);
uint32_t  pn532_read_passive_target_id(pn532_t *dev, uint8_t cardbaudrate);

/**
 * Send an APDU via the PN532 InDataExchange command and receive the response.
 *
 * @param dev       Initialised PN532 device handle.
 * @param apdu      APDU command bytes.
 * @param apdu_len  Number of bytes in apdu.
 * @param response  Buffer to receive the card's response APDU.
 * @param resp_len  In: capacity of response buffer. Out: bytes written.
 * @return true on success, false on timeout or PN532 error.
 */
bool pn532_send_apdu(pn532_t *dev,
                     const uint8_t *apdu, uint8_t apdu_len,
                     uint8_t *response, uint8_t *resp_len);

/**
 * Send InRelease to free the currently listed target.
 *
 * @param dev  Initialised PN532 device handle.
 * @return true if the target was released successfully, false otherwise.
 */
bool pn532_release_target(pn532_t *dev);
