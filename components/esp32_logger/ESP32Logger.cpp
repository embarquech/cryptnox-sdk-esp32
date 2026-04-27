/******************************************************************
 * 1. Included files
 ******************************************************************/

#include "ESP32Logger.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"

/******************************************************************
 * 2. Module constants
 ******************************************************************/

static const uart_port_t UART_LOG_PORT              = UART_NUM_0;
static const uint8_t     UART_RX_FLOW_THRESH_NONE   = static_cast<uint8_t>(0U);

/* 32 binary digits + NUL terminator */
static const uint32_t NUM_BUF_SIZE  = 33U;
static const uint32_t NUM_BASE_MIN  = 2U;
static const uint32_t NUM_BASE_MAX  = 16U;
static const size_t   ELEMENT_SIZE  = 1U;

static const char LOGGER_NEWLINE[] = "\r\n";
static const char HEX_CHARS[]      = "0123456789ABCDEF";

/******************************************************************
 * 3. Static helpers
 ******************************************************************/

static uint32_t clamp_base(int base)
{
    uint32_t result = static_cast<uint32_t>(DEC);
    bool in_range   = ((static_cast<uint32_t>(base) >= NUM_BASE_MIN) &&
                       (static_cast<uint32_t>(base) <= NUM_BASE_MAX));
    if (in_range) {
        result = static_cast<uint32_t>(base);
    }
    return result;
}

static void uart_write_str(const char *str)
{
    size_t len = strlen(str);
    (void)fwrite(str, ELEMENT_SIZE, len, stdout);
}

static void write_uint_to_uart(uint32_t value, uint32_t base)
{
    char     buf[NUM_BUF_SIZE] = {};
    uint32_t pos               = NUM_BUF_SIZE - 1U;
    uint32_t v                 = value;

    buf[pos] = '\0';

    if (v == 0U) {
        pos--;
        buf[pos] = '0';
    } else {
        while (v > 0U) {
            pos--;
            buf[pos] = HEX_CHARS[v % base];
            v        = v / base;
        }
    }

    (void)fputs(&buf[pos], stdout);
}

/******************************************************************
 * 4. Public method implementations
 ******************************************************************/

bool ESP32Logger::begin(unsigned long baudRate)
{
    uart_config_t cfg = {};
    cfg.baud_rate           = static_cast<int>(baudRate);
    cfg.data_bits           = UART_DATA_8_BITS;
    cfg.parity              = UART_PARITY_DISABLE;
    cfg.stop_bits           = UART_STOP_BITS_1;
    cfg.flow_ctrl           = UART_HW_FLOWCTRL_DISABLE;
    cfg.rx_flow_ctrl_thresh = UART_RX_FLOW_THRESH_NONE;
    cfg.source_clk          = UART_SCLK_DEFAULT;

    esp_err_t err  = uart_param_config(UART_LOG_PORT, &cfg);
    m_initialized  = (err == ESP_OK);

    return m_initialized;
}

void ESP32Logger::print(const __FlashStringHelper *str)
{
    if (m_initialized) {
        uart_write_str(reinterpret_cast<const char *>(str));
    }
}

void ESP32Logger::print(const char *str)
{
    if (m_initialized) {
        uart_write_str(str);
    }
}

void ESP32Logger::print(char c)
{
    if (m_initialized) {
        (void)fputc(static_cast<int>(c), stdout);
    }
}

void ESP32Logger::print(uint8_t value, int base)
{
    if (m_initialized) {
        uint32_t safe_base = clamp_base(base);
        write_uint_to_uart(static_cast<uint32_t>(value), safe_base);
    }
}

void ESP32Logger::print(uint16_t value, int base)
{
    if (m_initialized) {
        uint32_t safe_base = clamp_base(base);
        write_uint_to_uart(static_cast<uint32_t>(value), safe_base);
    }
}

void ESP32Logger::print(uint32_t value, int base)
{
    if (m_initialized) {
        uint32_t safe_base = clamp_base(base);
        write_uint_to_uart(value, safe_base);
    }
}

void ESP32Logger::print(int value, int base)
{
    if (m_initialized) {
        uint32_t safe_base               = clamp_base(base);
        bool     is_negative_decimal     = ((value < 0) && (safe_base == static_cast<uint32_t>(DEC)));
        if (is_negative_decimal) {
            (void)fputc(static_cast<int>('-'), stdout);
            /* Two's-complement negation — avoids UB on INT_MIN */
            uint32_t abs_val = (~static_cast<uint32_t>(value)) + 1U;
            write_uint_to_uart(abs_val, safe_base);
        } else {
            write_uint_to_uart(static_cast<uint32_t>(value), safe_base);
        }
    }
}

void ESP32Logger::println()
{
    if (m_initialized) {
        uart_write_str(LOGGER_NEWLINE);
    }
}

void ESP32Logger::println(const __FlashStringHelper *str)
{
    print(str);
    println();
}

void ESP32Logger::println(const char *str)
{
    print(str);
    println();
}

void ESP32Logger::println(char c)
{
    print(c);
    println();
}

void ESP32Logger::println(uint8_t value, int base)
{
    print(value, base);
    println();
}

void ESP32Logger::println(uint16_t value, int base)
{
    print(value, base);
    println();
}

void ESP32Logger::println(uint32_t value, int base)
{
    print(value, base);
    println();
}

void ESP32Logger::println(int value, int base)
{
    print(value, base);
    println();
}
