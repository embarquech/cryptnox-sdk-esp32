/*
 * SPDX-License-Identifier: LGPL-3.0-or-later AND BSD-3-Clause
 *
 * Public PN532 driver header. Portions derive from Adafruit_PN532
 * (Copyright (c) 2012, Adafruit Industries; BSD-3-Clause). See
 * NOTICES.md at the repo root for the full notice.
 */

/**
 * @file pn532.h
 * @brief Low-level PN532 NFC controller driver for ESP-IDF (SPI and I²C).
 *
 * Provides a transport-agnostic C API for the NXP PN532 NFC controller.
 * Both the ESP-IDF SPI master peripheral and the IDF v5.x I²C master API
 * are supported; the active transport is selected at @ref pn532_init time
 * via @ref pn532_config_t::transport.
 *
 * Typical usage (SPI):
 * @code
 * pn532_config_t cfg = {};
 * cfg.transport     = PN532_TRANSPORT_SPI;
 * cfg.spi_host      = SPI2_HOST;
 * cfg.pin_cs        = 10;
 * cfg.skip_bus_init = true;   // caller already called spi_bus_initialize()
 *
 * pn532_t nfc;
 * ESP_ERROR_CHECK(pn532_init(&nfc, &cfg));
 * @endcode
 *
 * @defgroup pn532_driver PN532 NFC driver
 * @brief Low-level PN532 driver (SPI + I²C, ESP-IDF).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "driver/i2c_master.h"   /* new IDF v5.x master API */

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup pn532_driver
 * @{
 */

/* ── Card / protocol constants ──────────────────────────────────── */

/** @brief Baud-rate selector for ISO 14443-A (Mifare) cards passed to
 *         @ref pn532_read_passive_target_id. */
/* cppcheck-suppress misra-c2012-2.5 */
#define PN532_MIFARE_ISO14443A  (0x00U)

/** @brief Maximum APDU payload length accepted by @ref pn532_send_apdu.
 *
 *  Constrained so that the PN532 InDataExchange frame LEN field (8-bit) never
 *  overflows: @c cmd_total_len = apdu_len + 2 ≤ 254, @c frame_len = 255. */
#define PN532_MAX_APDU_LEN      (252U)

/** @brief 7-bit I²C slave address of the PN532 (fixed in hardware). */
#define PN532_I2C_ADDRESS       (0x24U)

/* ── Transport selector ─────────────────────────────────────────── */

/**
 * @enum pn532_transport_t
 * @brief Physical bus used to communicate with the PN532.
 *
 * | Value                    | Bus     | Typical use                             |
 * |--------------------------|---------|------------------------------------------|
 * | @c PN532_TRANSPORT_SPI   | SPI     | ESP32-S3 dev kit + Keyestudio breakout  |
 * | @c PN532_TRANSPORT_I2C   | I²C     | Cheap Yellow Display (CN1 connector)    |
 */
typedef enum {
    PN532_TRANSPORT_SPI, /**< SPI master via ESP-IDF @c spi_master driver. */
    PN532_TRANSPORT_I2C, /**< I²C master via ESP-IDF v5.x @c i2c_master driver. */
} pn532_transport_t;

/* ── Configuration structure ────────────────────────────────────── */

/**
 * @struct pn532_config_t
 * @brief Compile-time configuration passed to @ref pn532_init.
 *
 * Only the fields that correspond to the selected @ref transport need to be
 * initialised; fields for the other transport are ignored.
 *
 * @par SPI example
 * @code
 * pn532_config_t cfg = {};
 * cfg.transport     = PN532_TRANSPORT_SPI;
 * cfg.spi_host      = SPI2_HOST;
 * cfg.pin_cs        = 10;
 * cfg.skip_bus_init = true;
 * @endcode
 *
 * @par I²C example
 * @code
 * pn532_config_t cfg = {};
 * cfg.transport    = PN532_TRANSPORT_I2C;
 * cfg.i2c_port     = 0;
 * cfg.pin_sda      = 27;
 * cfg.pin_scl      = 22;
 * cfg.pin_irq      = -1;
 * cfg.pin_rst      = -1;
 * cfg.i2c_clock_hz = 100000U;
 * @endcode
 */
typedef struct {
    pn532_transport_t transport; /**< Active transport (@ref PN532_TRANSPORT_SPI or @ref PN532_TRANSPORT_I2C). */

    /* ── SPI fields (used when transport == PN532_TRANSPORT_SPI) ── */
    spi_host_device_t spi_host;    /**< SPI peripheral (@c SPI2_HOST or @c SPI3_HOST). */
    int               pin_mosi;   /**< MOSI GPIO number (ignored when @c skip_bus_init is @c true). */
    int               pin_miso;   /**< MISO GPIO number (ignored when @c skip_bus_init is @c true). */
    int               pin_sclk;   /**< SCLK GPIO number (ignored when @c skip_bus_init is @c true). */
    int               pin_cs;     /**< Chip-select GPIO number (software-driven). */
    bool              skip_bus_init; /**< When @c true, skip @c spi_bus_initialize(); caller has already done it. */

    /* ── I²C fields (used when transport == PN532_TRANSPORT_I2C) ── */
    int      i2c_port;    /**< I²C port number (@c I2C_NUM_0 or @c I2C_NUM_1). */
    int      pin_sda;     /**< SDA GPIO number. */
    int      pin_scl;     /**< SCL GPIO number. */
    int      pin_irq;     /**< IRQ input GPIO (-1 = unused; IRQ is not currently used by the driver). */
    int      pin_rst;     /**< Reset output GPIO (-1 = unused; a pulse is asserted on init when provided). */
    uint32_t i2c_clock_hz; /**< I²C clock frequency in Hz (@c 100000 standard, @c 400000 fast). */
} pn532_config_t;

/* ── Runtime device state ───────────────────────────────────────── */

/**
 * @struct pn532_t
 * @brief Opaque-like runtime state for a single PN532 instance.
 *
 * Populated by @ref pn532_init; must remain valid for the lifetime of all
 * subsequent driver calls.  Do not read or write fields directly — treat this
 * as an opaque handle.
 */
typedef struct {
    pn532_transport_t transport; /**< Active transport (copied from @ref pn532_config_t). */

    /* ── SPI state ── */
    spi_device_handle_t spi;    /**< SPI device handle returned by @c spi_bus_add_device(). */
    int                 pin_cs; /**< Chip-select GPIO (cached from config for fast toggling). */

    /* ── I²C state ── */
    i2c_master_bus_handle_t i2c_bus; /**< I²C bus handle returned by @c i2c_new_master_bus(). */
    i2c_master_dev_handle_t i2c_dev; /**< I²C device handle returned by @c i2c_master_bus_add_device(). */
    int                     pin_irq; /**< IRQ GPIO (-1 when unused). */
    int                     pin_rst; /**< Reset GPIO (-1 when unused). */
} pn532_t;

/* ── Public API ─────────────────────────────────────────────────── */

/**
 * @brief Initialise the PN532 and bring it to a ready state.
 *
 * Performs the full transport-specific bring-up:
 * - **SPI**: configures the SPI device, runs the wake-up byte sequence, sends
 *   a sacrificial @ref pn532_sam_config (absorbs the post-power-on latency),
 *   then confirms readiness with @ref pn532_get_firmware_version.
 * - **I²C**: configures the I²C bus and device, sends two SAMConfig frames to
 *   recover from Soft-Power-Down if the PN532 was already powered, then
 *   confirms readiness with @ref pn532_get_firmware_version.
 *
 * @param[out] dev    Device state structure to populate.  Must remain valid
 *                    for the lifetime of all subsequent calls on this device.
 * @param[in]  config Transport and pin configuration.  Caller may free or
 *                    reuse this after @c pn532_init returns.
 * @return @c ESP_OK on success, an @c esp_err_t error code otherwise.
 *
 * @note The return value of the internal firmware-version and sacrificial
 *       SAMConfig calls is intentionally ignored; they are used purely for
 *       timing and FIFO-drain purposes.
 */
esp_err_t pn532_init(pn532_t *dev, const pn532_config_t *config);

/**
 * @brief Query the PN532 firmware version.
 *
 * Sends the @c GetFirmwareVersion command (0x02) and reads the response.
 * Also serves as a FIFO-drain step after init to prevent stale bytes from
 * confusing subsequent command sequences.
 *
 * @param[in] dev Initialised device handle.
 * @return Packed 32-bit version word:
 *         @c (IC << 24) | (Ver << 16) | (Rev << 8) | Support,
 *         or @c 0 on communication failure.
 *
 * @see pn532_init
 */
uint32_t pn532_get_firmware_version(pn532_t *dev);

/**
 * @brief Configure the PN532's Security Access Module (SAM).
 *
 * Sends the @c SAMConfiguration command (0x14) with Normal Mode, a 1-second
 * timeout, and IRQ enabled.  Must be called (at least once) before listing
 * passive targets or exchanging APDUs.
 *
 * @ref pn532_init already issues this command internally; application code
 * calls it again via @c CW_NfcTransport::begin() / @ref Pn532NfcTransport::begin.
 *
 * @param[in] dev Initialised device handle.
 * @return @c true on success (PN532 acknowledged and returned response code
 *         @c 0x15), @c false on timeout or unexpected response.
 */
bool pn532_sam_config(pn532_t *dev);

/**
 * @brief Scan for a passive ISO 14443-A card and return its UID.
 *
 * Sends @c InListPassiveTarget (0x4A) with @c maxTargets = 1.  Blocks until
 * a card is found or the command times out inside the PN532 firmware.
 *
 * @param[in] dev         Initialised device handle.
 * @param[in] cardbaudrate Baud-rate selector; use @ref PN532_MIFARE_ISO14443A
 *                         for standard Mifare / ISO 14443-A cards.
 * @return 32-bit packed card UID (UID bytes packed MSB-first), or @c 0 if
 *         no card was found or a communication error occurred.
 *
 * @note UIDs longer than 4 bytes are truncated to the most-significant 32 bits.
 */
uint32_t pn532_read_passive_target_id(pn532_t *dev, uint8_t cardbaudrate);

/**
 * @brief Exchange a single ISO-DEP APDU with the currently selected card.
 *
 * Wraps the PN532 @c InDataExchange command (0x40).  Handles both normal
 * PN532 frames (LEN ≤ 255) and extended frames (LEN > 255) transparently;
 * the frame format is detected from the response header.
 *
 * @param[in]     dev          Initialised device handle.
 * @param[in]     apdu         APDU command bytes (must not be @c NULL).
 * @param[in]     apdu_len     Length of @p apdu; must be ≤ @ref PN532_MAX_APDU_LEN.
 * @param[out]    response     Caller-allocated buffer for the card's DataOut.
 * @param[in,out] response_len In: capacity of @p response in bytes.
 *                             Out: number of DataOut bytes actually written.
 * @return @c true on success (PN532 error byte = 0x00), @c false on a PN532
 *         transport error, a card-level error, or if @p apdu_len exceeds
 *         @ref PN532_MAX_APDU_LEN.
 *
 * @warning @p response must be large enough to hold the full DataOut; if
 *          capacity is exceeded the copy is silently truncated to @p *response_len.
 *
 * @see pn532_release_target
 */
bool pn532_send_apdu(pn532_t *dev, const uint8_t *apdu, uint8_t apdu_len,
                     uint8_t *response, uint16_t *response_len);

/**
 * @brief Release the currently selected NFC target.
 *
 * Sends @c InRelease (0x52) so the PN532 drops the logical target and is
 * ready to list a new one.  Should be called after finishing a card session
 * before the next @ref pn532_read_passive_target_id.
 *
 * @param[in] dev Initialised device handle.
 * @return @c true if the PN532 acknowledged the release successfully,
 *         @c false on communication failure.
 */
bool pn532_release_target(pn532_t *dev);

/** @} */ /* end of pn532_driver group */

#ifdef __cplusplus
}
#endif
