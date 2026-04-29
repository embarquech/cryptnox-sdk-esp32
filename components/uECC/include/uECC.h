#ifndef UECC_H
#define UECC_H

/*
 * uECC compatibility shim for ESP32.
 * Exposes the micro-ecc API surface required by cryptnox-sdk-cpp,
 * implemented internally via mbedTLS and the ESP32 hardware RNG.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Opaque curve descriptor — definition lives in uECC_esp32.cpp. */
typedef struct uECC_Curve_t uECC_Curve_t;

/* Return curve descriptors for the two Cryptnox curves. */
const uECC_Curve_t* uECC_secp256r1(void);
const uECC_Curve_t* uECC_secp256k1(void);

/*
 * RNG callback type (micro-ecc compatible).
 * Returns 1 on success, 0 on failure.
 */
typedef int (*uECC_RNG_Function)(uint8_t *dest, unsigned size);

/*
 * Register an external RNG callback.
 * On ESP32 this is a no-op — the hardware RNG is used via mbedTLS directly.
 */
void uECC_set_rng(uECC_RNG_Function rng_function);

/*
 * Generate an EC key pair.
 *   public_key  [out] 64 bytes: X || Y (uncompressed, no 0x04 prefix).
 *   private_key [out] 32 bytes.
 * Returns 1 on success, 0 on failure.
 */
int uECC_make_key(uint8_t *public_key, uint8_t *private_key,
                  const uECC_Curve_t *curve);

/*
 * Compute an ECDH shared secret.
 *   public_key  [in]  64-byte remote public key (X || Y, no 0x04 prefix).
 *   private_key [in]  32-byte local private key.
 *   secret      [out] 32-byte shared secret (X coordinate of d*Q).
 * Returns 1 on success, 0 on failure.
 */
int uECC_shared_secret(const uint8_t *public_key, const uint8_t *private_key,
                       uint8_t *secret, const uECC_Curve_t *curve);

/*
 * Verify an ECDSA signature.
 *   public_key [in] 64-byte public key (X || Y, no 0x04 prefix).
 *   hash       [in] Message hash bytes.
 *   hash_size  [in] Hash length in bytes (32 for SHA-256).
 *   signature  [in] 64-byte raw signature: r[32] || s[32].
 * Returns 1 if valid, 0 if invalid.
 */
int uECC_verify(const uint8_t *public_key, const uint8_t *hash, unsigned hash_size,
                const uint8_t *signature, const uECC_Curve_t *curve);

#ifdef __cplusplus
}
#endif

#endif /* UECC_H */
