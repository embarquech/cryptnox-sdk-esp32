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

        /* ESP32-S3 TRNG operates from thermal noise and ring-oscillator jitter
         * even without a radio, but a live WiFi/BT subsystem feeds additional
         * hardware entropy.  Always fill the buffer; warn when reduced-entropy
         * mode is active so callers are aware. */
        if ((!wifi_on) && (!bt_on)) {
            ESP_LOGW(TAG, "RNG: no radio active — reduced entropy (TRNG only)");
        }

        esp_fill_random(dest, len);
        result = true;
    }

    return result;
}
