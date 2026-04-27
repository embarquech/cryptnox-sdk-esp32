#include "CryptnoxUtils.h"
#include "esp_random.h"
#include "esp_wifi.h"

static bool wifi_is_active(void) {
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_err_t err = esp_wifi_get_mode(&mode);
    bool wifi_active = ((err == ESP_OK) && (mode != WIFI_MODE_NULL));
    return wifi_active;
}

bool CryptnoxUtils::fill_secure_random(uint8_t* dest, size_t len) {
    bool wifi_ready = wifi_is_active();
    bool is_ready = (dest != nullptr) && (len != 0U) && wifi_ready;

    if (is_ready) {
        esp_fill_random(dest, len);
    }

    return is_ready;
}
