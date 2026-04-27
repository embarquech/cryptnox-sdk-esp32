#ifndef ESP32_LOGGER_H
#define ESP32_LOGGER_H

/******************************************************************
 * 1. Included files
 ******************************************************************/

#include "CW_Logger.h"

/******************************************************************
 * 2. Class declaration
 ******************************************************************/

class ESP32Logger : public CW_Logger {
public:
    bool begin(unsigned long baudRate = 115200UL) override;

    void print(const __FlashStringHelper *str)              override;
    void print(const char *str)                             override;
    void print(char c)                                      override;
    void print(uint8_t  value, int base = DEC)              override;
    void print(uint16_t value, int base = DEC)              override;
    void print(uint32_t value, int base = DEC)              override;
    void print(int      value, int base = DEC)              override;

    void println()                                          override;
    void println(const __FlashStringHelper *str)            override;
    void println(const char *str)                           override;
    void println(char c)                                    override;
    void println(uint8_t  value, int base = DEC)            override;
    void println(uint16_t value, int base = DEC)            override;
    void println(uint32_t value, int base = DEC)            override;
    void println(int      value, int base = DEC)            override;

    ~ESP32Logger() override {}

private:
    bool m_initialized = false;
};

#endif /* ESP32_LOGGER_H */
