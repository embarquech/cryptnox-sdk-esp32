#ifndef PN532_ADAPTER_H
#define PN532_ADAPTER_H

#include "CW_NfcTransport.h"
#include "CW_Logger.h"
#include "pn532.h"

/**
 * @class PN532Adapter
 * @brief Concrete CW_NfcTransport implementation for the PN532 over SPI on ESP32-S3.
 *
 * Bridges the C-level pn532 driver with the C++ CW_NfcTransport interface
 * required by CW_SecureChannel and CryptnoxWallet.
 */
class PN532Adapter : public CW_NfcTransport {
public:
    /**
     * @brief Construct the adapter.
     * @param config  SPI pin and host configuration for the PN532.
     * @param logger  Logger implementation for firmware-version printing.
     */
    PN532Adapter(const pn532_config_t &config, CW_Logger &logger);

    bool begin() override;
    bool inListPassiveTarget() override;
    bool sendAPDU(const uint8_t *apdu, uint8_t apduLen,
                  uint8_t *response, uint8_t &responseLen) override;
    void resetReader() override;
    bool printFirmwareVersion() override;

private:
    pn532_config_t  _config;
    pn532_t         _dev;
    CW_Logger      &_logger;
    bool            _initialized;
};

#endif /* PN532_ADAPTER_H */
