#pragma once

#include "CW_NfcTransport.h"
#include "CW_Logger.h"

extern "C" {
#include "pn532.h"
}

class Pn532NfcTransport : public CW_NfcTransport {
public:
    Pn532NfcTransport(pn532_t *dev, CW_Logger &logger);

    bool begin() override;

    bool inListPassiveTarget() override;

    bool sendAPDU(const uint8_t *apdu, uint8_t apduLen,
                  uint8_t *response, uint8_t &responseLen) override;

    void resetReader() override;

    bool printFirmwareVersion() override;

    ~Pn532NfcTransport() override {}

private:
    pn532_t *m_dev;
    CW_Logger &m_logger;
};
