/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file Pn532NfcTransport.h
 * @brief @ref CW_NfcTransport adapter wrapping the ESP-IDF @ref pn532_driver.
 *
 * @ref Pn532NfcTransport bridges the C-level @ref pn532_driver with the C++
 * @ref CW_NfcTransport interface consumed by @ref CW_SecureChannel and
 * @ref CryptnoxWallet.  Callers construct the PN532 handle independently (via
 * @ref pn532_init) and inject the pointer at construction time.
 *
 * @par Typical usage
 * @code
 * pn532_t nfc;
 * pn532_config_t cfg = {};
 * cfg.transport     = PN532_TRANSPORT_SPI;
 * cfg.spi_host      = SPI2_HOST;
 * cfg.pin_cs        = 10;
 * cfg.skip_bus_init = true;
 * ESP_ERROR_CHECK(pn532_init(&nfc, &cfg));
 *
 * ESP32Logger          logger;
 * Pn532NfcTransport    transport(&nfc, logger);
 * ESP32CryptoProvider  crypto;
 * ESP32Platform        platform;
 * CryptnoxWallet       wallet(transport, logger, crypto, platform);
 * @endcode
 *
 * @ingroup esp32_adapters
 *
 * @defgroup esp32_adapters ESP32 concrete adapters
 * @brief Concrete @ref CW_NfcTransport / @ref CW_CryptoProvider / @ref CW_Logger /
 *        @ref CW_Platform implementations for ESP32 / ESP-IDF.
 *
 * Each class in this group implements one of the abstract interfaces declared
 * in @ref adapters using the ESP-IDF peripheral drivers and mbedTLS:
 *
 * | Adapter                  | Interface              | Backing library / hardware             |
 * |--------------------------|------------------------|----------------------------------------|
 * | @ref Pn532NfcTransport   | @ref CW_NfcTransport   | ESP-IDF PN532 C driver (injected handle) |
 * | @ref PN532Adapter        | @ref CW_NfcTransport   | ESP-IDF PN532 C driver (owned handle)  |
 * | @ref ESP32CryptoProvider | @ref CW_CryptoProvider | mbedTLS + ESP32 hardware TRNG          |
 * | @ref ESP32Logger         | @ref CW_Logger         | ESP32 UART0 (development builds)       |
 * | @ref ESP32Platform       | @ref CW_Platform       | FreeRTOS @c vTaskDelay                 |
 */

#pragma once

#include "CW_NfcTransport.h"
#include "CW_Logger.h"

extern "C" {
#include "pn532.h"
}

/**
 * @class Pn532NfcTransport
 * @ingroup esp32_nfc_transport
 * @brief @ref CW_NfcTransport implementation backed by the ESP-IDF PN532 driver.
 *
 * Delegates every NFC operation to the C-level @ref pn532_driver via a
 * @ref pn532_t handle supplied by the caller.  The handle's lifetime must
 * exceed the lifetime of this object.
 *
 * Both @ref sendAPDU (response ≤ 255 bytes, @c uint8_t length) and
 * @ref sendAPDULarge (response ≤ 65535 bytes, @c uint16_t length) are
 * implemented; both ultimately call @ref pn532_send_apdu — the difference
 * is only in the type used to report the response length back to the caller.
 *
 * @note Non-copyable: the class holds a reference (@c CW_Logger &) and a
 *       raw pointer (@c pn532_t *) that cannot be transferred safely.
 *
 * @see pn532_t
 * @see CW_NfcTransport
 * @see CryptnoxWallet
 */
class Pn532NfcTransport : public CW_NfcTransport {
public:
    /**
     * @brief Construct the transport adapter.
     *
     * @param[in] dev    Pointer to an already-initialised @ref pn532_t handle.
     *                   Must remain valid for the lifetime of this object.
     * @param[in] logger Logger used by @ref printFirmwareVersion.
     */
    Pn532NfcTransport(pn532_t *dev, CW_Logger &logger);

    /**
     * @brief Configure the PN532 SAM and prepare it to accept card commands.
     *
     * Calls @ref pn532_sam_config.  Must succeed before @ref inListPassiveTarget
     * or any APDU exchange is attempted.  Called automatically by
     * @c CryptnoxWallet::begin().
     *
     * @return @c true if the PN532 acknowledged SAMConfiguration, @c false otherwise.
     */
    bool begin() override;

    /**
     * @brief Scan for a passive ISO 14443-A card.
     *
     * Calls @ref pn532_read_passive_target_id with the ISO 14443-A baud-rate
     * selector.
     *
     * @return @c true if at least one card was found in the RF field,
     *         @c false if no card is present or a communication error occurred.
     */
    bool inListPassiveTarget() override;

    /**
     * @brief Exchange one ISO-DEP APDU with the selected card (short response).
     *
     * Use this overload when the card's DataOut is guaranteed to fit in a
     * @c uint8_t (≤ 255 bytes).  For larger responses — such as the
     * manufacturer certificate — use @ref sendAPDULarge.
     *
     * @param[in]     apdu        APDU command bytes (must not be @c NULL).
     * @param[in]     apduLen     Length of @p apdu in bytes (≤ @ref PN532_MAX_APDU_LEN).
     * @param[out]    response    Caller-allocated buffer for the DataOut bytes.
     * @param[in,out] responseLen In: capacity of @p response.
     *                            Out: number of DataOut bytes written, capped
     *                            at @c UINT8_MAX on overflow.
     * @return @c true on a successful APDU exchange, @c false otherwise.
     *
     * @see sendAPDULarge
     */
    bool sendAPDU(const uint8_t *apdu, uint8_t apduLen,
                  uint8_t *response, uint8_t &responseLen) override;

    /**
     * @brief Exchange one ISO-DEP APDU with the selected card (large response).
     *
     * Identical to @ref sendAPDU but uses a @c uint16_t response length,
     * allowing DataOut up to 65535 bytes.  Used for commands whose response
     * can exceed 255 bytes (e.g. @c GET_MANUFACTURER_CERTIFICATE whose DataOut
     * may be ~415 bytes and requires an extended PN532 frame).
     *
     * @param[in]     apdu        APDU command bytes (must not be @c NULL).
     * @param[in]     apduLen     Length of @p apdu in bytes (≤ @ref PN532_MAX_APDU_LEN).
     * @param[out]    response    Caller-allocated buffer for the DataOut bytes.
     * @param[in,out] responseLen In: capacity of @p response in bytes.
     *                            Out: number of DataOut bytes actually written.
     * @return @c true on a successful APDU exchange, @c false otherwise.
     *
     * @see sendAPDU
     */
    bool sendAPDULarge(const uint8_t *apdu, uint8_t apduLen,
                       uint8_t *response, uint16_t &responseLen) override;

    /**
     * @brief Release the currently selected NFC target.
     *
     * Calls @ref pn532_release_target so the PN532 drops its logical link to
     * the card and is ready for the next @ref inListPassiveTarget call.
     */
    void resetReader() override;

    /**
     * @brief Query and log the PN532 firmware version.
     *
     * Calls @ref pn532_get_firmware_version and prints a human-readable
     * version string (@c "PN5xx firmware vX.Y") via the injected logger.
     *
     * @return @c true if the PN532 returned a non-zero version, @c false on
     *         communication failure.
     */
    bool printFirmwareVersion() override;

    /** @brief Default destructor. */
    ~Pn532NfcTransport() override {}

private:
    pn532_t   *m_dev;    ///< PN532 device handle (not owned; must outlive this object).
    CW_Logger &m_logger; ///< Logger for @ref printFirmwareVersion output.
};
