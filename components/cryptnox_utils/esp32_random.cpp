#include "CW_Utils.h"
#include "esp_random.h"

bool CW_Utils::fill_secure_random(uint8_t *dest, size_t len) {
    bool is_ready = ((dest != nullptr) && (len != 0U));
    if (is_ready) {
        esp_fill_random(dest, len);
    }
    return is_ready;
}
