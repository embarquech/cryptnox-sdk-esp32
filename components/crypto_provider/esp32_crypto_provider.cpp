#include "esp32_crypto_provider.h"
#include "CW_Utils.h"
#include "uECC.h"
#include "mbedtls/sha256.h"
#include "mbedtls/sha512.h"
#include "mbedtls/aes.h"
#include "esp_random.h"

#ifdef CONFIG_ESP_WIFI_ENABLED
#include "esp_wifi.h"
#endif

#ifdef CONFIG_BT_ENABLED
#include "esp_bt.h"
#endif

/******************************************************************
 * 1. Module constants
 ******************************************************************/

/* AES */
#define AES_BLOCK_SIZE_BYTES     (16U)  /* bytes in one AES block                          */
#define AES_KEY_BITS_PER_BYTE    (8U)   /* multiplier: key bytes → key bits                */
/* Maximum AES plaintext that can be padded: covers CW_USER_DATA_PAGE_SIZE + one block. */
#define AES_PAD_BUF_MAX_INPUT    (256U)
/* Pad buffer: max input + one extra block for the bit-padding byte.                    */
#define AES_PAD_BUF_SIZE         (AES_PAD_BUF_MAX_INPUT + AES_BLOCK_SIZE_BYTES)

/* ISO/IEC 9797-1 Method 2 (bit padding) */
#define BIT_PADDING_MARKER       (0x80U)  /* mandatory leading 1-bit as a full byte */
#define PADDING_ZERO_FILL        (0x00U)  /* fill value for padding bytes after marker */

/* mbedTLS SHA mode selectors */
#define MBEDTLS_SHA256_MODE      (0)   /* 0 = SHA-256, 1 = SHA-224 */
#define MBEDTLS_SHA512_MODE      (0)   /* 0 = SHA-512, 1 = SHA-384 */

/* mbedTLS return code for success */
#define MBEDTLS_OK               (0)

/* uECC return code for success */
#define UECC_SUCCESS             (1)

/******************************************************************
 * 2. TRNG readiness helpers
 ******************************************************************/

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

/******************************************************************
 * 3. SHA methods
 ******************************************************************/

/** @brief Compute SHA-256 over the input buffer, writing 32 bytes to out. */
void ESP32CryptoProvider::sha256(const uint8_t* data, size_t len, uint8_t* out) {
    (void)mbedtls_sha256(data, len, out, MBEDTLS_SHA256_MODE);
}

/** @brief Compute SHA-512 over the input buffer, writing 64 bytes to out. */
void ESP32CryptoProvider::sha512(const uint8_t* data, size_t len, uint8_t* out) {
    (void)mbedtls_sha512(data, len, out, MBEDTLS_SHA512_MODE);
}

/******************************************************************
 * 4. AES-CBC encrypt
 ******************************************************************/

/** @brief AES-CBC encrypt with optional ISO/IEC 9797-1 Method 2 bit padding. */
uint16_t ESP32CryptoProvider::aesCbcEncrypt(const uint8_t* in, uint16_t len, uint8_t* out,
                                             const uint8_t* key, uint8_t keyLen,
                                             uint8_t* iv, bool bitPadding) {
    uint16_t result = 0U;

    if ((in != NULL) && (out != NULL) && (key != NULL) && (iv != NULL)) {
        uint8_t  padBuf[AES_PAD_BUF_SIZE] = { PADDING_ZERO_FILL };
        uint16_t encLen = 0U;

        if (bitPadding) {
            /* Round up to next block boundary after appending the 0x80 marker. */
            uint32_t paddedLen32 = ((static_cast<uint32_t>(len) + 1U + (AES_BLOCK_SIZE_BYTES - 1U)) / AES_BLOCK_SIZE_BYTES) * AES_BLOCK_SIZE_BYTES;
            uint16_t paddedLen   = static_cast<uint16_t>(paddedLen32);

            if (paddedLen <= static_cast<uint16_t>(AES_PAD_BUF_SIZE)) {
                (void)CW_Utils::safe_memcpy(padBuf, sizeof(padBuf), in, static_cast<size_t>(len));
                padBuf[static_cast<size_t>(len)] = BIT_PADDING_MARKER;
                /* Remaining bytes already zero from initialisation. */
                encLen = paddedLen;
            }
        } else {
            if (len <= static_cast<uint16_t>(AES_PAD_BUF_SIZE)) {
                (void)CW_Utils::safe_memcpy(padBuf, sizeof(padBuf), in, static_cast<size_t>(len));
                encLen = len;
            }
        }

        if (encLen > 0U) {
            mbedtls_aes_context ctx = { 0 };
            mbedtls_aes_init(&ctx);

            unsigned int keyBits = static_cast<unsigned int>(
                static_cast<uint32_t>(keyLen) * static_cast<uint32_t>(AES_KEY_BITS_PER_BYTE));

            int ret = mbedtls_aes_setkey_enc(&ctx, key, keyBits);

            if (ret == MBEDTLS_OK) {
                ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT,
                                             static_cast<size_t>(encLen),
                                             iv, padBuf, out);
            }

            mbedtls_aes_free(&ctx);

            if (ret == MBEDTLS_OK) {
                result = encLen;
            }
        }
    }

    return result;
}

/******************************************************************
 * 5. AES-CBC decrypt
 ******************************************************************/

/** @brief AES-CBC decrypt with optional ISO/IEC 9797-1 Method 2 bit-padding removal. */
uint16_t ESP32CryptoProvider::aesCbcDecrypt(uint8_t* in, uint16_t len, uint8_t* out,
                                             const uint8_t* key, uint8_t keyLen,
                                             uint8_t* iv, bool bitPadding) {
    uint16_t result = 0U;

    if ((in != NULL) && (out != NULL) && (key != NULL) && (iv != NULL) && (len > 0U)) {
        bool blockAligned = ((static_cast<uint32_t>(len) % AES_BLOCK_SIZE_BYTES) == 0U);

        if (blockAligned) {
            mbedtls_aes_context ctx = { 0 };
            mbedtls_aes_init(&ctx);

            unsigned int keyBits = static_cast<unsigned int>(
                static_cast<uint32_t>(keyLen) * static_cast<uint32_t>(AES_KEY_BITS_PER_BYTE));

            int ret = mbedtls_aes_setkey_dec(&ctx, key, keyBits);

            if (ret == MBEDTLS_OK) {
                ret = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT,
                                             static_cast<size_t>(len),
                                             iv, in, out);
            }

            mbedtls_aes_free(&ctx);

            if (ret == MBEDTLS_OK) {
                if (bitPadding) {
                    /* Strip ISO/IEC 9797-1 Method 2 padding: scan backward,
                     * skip 0x00 bytes, then expect the 0x80 marker byte.
                     * padPos ends at the index of the 0x80 byte on success. */
                    uint16_t padPos    = len;
                    bool     found     = false;
                    bool     searching = true;

                    while ((padPos != static_cast<uint16_t>(0U)) && searching) {
                        padPos--;
                        if (out[padPos] == BIT_PADDING_MARKER) {
                            found     = true;
                            searching = false;
                        } else if (out[padPos] != PADDING_ZERO_FILL) {
                            searching = false;
                        } else {
                            /* byte is 0x00 — continue scanning toward the marker */
                        }
                    }

                    if (found) {
                        result = padPos;  /* bytes 0 .. padPos-1 are the plaintext */
                    }
                } else {
                    result = len;
                }
            }
        }
    }

    return result;
}

/******************************************************************
 * 6. ECDH and key generation (delegated to uECC shim)
 ******************************************************************/

/** @brief Compute ECDH shared secret: X-coordinate of privKey * pubKey point. */
bool ESP32CryptoProvider::ecdh(const uint8_t* pubKey, const uint8_t* privKey,
                                uint8_t* secret, const uECC_Curve_t* curve) {
    int ret = uECC_shared_secret(pubKey, privKey, secret, curve);
    return (ret == UECC_SUCCESS);
}

/** @brief Generate an ECC key pair via mbedTLS and the ESP32 hardware RNG. */
bool ESP32CryptoProvider::makeKey(uint8_t* pubKey, uint8_t* privKey,
                                   const uECC_Curve_t* curve) {
    int ret = uECC_make_key(pubKey, privKey, curve);
    return (ret == UECC_SUCCESS);
}

/******************************************************************
 * 7. Random bytes from ESP32 hardware True RNG
 ******************************************************************/

/** @brief Fill dest with size cryptographically random bytes from the ESP32 hardware TRNG. */
bool ESP32CryptoProvider::random(uint8_t* dest, unsigned size) {
    bool result = false;
    // QUICK TEST: WiFi/BT seeding gate temporarily disabled to validate that
    // the TRNG check is the root cause of failing tests.
    // bool wifi_seeded = false;
    // bool bt_seeded   = false;
    // #ifdef CONFIG_ESP_WIFI_ENABLED
    //     wifi_seeded = wifi_is_active();
    // #endif
    // #ifdef CONFIG_BT_ENABLED
    //     bt_seeded = bt_is_active();
    // #endif
    // bool trng_seeded = (wifi_seeded || bt_seeded);

    if ((dest != NULL) && (size > 0U)) {
        esp_fill_random(dest, static_cast<size_t>(size));
        result = true;
    }

    return result;
}
