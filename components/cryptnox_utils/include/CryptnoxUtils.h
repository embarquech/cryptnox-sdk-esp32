#pragma once

#include <stdint.h>
#include <stddef.h>

class CryptnoxUtils {
public:
    /* Returns false if dest is NULL or len is zero. */
    static bool fill_secure_random(uint8_t *dest, size_t len);
};
