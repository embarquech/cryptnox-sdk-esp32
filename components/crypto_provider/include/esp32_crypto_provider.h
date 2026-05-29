/*
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Copyright (c) 2026 Cryptnox SA
 */

/**
 * @file esp32_crypto_provider.h
 * @brief @ref CW_CryptoProvider implementation for ESP32 using mbedTLS and the hardware TRNG.
 *
 * @ref ESP32CryptoProvider wires the platform-independent @ref CW_CryptoProvider
 * interface to the ESP-IDF cryptographic stack:
 *
 * | Operation           | Backend                                                    |
 * |---------------------|------------------------------------------------------------|
 * | SHA-256 / SHA-512   | mbedTLS (hardware-accelerated on ESP32-S3)                 |
 * | AES-CBC enc / dec   | mbedTLS (hardware-accelerated on ESP32-S3)                 |
 * | ECDH / key-gen      | uECC shim backed by mbedTLS ECP primitives                 |
 * | ECDSA verify        | mbedTLS ECP verify                                         |
 * | Random bytes        | @c esp_fill_random() — hardware TRNG (SEC-001)             |
 *
 * @warning The ESP32 hardware TRNG delivers full entropy only when Wi-Fi or
 *          Bluetooth is active.  When neither radio is running the TRNG falls
 *          back to thermal noise and ring-oscillator jitter, which provides
 *          reduced (but non-zero) entropy.  Enable Wi-Fi before performing
 *          key generation or signing operations in production firmware.
 *
 * @ingroup esp32_adapters
 */

#ifndef ESP32_CRYPTO_PROVIDER_H
#define ESP32_CRYPTO_PROVIDER_H

#include "CW_CryptoProvider.h"
#include "CW_Defs.h"

/**
 * @class ESP32CryptoProvider
 * @ingroup esp32_adapters
 * @brief @ref CW_CryptoProvider backed by mbedTLS and the ESP32 hardware TRNG.
 *
 * All operations are stateless; a single instance may be shared across
 * multiple @ref CryptnoxWallet objects (though concurrent use from different
 * RTOS tasks is not thread-safe without external locking).
 *
 * @warning See the @ref random method for TRNG entropy requirements (SEC-001).
 *
 * @par Example
 * @code
 * ESP32CryptoProvider crypto;
 * CW_CryptoProvider &provider = crypto;
 *
 * uint8_t pub[64], priv[32];
 * provider.makeKey(pub, priv, CW_CURVE_SECP256R1);   // generate ephemeral key pair
 *
 * uint8_t digest[32];
 * provider.sha256(message, messageLen, digest);       // hash a message
 * @endcode
 *
 * @see CW_CryptoProvider
 * @see CryptnoxWallet
 */
class ESP32CryptoProvider : public CW_CryptoProvider {
public:
    /**
     * @brief Compute SHA-256 over a contiguous buffer.
     *
     * Uses the mbedTLS @c mbedtls_sha256 API, which is hardware-accelerated on
     * ESP32-S3.
     *
     * @param[in]  data Pointer to the input data (must not be @c NULL).
     * @param[in]  len  Length of @p data in bytes.
     * @param[out] out  32-byte output buffer for the digest (must not be @c NULL).
     * @return @c true on success, @c false if @p data or @p out is @c NULL or
     *         if the mbedTLS call fails.
     */
    bool sha256(const uint8_t* data, size_t len, uint8_t* out) override;

    /**
     * @brief Compute SHA-512 over a contiguous buffer.
     *
     * Uses the mbedTLS @c mbedtls_sha512 API, which is hardware-accelerated on
     * ESP32-S3.
     *
     * @param[in]  data Pointer to the input data (must not be @c NULL).
     * @param[in]  len  Length of @p data in bytes.
     * @param[out] out  64-byte output buffer for the digest (must not be @c NULL).
     * @return @c true on success, @c false if @p data or @p out is @c NULL or
     *         if the mbedTLS call fails.
     */
    bool sha512(const uint8_t* data, size_t len, uint8_t* out) override;

    /**
     * @brief Encrypt a buffer with AES-CBC.
     *
     * Uses mbedTLS AES-CBC with optional ISO/IEC 7816-4 bit-padding.  The IV
     * is updated in-place after each block so that the caller can chain calls
     * for streaming encryption.
     *
     * @param[in]     in         Plaintext input buffer (must not be @c NULL).
     * @param[in]     len        Length of @p in in bytes.
     * @param[out]    out        Ciphertext output buffer; must be at least
     *                           @c len + 16 bytes to accommodate padding.
     * @param[in]     key        AES key bytes (must not be @c NULL).
     * @param[in]     keyLen     Key length in bytes (@c 16 for AES-128,
     *                           @c 24 for AES-192, @c 32 for AES-256).
     * @param[in,out] iv         16-byte IV; updated in-place after the call.
     * @param[in]     bitPadding When @c true, applies ISO/IEC 7816-4 bit
     *                           padding before encrypting.
     * @return Number of ciphertext bytes written to @p out, or @c 0 on failure.
     */
    uint16_t aesCbcEncrypt(const uint8_t* in, uint16_t len, uint8_t* out,
                           const uint8_t* key, uint8_t keyLen,
                           uint8_t* iv, bool bitPadding) override;

    /**
     * @brief Decrypt a buffer with AES-CBC.
     *
     * Uses mbedTLS AES-CBC and optionally strips ISO/IEC 7816-4 bit-padding
     * after decryption.  The IV is updated in-place.
     *
     * @param[in,out] in         Ciphertext input buffer; may be modified
     *                           in-place (must not be @c NULL).
     * @param[in]     len        Length of @p in in bytes (must be a multiple
     *                           of 16).
     * @param[out]    out        Plaintext output buffer (must not be @c NULL).
     * @param[in]     key        AES key bytes (must not be @c NULL).
     * @param[in]     keyLen     Key length in bytes (@c 16 / @c 24 / @c 32).
     * @param[in,out] iv         16-byte IV; updated in-place after the call.
     * @param[in]     bitPadding When @c true, strips ISO/IEC 7816-4 bit
     *                           padding from the decrypted output.
     * @return Number of plaintext bytes written to @p out, or @c 0 on failure.
     */
    uint16_t aesCbcDecrypt(uint8_t* in, uint16_t len, uint8_t* out,
                           const uint8_t* key, uint8_t keyLen,
                           uint8_t* iv, bool bitPadding) override;

    /**
     * @brief Compute an ECDH shared secret.
     *
     * Performs the standard Diffie-Hellman point multiplication
     * @c secret = privKey × pubKey on the specified curve via the uECC shim.
     *
     * @param[in]  pubKey  Uncompressed peer public key (64 bytes, X||Y;
     *                     no @c 0x04 prefix; must not be @c NULL).
     * @param[in]  privKey 32-byte private scalar (must not be @c NULL).
     * @param[out] secret  32-byte shared-secret output buffer (must not be @c NULL).
     * @param[in]  curve   Elliptic curve selector (@ref CW_CURVE_SECP256R1 or
     *                     @ref CW_CURVE_SECP256K1).
     * @return @c true on success, @c false if point multiplication fails or
     *         a pointer argument is @c NULL.
     */
    bool ecdh(const uint8_t* pubKey, const uint8_t* privKey,
              uint8_t* secret, CW_Curve curve) override;

    /**
     * @brief Generate an ephemeral EC key pair.
     *
     * Uses the hardware TRNG (via @ref random) as the entropy source for the
     * private scalar.
     *
     * @param[out] pubKey  64-byte uncompressed public key output (X||Y, no
     *                     @c 0x04 prefix; must not be @c NULL).
     * @param[out] privKey 32-byte private key output (must not be @c NULL).
     * @param[in]  curve   Elliptic curve selector (@ref CW_CURVE_SECP256R1 or
     *                     @ref CW_CURVE_SECP256K1).
     * @return @c true on success, @c false on RNG or key-generation failure.
     *
     * @warning Ensure Wi-Fi or BT is active before calling this to guarantee
     *          full TRNG entropy (SEC-001).
     */
    bool makeKey(uint8_t* pubKey, uint8_t* privKey,
                 CW_Curve curve) override;

    /**
     * @brief Fill a buffer with cryptographically random bytes.
     *
     * Calls @c esp_fill_random() which reads from the ESP32 hardware TRNG.
     * Full entropy requires Wi-Fi or Bluetooth to be active; without a live
     * radio the TRNG operates in reduced-entropy mode (thermal noise and
     * ring-oscillator jitter only — see SEC-001).
     *
     * @param[out] dest Buffer to fill (must not be @c NULL).
     * @param[in]  size Number of random bytes to generate.
     * @return @c true on success, @c false if @p dest is @c NULL or
     *         @p size is @c 0.
     *
     * @warning In production firmware, call this only after Wi-Fi or BT has
     *          been started to ensure full entropy.
     */
    bool random(uint8_t* dest, unsigned size) override;

    /**
     * @brief Verify an ECDSA signature.
     *
     * Checks that @p sig is a valid low-S DER-encoded ECDSA signature over
     * @p hash produced by the private key corresponding to @p pubKey64.
     *
     * @param[in] pubKey64 64-byte uncompressed public key (X||Y, no @c 0x04
     *                     prefix; must not be @c NULL).
     * @param[in] hash     Message digest to verify against (must not be @c NULL).
     * @param[in] hashLen  Length of @p hash in bytes.
     * @param[in] sig      DER-encoded signature bytes (must not be @c NULL).
     * @param[in] curve    Elliptic curve selector (@ref CW_CURVE_SECP256R1 or
     *                     @ref CW_CURVE_SECP256K1).
     * @return @c true if the signature is valid, @c false otherwise (including
     *         on NULL arguments or malformed DER).
     */
    bool ecdsaVerify(const uint8_t* pubKey64, const uint8_t* hash,
                     size_t hashLen, const uint8_t* sig,
                     CW_Curve curve) override;

    /** @brief Default destructor. */
    ~ESP32CryptoProvider() override {}
};

#endif /* ESP32_CRYPTO_PROVIDER_H */
