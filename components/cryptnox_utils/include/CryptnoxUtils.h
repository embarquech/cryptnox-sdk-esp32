#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Fill @p len bytes at @p dest with cryptographically random data from
 * the ESP32 hardware TRNG (esp_fill_random).  Returns false only if
 * dest is NULL or len is zero.
 */
bool CryptnoxUtils_random(uint8_t *dest, size_t len);

/**
 * Constant-time byte comparison — safe against timing side-channels.
 * Returns true if the first @p len bytes of @p a and @p b are identical.
 */
bool CryptnoxUtils_secure_compare(const uint8_t *a, const uint8_t *b, size_t len);

/**
 * Overwrite @p len bytes at @p buf with zero in a way the compiler
 * cannot optimise away.
 */
void CryptnoxUtils_secure_wipe(uint8_t *buf, size_t len);

/**
 * Bounds-checked memcpy.  Copies @p count bytes from @p src into @p dst
 * (capacity @p dstSize).  Returns false and leaves dst untouched if the
 * copy would overflow or if src/dst overlap.
 */
bool CryptnoxUtils_safe_memcpy(uint8_t *dst, size_t dstSize,
                               const uint8_t *src, size_t count);

#ifdef __cplusplus
}

/**
 * C++ wrapper — mirrors the SDK's CryptnoxUtils class API so existing
 * SDK callers compile without changes.
 */
class CryptnoxUtils {
public:
    static bool random(uint8_t *dest, size_t len) {
        return CryptnoxUtils_random(dest, len);
    }
    static bool secure_compare(const uint8_t *a, const uint8_t *b, size_t len) {
        return CryptnoxUtils_secure_compare(a, b, len);
    }
    static void secure_wipe(uint8_t *buf, size_t len) {
        CryptnoxUtils_secure_wipe(buf, len);
    }
    static bool safe_memcpy(uint8_t *dst, size_t dstSize,
                            const uint8_t *src, size_t count) {
        return CryptnoxUtils_safe_memcpy(dst, dstSize, src, count);
    }
};
#endif
