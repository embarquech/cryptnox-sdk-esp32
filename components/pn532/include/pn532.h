// PN532 SPI driver for ESP-IDF
// Ported from Adafruit/Seeed PN532 Arduino library (MIT license)

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* cppcheck-suppress misra-c2012-2.5 */
#define PN532_MIFARE_ISO14443A  (0x00U)
#define PN532_MAX_APDU_LEN      (253U)  /* max APDU bytes for pn532_send_apdu */

typedef struct {
    spi_host_device_t spi_host;
    int pin_mosi;
    int pin_miso;
    int pin_sclk;
    int pin_cs;
    bool skip_bus_init;  /* set true if SPI bus already initialized */
} pn532_config_t;

typedef struct {
    spi_device_handle_t spi;
    int                 pin_cs;
} pn532_t;

esp_err_t pn532_init(pn532_t *dev, const pn532_config_t *config);
uint32_t  pn532_get_firmware_version(pn532_t *dev);
bool      pn532_sam_config(pn532_t *dev);
uint32_t  pn532_read_passive_target_id(pn532_t *dev, uint8_t cardbaudrate);
bool      pn532_send_apdu(pn532_t *dev, const uint8_t *apdu, uint8_t apdu_len,
                          uint8_t *response, uint8_t *response_len);
bool      pn532_release_target(pn532_t *dev);

#ifdef __cplusplus
}
#endif
