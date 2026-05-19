#include "CW_Utils.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include <string.h>

/******************************************************************
 * Module constants
 ******************************************************************/

/* bytes drawn from the ESP32-S3 hardware TRNG */
#define ENTROPY_TRNG_BYTES      (32U)
/* bytes captured from the high-resolution monotonic timer (sizeof int64_t) */
#define ENTROPY_TIMER_BYTES     (8U)
/* total entropy pool fed into SHA-256 */
#define ENTROPY_POOL_BYTES      (ENTROPY_TRNG_BYTES + ENTROPY_TIMER_BYTES)
/* 32-bit counter appended per output block to produce distinct SHA-256 inputs */
#define ENTROPY_CTR_BYTES       (4U)
/* SHA-256 hash input: pool bytes + counter */
#define ENTROPY_INPUT_BYTES     (ENTROPY_POOL_BYTES + ENTROPY_CTR_BYTES)
/* SHA-256 digest size */
#define ENTROPY_SHA256_OUT      (32U)
/* 0 = SHA-256 mode; 1 = SHA-224 mode */
#define MBEDTLS_SHA256_MODE     (0)
#define MBEDTLS_OK              (0)

/* Counter field byte offsets within hashInput */
#define CTR_BYTE0_OFFSET        (ENTROPY_POOL_BYTES)
#define CTR_BYTE1_OFFSET        (ENTROPY_POOL_BYTES + 1U)
#define CTR_BYTE2_OFFSET        (ENTROPY_POOL_BYTES + 2U)
#define CTR_BYTE3_OFFSET        (ENTROPY_POOL_BYTES + 3U)

bool CW_Utils::fill_secure_random(uint8_t *dest, size_t len) {
    bool result = false;

    if ((dest != nullptr) && (len != 0U)) {
        /* Build an entropy pool from two independent sources and mix them
         * through SHA-256 so the output is at least as strong as the
         * stronger source.  This addresses the pre-radio entropy concern
         * on ESP32-S3: esp_fill_random() uses thermal-noise TRNG, whose
         * quality is lower before Wi-Fi/BT is initialised; the timer
         * sample adds jitter from runtime state that is unpredictable to
         * an off-chip attacker. */
        uint8_t pool[ENTROPY_POOL_BYTES]       = { 0U };
        uint8_t hashInput[ENTROPY_INPUT_BYTES] = { 0U };

        /* Source 1: 32 bytes from the ESP32-S3 hardware TRNG. */
        esp_fill_random(pool, ENTROPY_TRNG_BYTES);

        /* Source 2: 8-byte microsecond-resolution timer (boot-relative).
         * Captures accumulated jitter from NFC polls, FreeRTOS context
         * switches, and interrupt handling since power-on. */
        int64_t           timerVal   = esp_timer_get_time();
        const uint8_t    *timerBytes = reinterpret_cast<const uint8_t *>(&timerVal);
        for (uint8_t i = 0U; i < ENTROPY_TIMER_BYTES; i++) {
            pool[ENTROPY_TRNG_BYTES + static_cast<size_t>(i)] = timerBytes[i];
        }

        /* Copy the full pool into the hash-input buffer.  The last four
         * bytes of hashInput are reserved for the block counter. */
        (void)memcpy(hashInput, pool, ENTROPY_POOL_BYTES);

        /* Derive output blocks: SHA-256(pool || counter32). */
        size_t   offset  = 0U;
        uint32_t counter = 0U;
        bool     ok      = true;

        while ((offset < len) && ok) {
            /* Encode counter big-endian into the last four bytes. */
            hashInput[CTR_BYTE0_OFFSET] = static_cast<uint8_t>(counter >> 24U);
            hashInput[CTR_BYTE1_OFFSET] = static_cast<uint8_t>(counter >> 16U);
            hashInput[CTR_BYTE2_OFFSET] = static_cast<uint8_t>(counter >> 8U);
            hashInput[CTR_BYTE3_OFFSET] = static_cast<uint8_t>(counter);

            uint8_t digest[ENTROPY_SHA256_OUT] = { 0U };
            int     ret = mbedtls_sha256(hashInput, sizeof(hashInput),
                                         digest, MBEDTLS_SHA256_MODE);

            if (ret != MBEDTLS_OK) {
                ok = false;
            } else {
                size_t remaining = len - offset;
                size_t chunk     = 0U;

                if (remaining < ENTROPY_SHA256_OUT) {
                    chunk = remaining;
                } else {
                    chunk = ENTROPY_SHA256_OUT;
                }

                (void)memcpy(dest + offset, digest, chunk);
                offset  += chunk;
                counter += 1U;
            }
        }

        result = ok;
    }

    return result;
}
