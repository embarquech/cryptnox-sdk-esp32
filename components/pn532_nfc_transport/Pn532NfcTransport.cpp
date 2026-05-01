#include "Pn532NfcTransport.h"

static const uint32_t PN532_FW_IC_SHIFT  = 24U;
static const uint32_t PN532_FW_VER_SHIFT = 16U;
static const uint32_t PN532_FW_REV_SHIFT = 8U;
static const uint32_t PN532_BYTE_MASK    = 0xFFU;

Pn532NfcTransport::Pn532NfcTransport(pn532_t *dev, CW_Logger &logger)
    : m_dev(dev), m_logger(logger)
{
}

bool Pn532NfcTransport::begin()
{
    bool result = pn532_sam_config(m_dev);
    return result;
}

bool Pn532NfcTransport::inListPassiveTarget()
{
    uint32_t uid = pn532_read_passive_target_id(m_dev, PN532_MIFARE_ISO14443A);
    bool result = (uid != 0U);
    return result;
}

bool Pn532NfcTransport::sendAPDU(const uint8_t *apdu, uint8_t apduLen,
                                  uint8_t *response, uint8_t &responseLen)
{
    bool result = pn532_send_apdu(m_dev, apdu, apduLen, response, &responseLen);
    return result;
}

void Pn532NfcTransport::resetReader()
{
    (void)pn532_release_target(m_dev);
}

bool Pn532NfcTransport::printFirmwareVersion()
{
    uint32_t version = pn532_get_firmware_version(m_dev);
    bool result = (version != 0U);

    if (result) {
        m_logger.print("PN5");
        m_logger.print(static_cast<uint8_t>((version >> PN532_FW_IC_SHIFT) & PN532_BYTE_MASK), HEX);
        m_logger.print(" firmware v");
        m_logger.print(static_cast<uint8_t>((version >> PN532_FW_VER_SHIFT) & PN532_BYTE_MASK));
        m_logger.print('.');
        m_logger.println(static_cast<uint8_t>((version >> PN532_FW_REV_SHIFT) & PN532_BYTE_MASK));
    }

    return result;
}
