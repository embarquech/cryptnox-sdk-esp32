#include "uECC.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdsa.h"
#include "esp_random.h"
#include <algorithm>

#ifdef CONFIG_ESP_WIFI_ENABLED
#include "esp_wifi.h"
#endif

#ifdef CONFIG_BT_ENABLED
#include "esp_bt.h"
#endif

/******************************************************************
 * 1. Module constants
 ******************************************************************/

#define COORD_SIZE_BYTES       (32U)  /* bytes per coordinate (256-bit curve) */
#define UNCOMPRESSED_PUB_SIZE  (65U)  /* 0x04 || X[32] || Y[32]              */
#define UNCOMPRESSED_PREFIX    (0x04U)
#define POINT_PREFIX_OFFSET    (0U)   /* byte offset of the 0x04 prefix      */
#define COORD_X_OFFSET         (1U)   /* byte offset of X in 65-byte key     */
#define RAW_SIG_R_OFFSET       (0U)   /* byte offset of r in 64-byte raw sig */
#define RAW_SIG_S_OFFSET       (32U)  /* byte offset of s in 64-byte raw sig */
#define UECC_SUCCESS           (1)
#define UECC_FAILURE           (0)
#define MBEDTLS_OK             (0)
#define RNG_ERROR              (-1)  /* returned by RNG callback on invalid arguments */

/******************************************************************
 * 2. uECC_Curve_t definition (opaque in uECC.h)
 ******************************************************************/

struct uECC_Curve_t {
    mbedtls_ecp_group_id grp_id;
};

/******************************************************************
 * 3. Static curve instances
 ******************************************************************/

static const uECC_Curve_t s_secp256r1 = { MBEDTLS_ECP_DP_SECP256R1 };
static const uECC_Curve_t s_secp256k1 = { MBEDTLS_ECP_DP_SECP256K1 };

/******************************************************************
 * 4. TRNG readiness helpers
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
 * 5. Internal RNG adapter for mbedTLS
 ******************************************************************/

static int esp32_mbedtls_rng(void *ctx, unsigned char *output, size_t len) {
    int  result      = RNG_ERROR;
    (void)ctx;
    bool wifi_seeded = false;
    bool bt_seeded   = false;
#ifdef CONFIG_ESP_WIFI_ENABLED
    wifi_seeded = wifi_is_active();
#endif
#ifdef CONFIG_BT_ENABLED
    bt_seeded = bt_is_active();
#endif
    // cppcheck-suppress knownConditionTrueFalse
    bool trng_seeded = (wifi_seeded || bt_seeded);

    // cppcheck-suppress knownConditionTrueFalse
    if ((output != NULL) && (len > 0U) && trng_seeded) {
        esp_fill_random(output, len);
        result = MBEDTLS_OK;
    }

    return result;
}

/******************************************************************
 * 6. Public API
 ******************************************************************/

/** @brief Return the static secp256r1 curve descriptor. */
const uECC_Curve_t* uECC_secp256r1(void) {
    return &s_secp256r1;
}

/** @brief Return the static secp256k1 curve descriptor. */
const uECC_Curve_t* uECC_secp256k1(void) {
    return &s_secp256k1;
}

/** @brief No-op: ESP32 hardware RNG is used internally; no external callback needed. */
void uECC_set_rng(uECC_RNG_Function rng_function) {
    /* ESP32 hardware RNG is used directly through mbedTLS — no external
     * callback is needed.  Accept the pointer to satisfy the API contract. */
    (void)rng_function;
}

/** @brief Generate an ECC key pair using mbedTLS and the ESP32 hardware RNG. */
int uECC_make_key(uint8_t *public_key, uint8_t *private_key,
                  const uECC_Curve_t *curve) {
    int result = UECC_FAILURE;

    if ((public_key != NULL) && (private_key != NULL) && (curve != NULL)) {
        mbedtls_ecp_group grp = {};
        mbedtls_mpi       d   = {};
        mbedtls_ecp_point Q   = {};

        mbedtls_ecp_group_init(&grp);
        mbedtls_mpi_init(&d);
        mbedtls_ecp_point_init(&Q);

        int ret = mbedtls_ecp_group_load(&grp, curve->grp_id);

        if (ret == MBEDTLS_OK) {
            ret = mbedtls_ecp_gen_keypair(&grp, &d, &Q,
                                          esp32_mbedtls_rng, NULL);
        }

        if (ret == MBEDTLS_OK) {
            ret = mbedtls_mpi_write_binary(&d, private_key,
                                           static_cast<size_t>(COORD_SIZE_BYTES));
        }

        if (ret == MBEDTLS_OK) {
            uint8_t pub65[UNCOMPRESSED_PUB_SIZE] = { 0U };
            size_t  olen = 0U;
            ret = mbedtls_ecp_point_write_binary(&grp, &Q,
                                                  MBEDTLS_ECP_PF_UNCOMPRESSED,
                                                  &olen,
                                                  pub65, sizeof(pub65));
            if (ret == MBEDTLS_OK) {
                (void)std::copy_n(pub65 + COORD_X_OFFSET,
                                  static_cast<size_t>(COORD_SIZE_BYTES * 2U),
                                  public_key);
            }
        }

        if (ret == MBEDTLS_OK) {
            result = UECC_SUCCESS;
        }

        mbedtls_ecp_point_free(&Q);
        mbedtls_mpi_free(&d);
        mbedtls_ecp_group_free(&grp);
    }

    return result;
}

/** @brief Compute ECDH shared secret (X-coordinate of privKey * pubKey). */
int uECC_shared_secret(const uint8_t *public_key, const uint8_t *private_key,
                       uint8_t *secret, const uECC_Curve_t *curve) {
    int result = UECC_FAILURE;

    if ((public_key != NULL) && (private_key != NULL) &&
        (secret    != NULL) && (curve       != NULL)) {
        mbedtls_ecp_group grp      = {};
        mbedtls_ecp_point remote_Q = {};
        mbedtls_mpi       local_d  = {};
        mbedtls_ecp_point shared_R = {};

        mbedtls_ecp_group_init(&grp);
        mbedtls_ecp_point_init(&remote_Q);
        mbedtls_mpi_init(&local_d);
        mbedtls_ecp_point_init(&shared_R);

        int ret = mbedtls_ecp_group_load(&grp, curve->grp_id);

        if (ret == MBEDTLS_OK) {
            uint8_t pub65[UNCOMPRESSED_PUB_SIZE] = { 0U };
            pub65[POINT_PREFIX_OFFSET] = UNCOMPRESSED_PREFIX;
            (void)std::copy_n(public_key,
                              static_cast<size_t>(COORD_SIZE_BYTES * 2U),
                              pub65 + COORD_X_OFFSET);
            ret = mbedtls_ecp_point_read_binary(&grp, &remote_Q,
                                                 pub65, sizeof(pub65));
        }

        if (ret == MBEDTLS_OK) {
            ret = mbedtls_mpi_read_binary(&local_d, private_key,
                                          static_cast<size_t>(COORD_SIZE_BYTES));
        }

        if (ret == MBEDTLS_OK) {
            ret = mbedtls_ecp_mul(&grp, &shared_R, &local_d, &remote_Q,
                                  esp32_mbedtls_rng, NULL);
        }

        if (ret == MBEDTLS_OK) {
            uint8_t shared65[UNCOMPRESSED_PUB_SIZE] = { 0U };
            size_t  olen = 0U;
            ret = mbedtls_ecp_point_write_binary(&grp, &shared_R,
                                                  MBEDTLS_ECP_PF_UNCOMPRESSED,
                                                  &olen,
                                                  shared65, sizeof(shared65));
            if (ret == MBEDTLS_OK) {
                (void)std::copy_n(shared65 + COORD_X_OFFSET,
                                  static_cast<size_t>(COORD_SIZE_BYTES),
                                  secret);
            }
        }

        if (ret == MBEDTLS_OK) {
            result = UECC_SUCCESS;
        }

        mbedtls_ecp_point_free(&shared_R);
        mbedtls_mpi_free(&local_d);
        mbedtls_ecp_point_free(&remote_Q);
        mbedtls_ecp_group_free(&grp);
    }

    return result;
}

/** @brief Verify an ECDSA signature (raw 64-byte r||s) against a hash. */
int uECC_verify(const uint8_t *public_key, const uint8_t *hash, unsigned hash_size,
                const uint8_t *signature, const uECC_Curve_t *curve) {
    int result = UECC_FAILURE;

    if ((public_key != NULL) && (hash != NULL) &&
        (signature  != NULL) && (curve != NULL)) {
        mbedtls_ecp_group grp = {};
        mbedtls_ecp_point Q   = {};
        mbedtls_mpi       r   = {};
        mbedtls_mpi       s   = {};

        mbedtls_ecp_group_init(&grp);
        mbedtls_ecp_point_init(&Q);
        mbedtls_mpi_init(&r);
        mbedtls_mpi_init(&s);

        int ret = mbedtls_ecp_group_load(&grp, curve->grp_id);

        if (ret == MBEDTLS_OK) {
            uint8_t pub65[UNCOMPRESSED_PUB_SIZE] = { 0U };
            pub65[POINT_PREFIX_OFFSET] = UNCOMPRESSED_PREFIX;
            (void)std::copy_n(public_key,
                              static_cast<size_t>(COORD_SIZE_BYTES * 2U),
                              pub65 + COORD_X_OFFSET);
            ret = mbedtls_ecp_point_read_binary(&grp, &Q,
                                                 pub65, sizeof(pub65));
        }

        if (ret == MBEDTLS_OK) {
            ret = mbedtls_mpi_read_binary(&r,
                                          signature + RAW_SIG_R_OFFSET,
                                          static_cast<size_t>(COORD_SIZE_BYTES));
        }

        if (ret == MBEDTLS_OK) {
            ret = mbedtls_mpi_read_binary(&s,
                                          signature + RAW_SIG_S_OFFSET,
                                          static_cast<size_t>(COORD_SIZE_BYTES));
        }

        if (ret == MBEDTLS_OK) {
            ret = mbedtls_ecdsa_verify(&grp, hash,
                                        static_cast<size_t>(hash_size),
                                        &Q, &r, &s);
        }

        if (ret == MBEDTLS_OK) {
            result = UECC_SUCCESS;
        }

        mbedtls_mpi_free(&s);
        mbedtls_mpi_free(&r);
        mbedtls_ecp_point_free(&Q);
        mbedtls_ecp_group_free(&grp);
    }

    return result;
}
