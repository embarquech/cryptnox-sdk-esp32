/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file pn532_adapter.h
 * @brief Self-contained @ref CW_NfcTransport adapter that owns its @ref pn532_t handle.
 *
 * @ref PN532Adapter is an alternative to @ref Pn532NfcTransport for callers
 * that prefer to hand a @ref pn532_config_t to the adapter and let it manage
 * the full driver lifetime internally, rather than constructing the
 * @ref pn532_t externally.
 *
 * @ingroup esp32_adapters
 */

#ifndef PN532_ADAPTER_H
#define PN532_ADAPTER_H

#include "CW_NfcTransport.h"
#include "CW_Logger.h"
#include "pn532.h"

/**
 * @class PN532Adapter
 * @ingroup esp32_adapters
 * @brief Self-contained @ref CW_NfcTransport that owns its @ref pn532_t instance.
 *
 * Unlike @ref Pn532NfcTransport (which takes a pre-initialised @ref pn532_t
 * pointer), @ref PN532Adapter accepts a @ref pn532_config_t at construction
 * time and calls @ref pn532_init internally on the first @ref begin() call.
 * This is convenient when the NFC reader is the only SPI/I²C device and the
 * application does not need to manage the bus lifetime separately.
 *
 * @par Example
 * @code
 * pn532_config_t cfg = {};
 * cfg.transport = PN532_TRANSPORT_SPI;
 * cfg.spi_host  = SPI2_HOST;
 * cfg.pin_cs    = 10;
 *
 * ESP32Logger     logger;
 * PN532Adapter    transport(cfg, logger);
 * CryptnoxWallet  wallet(transport, logger, crypto, platform);
 * @endcode
 *
 * @note Non-copyable: @c _dev holds raw SPI/I²C handles that cannot be
 *       duplicated.
 *
 * @see Pn532NfcTransport
 * @see CW_NfcTransport
 */
class PN532Adapter : public CW_NfcTransport {
public:
    /**
     * @brief Construct the adapter with the given PN532 configuration.
     *
     * Stores the configuration and logger for use during @ref begin().
     * The PN532 driver is not initialised at this point.
     *
     * @param[in] config  Transport and pin configuration forwarded to
     *                    @ref pn532_init on the first @ref begin() call.
     * @param[in] logger  Logger used by @ref printFirmwareVersion.
     */
    PN532Adapter(const pn532_config_t &config, CW_Logger &logger);

    /**
     * @brief Initialise the PN532 driver and configure the SAM.
     *
     * Calls @ref pn532_init (if not already done) followed by
     * @ref pn532_sam_config.  Idempotent — repeated calls after a successful
     * init are no-ops that return @c true.
     *
     * @return @c true on success, @c false if @ref pn532_init or
     *         @ref pn532_sam_config fails.
     */
    bool begin() override;

    /**
     * @brief Scan for a passive ISO 14443-A card.
     *
     * @return @c true if a card was detected in the RF field,
     *         @c false if no card is present or a communication error occurred.
     */
    bool inListPassiveTarget() override;

    /**
     * @brief Exchange one ISO-DEP APDU with the selected card (short response).
     *
     * @param[in]     apdu        APDU command bytes (must not be @c NULL).
     * @param[in]     apduLen     Length of @p apdu (≤ @ref PN532_MAX_APDU_LEN).
     * @param[out]    response    Caller-allocated buffer for the DataOut bytes.
     * @param[in,out] responseLen In: capacity of @p response.
     *                            Out: number of DataOut bytes written, capped
     *                            at @c UINT8_MAX on overflow.
     * @return @c true on success, @c false on transport or card error.
     *
     * @see sendAPDULarge
     */
    bool sendAPDU(const uint8_t *apdu, uint8_t apduLen,
                  uint8_t *response, uint8_t &responseLen) override;

    /**
     * @brief Exchange one ISO-DEP APDU with the selected card (large response).
     *
     * Use when the card's DataOut may exceed 255 bytes (e.g.
     * @c GET_MANUFACTURER_CERTIFICATE).
     *
     * @param[in]     apdu        APDU command bytes (must not be @c NULL).
     * @param[in]     apduLen     Length of @p apdu (≤ @ref PN532_MAX_APDU_LEN).
     * @param[out]    response    Caller-allocated buffer for the DataOut bytes.
     * @param[in,out] responseLen In: capacity of @p response in bytes.
     *                            Out: number of DataOut bytes actually written.
     * @return @c true on success, @c false on transport or card error.
     *
     * @see sendAPDU
     */
    bool sendAPDULarge(const uint8_t *apdu, uint8_t apduLen,
                       uint8_t *response, uint16_t &responseLen) override;

    /**
     * @brief Release the currently selected NFC target.
     *
     * Calls @ref pn532_release_target to drop the logical link so the PN532
     * can list a new target on the next @ref inListPassiveTarget call.
     */
    void resetReader() override;

    /**
     * @brief Query and log the PN532 firmware version.
     *
     * Prints @c "PN5xx firmware vX.Y" via the injected logger.
     *
     * @return @c true if the PN532 returned a valid version, @c false on
     *         communication failure or if the adapter has not been initialised.
     */
    bool printFirmwareVersion() override;

private:
    pn532_config_t  _config;      ///< Stored configuration forwarded to @ref pn532_init.
    pn532_t         _dev;         ///< PN532 device state (owned by this adapter).
    CW_Logger      &_logger;      ///< Logger reference for @ref printFirmwareVersion.
    bool            _initialized; ///< @c true after a successful @ref pn532_init call.
};

#endif /* PN532_ADAPTER_H */
