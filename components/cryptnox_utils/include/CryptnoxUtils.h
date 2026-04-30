#pragma once

#include <stdint.h>
#include <stddef.h>

class CryptnoxUtils {
public:
    /* Returns false if neither WiFi nor Bluetooth is active (hardware TRNG not seeded). */
    static bool fill_secure_random(uint8_t *dest, size_t len);
};
