#include "CryptnoxUtils.h"
#include "esp_random.h"

bool CryptnoxUtils::random(uint8_t* dest, size_t len) {
    if (!dest || len == 0U) return false;
    esp_fill_random(dest, len);
    return true;
}
