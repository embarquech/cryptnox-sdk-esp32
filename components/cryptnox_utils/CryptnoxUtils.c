#include "CryptnoxUtils.h"
#include "esp_random.h"
#include <string.h>

bool CryptnoxUtils_random(uint8_t *dest, size_t len)
{
    if (!dest || len == 0)
        return false;
    esp_fill_random(dest, len);
    return true;
}

bool CryptnoxUtils_secure_compare(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++)
        diff |= a[i] ^ b[i];
    return diff == 0;
}

void CryptnoxUtils_secure_wipe(uint8_t *buf, size_t len)
{
    volatile uint8_t *p = buf;
    while (len--)
        *p++ = 0;
}

bool CryptnoxUtils_safe_memcpy(uint8_t *dst, size_t dstSize,
                               const uint8_t *src, size_t count)
{
    if (!dst || !src || count == 0 || count > dstSize)
        return false;
    /* Reject overlapping regions */
    if (dst < src + count && src < dst + count)
        return false;
    memcpy(dst, src, count);
    return true;
}
