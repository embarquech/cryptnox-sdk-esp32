/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

#include "CW_Utils.h"
#include "esp_random.h"
#include "esp_wifi.h"
#ifdef CONFIG_BT_ENABLED
#  include "esp_bt.h"
#endif
#include "esp_log.h"

static const char* const TAG = "CW_Utils";

bool CW_Utils::fill_secure_random(uint8_t *dest, size_t len) {
    bool result = false;

    if ((dest != nullptr) && (len != 0U)) {
        wifi_mode_t mode    = WIFI_MODE_NULL;
        bool        wifi_on = ((esp_wifi_get_mode(&mode) == ESP_OK) && (mode != WIFI_MODE_NULL));

#ifdef CONFIG_BT_ENABLED
        bool bt_on = (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED);
#else
        bool bt_on = false;
#endif

        /* SEC-001: esp_fill_random() is only a true RNG while Wi-Fi or BT runs
         * (esp_random.h). On the classic ESP32 the radio-off output is not true
         * random, so that case is a hard failure, as CW_Utils.h documents.
         * The buffer is still filled, so a caller that may accept weaker bytes
         * (a keypad layout, never a key) can do so knowingly.
         * ponytail: wifi mode != NULL means the driver is configured, not that
         * esp_wifi_start() ran; IDF has no public "RF running" query. */
        esp_fill_random(dest, len);
        result = wifi_on || bt_on;
        if (!result) {
            ESP_LOGE(TAG, "RNG: no radio active - not a true RNG (SEC-001)");
        }
    }

    return result;
}
