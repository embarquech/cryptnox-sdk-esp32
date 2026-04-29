#include "CryptnoxUtils.h"
#include "esp_random.h"

#ifdef CONFIG_ESP_WIFI_ENABLED
#include "esp_wifi.h"
#endif

#ifdef CONFIG_BT_ENABLED
#include "esp_bt.h"
#endif

#ifdef CONFIG_ESP_WIFI_ENABLED
static bool wifi_is_active(void) {
    wifi_mode_t mode        = WIFI_MODE_NULL;
    esp_err_t   err         = esp_wifi_get_mode(&mode);
    bool        wifi_active = ((err == ESP_OK) && (mode != WIFI_MODE_NULL));
    return wifi_active;
}
#endif

#ifdef CONFIG_BT_ENABLED
static bool bt_is_active(void) {
    esp_bt_controller_status_t status    = esp_bt_controller_get_status();
    bool                       bt_active = (status == ESP_BT_CONTROLLER_STATUS_ENABLED);
    return bt_active;
}
#endif

bool CryptnoxUtils::fill_secure_random(uint8_t *dest, size_t len) {
    bool trng_seeded = false;
#ifdef CONFIG_ESP_WIFI_ENABLED
    trng_seeded = (trng_seeded || wifi_is_active());
#endif
#ifdef CONFIG_BT_ENABLED
    trng_seeded = (trng_seeded || bt_is_active());
#endif
    bool is_ready = ((dest != nullptr) && (len != 0U) && trng_seeded);
    if (is_ready) {
        esp_fill_random(dest, len);
    }
    return is_ready;
}
