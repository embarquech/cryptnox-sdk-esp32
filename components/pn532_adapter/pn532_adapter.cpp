#include "pn532_adapter.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "pn532_adapter";

/* Bit positions used to extract firmware version fields from the packed uint32_t. */
static const uint32_t FW_IC_SHIFT = 24U;
static const uint32_t FW_VER_SHIFT = 16U;
static const uint32_t FW_REV_SHIFT = 8U;
static const uint32_t FW_BYTE_MASK = 0xFFU;

PN532Adapter::PN532Adapter(const pn532_config_t &config, CW_Logger &logger)
    : _config(config), _dev{}, _logger(logger), _initialized(false)
{
}

bool PN532Adapter::begin()
{
    esp_err_t ret = ESP_FAIL;
    bool result = false;

    ret = pn532_init(&_dev, &_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "pn532_init failed: %s", esp_err_to_name(ret));
    } else {
        _initialized = true;
        result = true;
    }

    return result;
}

bool PN532Adapter::inListPassiveTarget()
{
    uint32_t uid = 0U;
    bool result = false;

    if (_initialized) {
        uid = pn532_read_passive_target_id(&_dev, PN532_MIFARE_ISO14443A);
        result = (uid != 0U);
    }

    return result;
}

bool PN532Adapter::sendAPDU(const uint8_t *apdu, uint8_t apduLen,
                             uint8_t *response, uint8_t &responseLen)
{
    bool result = false;

    if (_initialized) {
        result = pn532_send_apdu(&_dev, apdu, apduLen, response, &responseLen);
    }

    return result;
}

void PN532Adapter::resetReader()
{
    if (_initialized) {
        (void)pn532_release_target(&_dev);
    }
}

bool PN532Adapter::printFirmwareVersion()
{
    uint32_t version = 0U;
    bool result = false;

    if (_initialized) {
        version = pn532_get_firmware_version(&_dev);
        if (version == 0U) {
            _logger.println("PN532 firmware query failed");
        } else {
            _logger.print("PN5");
            _logger.print(static_cast<uint8_t>((version >> FW_IC_SHIFT) & FW_BYTE_MASK), HEX);
            _logger.print(" Firmware v");
            _logger.print(static_cast<uint8_t>((version >> FW_VER_SHIFT) & FW_BYTE_MASK), DEC);
            _logger.print(".");
            _logger.println(static_cast<uint8_t>((version >> FW_REV_SHIFT) & FW_BYTE_MASK), DEC);
            result = true;
        }
    }

    return result;
}
