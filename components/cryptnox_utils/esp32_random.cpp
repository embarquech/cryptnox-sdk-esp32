#include "CW_Utils.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_bt.h"
#include "esp_log.h"

static const char* const TAG = "CW_Utils";

bool CW_Utils::fill_secure_random(uint8_t *dest, size_t len) {
    bool result = false;

    if ((dest != nullptr) && (len != 0U)) {
        wifi_mode_t mode    = WIFI_MODE_NULL;
        bool        wifi_on = ((esp_wifi_get_mode(&mode) == ESP_OK) && (mode != WIFI_MODE_NULL));
        bool        bt_on   = (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED);

        if ((!wifi_on) && (!bt_on)) {
            ESP_LOGE(TAG, "RNG called without WiFi/BT active — entropy NOT guaranteed");
        } else {
            esp_fill_random(dest, len);
            result = true;
        }
    }

    return result;
}
