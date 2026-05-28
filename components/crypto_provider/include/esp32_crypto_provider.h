/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

#ifndef ESP32_CRYPTO_PROVIDER_H
#define ESP32_CRYPTO_PROVIDER_H

#include "CW_CryptoProvider.h"
#include "CW_Defs.h"

/**
 * @class ESP32CryptoProvider
 * @brief Concrete CW_CryptoProvider for ESP32 using mbedTLS and the hardware RNG.
 *
 * SHA-256 / SHA-512  — mbedTLS software (hardware-accelerated on ESP32-S3).
 * AES-CBC            — mbedTLS software (hardware-accelerated on ESP32-S3).
 * ECDH / key-gen     — uECC shim backed by mbedTLS ECP primitives.
 * Random             — ESP32 hardware TRNG via esp_fill_random(); requires Wi-Fi or BT active (SEC-001).
 */
class ESP32CryptoProvider : public CW_CryptoProvider {
public:
    bool sha256(const uint8_t* data, size_t len, uint8_t* out) override;

    bool sha512(const uint8_t* data, size_t len, uint8_t* out) override;

    uint16_t aesCbcEncrypt(const uint8_t* in, uint16_t len, uint8_t* out,
                           const uint8_t* key, uint8_t keyLen,
                           uint8_t* iv, bool bitPadding) override;

    uint16_t aesCbcDecrypt(uint8_t* in, uint16_t len, uint8_t* out,
                           const uint8_t* key, uint8_t keyLen,
                           uint8_t* iv, bool bitPadding) override;

    bool ecdh(const uint8_t* pubKey, const uint8_t* privKey,
              uint8_t* secret, CW_Curve curve) override;

    bool makeKey(uint8_t* pubKey, uint8_t* privKey,
                 CW_Curve curve) override;

    bool random(uint8_t* dest, unsigned size) override;

    bool ecdsaVerify(const uint8_t* pubKey64, const uint8_t* hash,
                     size_t hashLen, const uint8_t* sig,
                     CW_Curve curve) override;

    ~ESP32CryptoProvider() override {}
};

#endif /* ESP32_CRYPTO_PROVIDER_H */
