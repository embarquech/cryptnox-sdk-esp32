/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file ESP32Logger.h
 * @brief @ref CW_Logger implementation that writes to ESP32 UART0 via @c printf.
 *
 * @ref ESP32Logger maps every @ref CW_Logger virtual method to the ESP-IDF
 * @c uart_write_bytes / @c printf family so the platform-independent SDK core
 * can produce human-readable diagnostic output on the serial console without
 * depending on Arduino's @c Serial object.
 *
 * @par Usage
 * @code
 * ESP32Logger logger;
 * logger.begin(115200UL);
 * CryptnoxWallet wallet(transport, logger, crypto, platform);
 * @endcode
 *
 * @ingroup esp32_adapters
 *
 * @defgroup esp32_adapters ESP32 concrete adapters
 * @brief Concrete @ref CW_Logger, @ref CW_Platform, and @ref CW_CryptoProvider
 *        implementations for ESP32 / ESP-IDF.
 */

#ifndef ESP32_LOGGER_H
#define ESP32_LOGGER_H

#include "CW_Logger.h"

/**
 * @class ESP32Logger
 * @ingroup esp32_adapters
 * @brief @ref CW_Logger backed by ESP32 UART0.
 *
 * Outputs all log traffic through UART0 (the default USB-serial console on
 * most ESP32 dev kits).  The @c __FlashStringHelper overloads are accepted for
 * Arduino source-compatibility but the pointer is treated as a plain RAM
 * pointer — there is no Harvard-architecture flash on ESP32.
 *
 * @note Call @ref begin once before passing this logger to @ref CryptnoxWallet;
 *       calling any @c print / @c println method before @ref begin is a no-op.
 *
 * @warning In production firmware, replace this logger with a null
 *          implementation (a @ref CW_Logger that silently discards all output).
 *          Leaving @ref ESP32Logger active on a production device exposes full
 *          APDU traces and PIN values on the serial console (LOW-03).
 */
class ESP32Logger : public CW_Logger {
public:
    /** @name Initialisation */
    ///@{

    /**
     * @brief Initialise UART0 at the given baud rate.
     *
     * @param[in] baudRate UART baud rate (default @c 115200).
     * @return @c true on success, @c false if UART driver installation fails.
     */
    bool begin(unsigned long baudRate = 115200UL) override;

    ///@}

    /** @name Print (no newline) */
    ///@{

    /**
     * @brief Print a PROGMEM string.
     *
     * On ESP32 the pointer is treated as a plain RAM pointer; no special
     * flash-read logic is applied.
     *
     * @param[in] str NUL-terminated string to print (must not be @c NULL).
     */
    void print(const __FlashStringHelper *str) override;

    /**
     * @brief Print a NUL-terminated C string.
     *
     * @param[in] str String to print (must not be @c NULL).
     */
    void print(const char *str) override;

    /**
     * @brief Print a single character.
     *
     * @param[in] c Character to print.
     */
    void print(char c) override;

    /**
     * @brief Print an 8-bit unsigned integer.
     *
     * @param[in] value Integer to print.
     * @param[in] base  Numeric base: @c DEC (10), @c HEX (16), @c OCT (8),
     *                  or @c BIN (2).  Defaults to @c DEC.
     */
    void print(uint8_t  value, int base = DEC) override;

    /**
     * @brief Print a 16-bit unsigned integer.
     *
     * @param[in] value Integer to print.
     * @param[in] base  Numeric base (see @ref print(uint8_t,int)).
     */
    void print(uint16_t value, int base = DEC) override;

    /**
     * @brief Print a 32-bit unsigned integer.
     *
     * @param[in] value Integer to print.
     * @param[in] base  Numeric base (see @ref print(uint8_t,int)).
     */
    void print(uint32_t value, int base = DEC) override;

    /**
     * @brief Print a signed integer.
     *
     * Outputs a @c '-' prefix for negative values when @p base is @c DEC.
     *
     * @param[in] value Integer to print.
     * @param[in] base  Numeric base (see @ref print(uint8_t,int)).
     */
    void print(int value, int base = DEC) override;

    ///@}

    /** @name Println (with trailing newline) */
    ///@{

    /**
     * @brief Print a CR+LF newline sequence.
     */
    void println() override;

    /**
     * @brief Print a PROGMEM string followed by a newline.
     *
     * @param[in] str NUL-terminated string to print (must not be @c NULL).
     */
    void println(const __FlashStringHelper *str) override;

    /**
     * @brief Print a NUL-terminated C string followed by a newline.
     *
     * @param[in] str String to print (must not be @c NULL).
     */
    void println(const char *str) override;

    /**
     * @brief Print a single character followed by a newline.
     *
     * @param[in] c Character to print.
     */
    void println(char c) override;

    /**
     * @brief Print an 8-bit unsigned integer followed by a newline.
     *
     * @param[in] value Integer to print.
     * @param[in] base  Numeric base (see @ref print(uint8_t,int)).
     */
    void println(uint8_t  value, int base = DEC) override;

    /**
     * @brief Print a 16-bit unsigned integer followed by a newline.
     *
     * @param[in] value Integer to print.
     * @param[in] base  Numeric base (see @ref print(uint8_t,int)).
     */
    void println(uint16_t value, int base = DEC) override;

    /**
     * @brief Print a 32-bit unsigned integer followed by a newline.
     *
     * @param[in] value Integer to print.
     * @param[in] base  Numeric base (see @ref print(uint8_t,int)).
     */
    void println(uint32_t value, int base = DEC) override;

    /**
     * @brief Print a signed integer followed by a newline.
     *
     * @param[in] value Integer to print.
     * @param[in] base  Numeric base (see @ref print(uint8_t,int)).
     */
    void println(int value, int base = DEC) override;

    ///@}

    /** @brief Default destructor. */
    ~ESP32Logger() override {}

private:
    bool m_initialized = false; ///< @c true after a successful @ref begin call.
};

#endif /* ESP32_LOGGER_H */
