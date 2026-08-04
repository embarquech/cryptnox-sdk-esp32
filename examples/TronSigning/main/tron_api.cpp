/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file tron_api.cpp
 * @brief Implementation of the minimal TRON full-node HTTP client.
 *
 * Same shape as the Ethereum example's eth_rpc.cpp: one HTTPS POST helper
 * over esp_http_client, plus strstr-based field extraction. A JSON parser
 * would be a heavier dependency than the three fields this example reads.
 */

#include "tron_api.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>     /* malloc, free */
#include <inttypes.h>   /* PRIu64 */

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

static const char *const TAG = "tron_api";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAX_RETRY      5
#define WIFI_TIMEOUT_MS     30000

#define URL_BUF_SIZE        192U
#define CREATE_BODY_SIZE    256U
/* "signature":["<130 hex chars>"], plus NUL — 148 bytes, rounded up. */
#define SIG_FIELD_SIZE      160U

#define HEX_PER_BYTE        2U

/******************************************************************
 * Module state
 ******************************************************************/

static const char *s_base_url = NULL;
static const char *s_api_key  = NULL;

static EventGroupHandle_t s_wifi_event_group;
static int                s_retry_num = 0;

/******************************************************************
 * WiFi event handler
 ******************************************************************/

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if ((event_base == WIFI_EVENT) && (event_id == WIFI_EVENT_STA_START)) {
        esp_wifi_connect();
    } else if ((event_base == WIFI_EVENT) &&
               (event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "WiFi retry %d/%d", s_retry_num, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP)) {
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else {
        /* other events ignored */
    }
}

/******************************************************************
 * HTTP helper
 ******************************************************************/

/*
 * POST 'body' to s_base_url + path over HTTPS and read the response into
 * resp_buf (NUL-terminated on success). Returns true if data was read.
 */
static bool do_post(const char *path, const char *body,
                    char *resp_buf, size_t resp_buf_size)
{
    bool success = false;

    char url[URL_BUF_SIZE];
    if ((size_t)snprintf(url, sizeof(url), "%s%s", s_base_url, path) >= sizeof(url)) {
        ESP_LOGE(TAG, "URL too long");
        return false;
    }

    esp_http_client_config_t cfg;
    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.url               = url;
    cfg.method            = HTTP_METHOD_POST;
    cfg.timeout_ms        = 15000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return false;
    }

    (void)esp_http_client_set_header(client, "Content-Type", "application/json");
    if ((s_api_key != NULL) && (s_api_key[0] != '\0')) {
        (void)esp_http_client_set_header(client, "TRON-PRO-API-KEY", s_api_key);
    }

    int body_len = (int)strlen(body);
    esp_err_t err = esp_http_client_open(client, body_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open: %s", esp_err_to_name(err));
        goto cleanup;
    }

    if (esp_http_client_write(client, body, body_len) != body_len) {
        ESP_LOGE(TAG, "HTTP write incomplete");
        goto cleanup;
    }

    {
        (void)esp_http_client_fetch_headers(client);  /* may be -1 when chunked */

        int total = 0;
        int read;
        do {
            int space = (int)(resp_buf_size - 1U) - total;
            if (space <= 0) { break; }
            read = esp_http_client_read(client, resp_buf + total, space);
            if (read > 0) { total += read; }
        } while (read > 0);

        resp_buf[total] = '\0';
        success = (total > 0);
    }

cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return success;
}

/******************************************************************
 * Public API
 ******************************************************************/

void tron_api_init(const char *base_url, const char *api_key)
{
    s_base_url = base_url;
    s_api_key  = api_key;
}

bool tron_api_wifi_connect(const char *ssid, const char *password)
{
    s_wifi_event_group = xEventGroupCreate();
    s_retry_num = 0;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    (void)esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t h_any;
    esp_event_handler_instance_t h_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &h_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &h_ip));

    wifi_config_t wifi_cfg;
    (void)memset(&wifi_cfg, 0, sizeof(wifi_cfg));
    (void)strncpy((char *)wifi_cfg.sta.ssid,     ssid,     sizeof(wifi_cfg.sta.ssid)     - 1U);
    (void)strncpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password) - 1U);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to \"%s\"...", ssid);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_TIMEOUT_MS));

    bool connected = ((bits & WIFI_CONNECTED_BIT) != 0U);
    if (connected) {
        ESP_LOGI(TAG, "WiFi connected");
    } else {
        ESP_LOGE(TAG, "WiFi connect failed");
    }

    (void)esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, h_ip);
    (void)esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, h_any);
    vEventGroupDelete(s_wifi_event_group);

    return connected;
}

bool tron_api_create_transfer(const char *owner_b58, const char *to_b58,
                              uint64_t amount,
                              char *tx_json, size_t tx_json_size)
{
    char body[CREATE_BODY_SIZE];
    (void)snprintf(body, sizeof(body),
                   "{\"owner_address\":\"%s\",\"to_address\":\"%s\","
                   "\"amount\":%" PRIu64 ",\"visible\":true}",
                   owner_b58, to_b58, amount);

    if (!do_post("/wallet/createtransaction", body, tx_json, tx_json_size)) {
        return false;
    }

    /* The node reports build errors as {"Error":"..."} with HTTP 200. */
    if (strstr(tx_json, "\"txID\"") == NULL) {
        ESP_LOGE(TAG, "createtransaction failed: %s", tx_json);
        return false;
    }

    return true;
}

bool tron_api_json_string(const char *json, const char *key,
                          char *out, size_t out_size)
{
    char pattern[32];
    if ((size_t)snprintf(pattern, sizeof(pattern), "\"%s\":\"", key) >= sizeof(pattern)) {
        return false;
    }

    const char *start = strstr(json, pattern);
    if (start == NULL) {
        return false;
    }
    start += strlen(pattern);

    const char *end = strchr(start, '"');
    if (end == NULL) {
        return false;
    }

    size_t len = (size_t)(end - start);
    if ((len + 1U) > out_size) {
        return false;
    }

    (void)memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

bool tron_api_hex_to_bytes(const char *hex, uint8_t *out, size_t out_size,
                           size_t *out_len)
{
    size_t hex_len = strlen(hex);
    if (((hex_len % HEX_PER_BYTE) != 0U) || ((hex_len / HEX_PER_BYTE) > out_size)) {
        return false;
    }

    size_t i;
    for (i = 0U; i < (hex_len / HEX_PER_BYTE); i++) {
        uint8_t byte = 0U;
        for (size_t n = 0U; n < HEX_PER_BYTE; n++) {
            char c = hex[(i * HEX_PER_BYTE) + n];
            uint8_t nib;
            if ((c >= '0') && (c <= '9'))        { nib = (uint8_t)(c - '0'); }
            else if ((c >= 'a') && (c <= 'f'))   { nib = (uint8_t)(c - 'a' + 10); }
            else if ((c >= 'A') && (c <= 'F'))   { nib = (uint8_t)(c - 'A' + 10); }
            else                                 { return false; }
            byte = (uint8_t)((byte << 4U) | nib);
        }
        out[i] = byte;
    }

    *out_len = i;
    return true;
}

bool tron_api_broadcast(const char *tx_json,
                        const uint8_t signature[TRON_SIGNATURE_BYTES],
                        char *resp, size_t resp_size)
{
    static const char HEX[] = "0123456789abcdef";

    /* Never leave the caller's buffer unwritten — it gets logged on failure. */
    if ((resp == NULL) || (resp_size == 0U)) {
        return false;
    }
    resp[0] = '\0';

    if (tx_json[0] != '{') {
        return false;
    }

    /* Splice "signature":["<hex>"], in right after the opening brace so the
     * node receives back exactly the transaction object it produced. */
    char sig_field[SIG_FIELD_SIZE];
    size_t p = 0U;
    const char *prefix = "\"signature\":[\"";
    (void)memcpy(&sig_field[p], prefix, strlen(prefix));
    p += strlen(prefix);
    for (size_t i = 0U; i < TRON_SIGNATURE_BYTES; i++) {
        sig_field[p++] = HEX[(signature[i] >> 4U) & 0x0FU];
        sig_field[p++] = HEX[signature[i] & 0x0FU];
    }
    sig_field[p++] = '"';
    sig_field[p++] = ']';
    sig_field[p++] = ',';
    sig_field[p]   = '\0';

    size_t body_size = strlen(tx_json) + p + 2U;
    char *body = (char *)malloc(body_size);
    if (body == NULL) {
        return false;
    }
    (void)snprintf(body, body_size, "{%s%s", sig_field, &tx_json[1]);

    bool posted = do_post("/wallet/broadcasttransaction", body, resp, resp_size);
    free(body);

    if (!posted) {
        return false;
    }

    return (strstr(resp, "\"result\":true") != NULL);
}
