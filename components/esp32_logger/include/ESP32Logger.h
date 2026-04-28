#ifndef ESP32_LOGGER_H
#define ESP32_LOGGER_H

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include "CW_Logger.h"

/******************************************************************
 * 2. Class declaration
 ******************************************************************/

/** ESP32 UART0 adapter implementing the CW_Logger interface. */
class ESP32Logger : public CW_Logger {
public:
    /** Initialise UART0 at the given baud rate; returns true on success. */
    bool begin(unsigned long baudRate = 115200UL) override;

    /** Print a PROGMEM string (treated as a RAM pointer on ESP32). */
    void print(const __FlashStringHelper *str)              override;
    /** Print a NUL-terminated C string. */
    void print(const char *str)                             override;
    /** Print a single character. */
    void print(char c)                                      override;
    /** Print an 8-bit unsigned integer in the given base. */
    void print(uint8_t  value, int base = DEC)              override;
    /** Print a 16-bit unsigned integer in the given base. */
    void print(uint16_t value, int base = DEC)              override;
    /** Print a 32-bit unsigned integer in the given base. */
    void print(uint32_t value, int base = DEC)              override;
    /** Print a signed integer in the given base; outputs a '-' prefix for negative decimals. */
    void print(int      value, int base = DEC)              override;

    /** Print a CR+LF newline sequence. */
    void println()                                          override;
    /** Print a PROGMEM string followed by a newline. */
    void println(const __FlashStringHelper *str)            override;
    /** Print a NUL-terminated C string followed by a newline. */
    void println(const char *str)                           override;
    /** Print a single character followed by a newline. */
    void println(char c)                                    override;
    /** Print an 8-bit unsigned integer followed by a newline. */
    void println(uint8_t  value, int base = DEC)            override;
    /** Print a 16-bit unsigned integer followed by a newline. */
    void println(uint16_t value, int base = DEC)            override;
    /** Print a 32-bit unsigned integer followed by a newline. */
    void println(uint32_t value, int base = DEC)            override;
    /** Print a signed integer followed by a newline. */
    void println(int      value, int base = DEC)            override;

    ~ESP32Logger() override {}

private:
    bool m_initialized = false;
};

#endif /* ESP32_LOGGER_H */
