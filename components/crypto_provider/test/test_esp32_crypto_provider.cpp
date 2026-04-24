#include "unity.h"
#include "esp32_crypto_provider.h"
#include "uECC.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <stdio.h>

/******************************************************************
 * 1. Sizes used across all test cases
 ******************************************************************/

#define TV_SHA256_OUT_BYTES    (32U)
#define TV_SHA512_OUT_BYTES    (64U)
#define TV_AES_KEY_BYTES       (16U)
#define TV_AES_IV_BYTES        (16U)
#define TV_AES_BLOCK_BYTES     (16U)
#define TV_EC_COORD_BYTES      (32U)
#define TV_EC_PUBKEY_BYTES     (64U)
#define TV_RANDOM_BYTES        (32U)
#define TV_BIT_PAD_INPUT_BYTES  (3U)  /* plaintext shorter than one AES block */

/******************************************************************
 * 2. Static provider instance (default-constructed, no heap)
 ******************************************************************/

static ESP32CryptoProvider s_provider;

/******************************************************************
 * 3. SHA-256 — NIST FIPS 180-4, message "abc"
 ******************************************************************/

TEST_CASE("sha256 NIST abc vector", "[crypto_provider]")
{
    static const uint8_t input[] = { 'a', 'b', 'c' };
    static const uint8_t expected[TV_SHA256_OUT_BYTES] = {
        0xbaU, 0x78U, 0x16U, 0xbfU, 0x8fU, 0x01U, 0xcfU, 0xeaU,
        0x41U, 0x41U, 0x40U, 0xdeU, 0x5dU, 0xaeU, 0x2eU, 0xc7U,
        0x3bU, 0x00U, 0x36U, 0x1bU, 0xbeU, 0xf2U, 0x48U, 0xdeU,
        0x69U, 0x30U, 0x35U, 0x9eU, 0x6cU, 0xbdU, 0x3eU, 0x7eU
    };
    uint8_t out[TV_SHA256_OUT_BYTES] = { 0U };

    s_provider.sha256(input, sizeof(input), out);

    printf("[sha256 via provider] ");
    for (size_t i = 0U; i < TV_SHA256_OUT_BYTES; i++) {
        printf("%02x", static_cast<unsigned int>(out[i]));
    }
    printf("\n");

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, TV_SHA256_OUT_BYTES);
}

/******************************************************************
 * 3a. SHA-256 — direct one-shot call (no virtual dispatch)
 ******************************************************************/

TEST_CASE("sha256 direct one-shot call", "[crypto_provider]")
{
    static const uint8_t input[] = { 'a', 'b', 'c' };
    static const uint8_t expected[TV_SHA256_OUT_BYTES] = {
        0xbaU, 0x78U, 0x16U, 0xbfU, 0x8fU, 0x01U, 0xcfU, 0xeaU,
        0x41U, 0x41U, 0x40U, 0xdeU, 0x5dU, 0xaeU, 0x2eU, 0xc7U,
        0x3bU, 0x00U, 0x36U, 0x1bU, 0xbeU, 0xf2U, 0x48U, 0xdeU,
        0x69U, 0x30U, 0x35U, 0x9eU, 0x6cU, 0xbdU, 0x3eU, 0x7eU
    };
    uint8_t out[TV_SHA256_OUT_BYTES] = { 0U };
    int ret = 0;

    ret = mbedtls_sha256(input, sizeof(input), out, 0);

    printf("[sha256 direct one-shot] ");
    for (size_t i = 0U; i < TV_SHA256_OUT_BYTES; i++) {
        printf("%02x", static_cast<unsigned int>(out[i]));
    }
    printf("\n");

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, TV_SHA256_OUT_BYTES);
}

/******************************************************************
 * 3b. SHA-256 — direct mbedTLS context API (no virtual dispatch)
 ******************************************************************/

TEST_CASE("sha256 direct mbedtls call", "[crypto_provider]")
{
    static const uint8_t input[] = { 'a', 'b', 'c' };
    static const uint8_t expected[TV_SHA256_OUT_BYTES] = {
        0xbaU, 0x78U, 0x16U, 0xbfU, 0x8fU, 0x01U, 0xcfU, 0xeaU,
        0x41U, 0x41U, 0x40U, 0xdeU, 0x5dU, 0xaeU, 0x2eU, 0xc7U,
        0x3bU, 0x00U, 0x36U, 0x1bU, 0xbeU, 0xf2U, 0x48U, 0xdeU,
        0x69U, 0x30U, 0x35U, 0x9eU, 0x6cU, 0xbdU, 0x3eU, 0x7eU
    };
    uint8_t out[TV_SHA256_OUT_BYTES] = { 0U };
    int ret = 0;

    mbedtls_sha256_context ctx = {};
    mbedtls_sha256_init(&ctx);
    ret = mbedtls_sha256_starts(&ctx, 0);
    if (ret == 0) {
        ret = mbedtls_sha256_update(&ctx, input, sizeof(input));
    }
    if (ret == 0) {
        ret = mbedtls_sha256_finish(&ctx, out);
    }
    mbedtls_sha256_free(&ctx);

    printf("[sha256 direct ctx]   ");
    for (size_t i = 0U; i < TV_SHA256_OUT_BYTES; i++) {
        printf("%02x", static_cast<unsigned int>(out[i]));
    }
    printf("\n");

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, TV_SHA256_OUT_BYTES);
}

/******************************************************************
 * 4. SHA-512 — NIST FIPS 180-4, message "abc"
 ******************************************************************/

TEST_CASE("sha512 NIST abc vector", "[crypto_provider]")
{
    static const uint8_t input[] = { 'a', 'b', 'c' };
    static const uint8_t expected[TV_SHA512_OUT_BYTES] = {
        0xddU, 0xafU, 0x35U, 0xa1U, 0x93U, 0x61U, 0x7aU, 0xbaU,
        0xccU, 0x41U, 0x73U, 0x49U, 0xaeU, 0x20U, 0x41U, 0x31U,
        0x12U, 0xe6U, 0xfaU, 0x4eU, 0x89U, 0xa9U, 0x7eU, 0xa2U,
        0x0aU, 0x9eU, 0xeeU, 0xe6U, 0x4bU, 0x55U, 0xd3U, 0x9aU,
        0x21U, 0x92U, 0x99U, 0x2aU, 0x27U, 0x4fU, 0xc1U, 0xa8U,
        0x36U, 0xbaU, 0x3cU, 0x23U, 0xa3U, 0xfeU, 0xebU, 0xbdU,
        0x45U, 0x4dU, 0x44U, 0x23U, 0x64U, 0x3cU, 0xe8U, 0x0eU,
        0x2aU, 0x9aU, 0xc9U, 0x4fU, 0xa5U, 0x4cU, 0xa4U, 0x9fU
    };
    uint8_t out[TV_SHA512_OUT_BYTES] = { 0U };

    s_provider.sha512(input, sizeof(input), out);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, TV_SHA512_OUT_BYTES);
}

/******************************************************************
 * 5. AES-128-CBC encrypt — NIST SP 800-38A F.2.1, one block
 ******************************************************************/

TEST_CASE("aesCbcEncrypt NIST SP800-38A F.2.1 one block", "[crypto_provider]")
{
    static const uint8_t key[TV_AES_KEY_BYTES] = {
        0x2bU, 0x7eU, 0x15U, 0x16U, 0x28U, 0xaeU, 0xd2U, 0xa6U,
        0xabU, 0xf7U, 0x15U, 0x88U, 0x09U, 0xcfU, 0x4fU, 0x3cU
    };
    static const uint8_t plaintext[TV_AES_BLOCK_BYTES] = {
        0x6bU, 0xc1U, 0xbeU, 0xe2U, 0x2eU, 0x40U, 0x9fU, 0x96U,
        0xe9U, 0x3dU, 0x7eU, 0x11U, 0x73U, 0x93U, 0x17U, 0x2aU
    };
    static const uint8_t expected[TV_AES_BLOCK_BYTES] = {
        0x76U, 0x49U, 0xabU, 0xacU, 0x81U, 0x19U, 0xb2U, 0x46U,
        0xceU, 0xe9U, 0x8eU, 0x9bU, 0x12U, 0xe9U, 0x19U, 0x7dU
    };
    uint8_t iv[TV_AES_IV_BYTES] = {
        0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU, 0x0eU, 0x0fU
    };
    uint8_t  out[TV_AES_BLOCK_BYTES] = { 0U };

    uint16_t encLen = s_provider.aesCbcEncrypt(
        plaintext,
        static_cast<uint16_t>(sizeof(plaintext)),
        out,
        key,
        static_cast<uint8_t>(sizeof(key)),
        iv,
        false);

    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(TV_AES_BLOCK_BYTES), encLen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, TV_AES_BLOCK_BYTES);
}

/******************************************************************
 * 6. AES-128-CBC decrypt — NIST SP 800-38A F.2.2, one block
 ******************************************************************/

TEST_CASE("aesCbcDecrypt NIST SP800-38A F.2.2 one block", "[crypto_provider]")
{
    static const uint8_t key[TV_AES_KEY_BYTES] = {
        0x2bU, 0x7eU, 0x15U, 0x16U, 0x28U, 0xaeU, 0xd2U, 0xa6U,
        0xabU, 0xf7U, 0x15U, 0x88U, 0x09U, 0xcfU, 0x4fU, 0x3cU
    };
    uint8_t ciphertext[TV_AES_BLOCK_BYTES] = {
        0x76U, 0x49U, 0xabU, 0xacU, 0x81U, 0x19U, 0xb2U, 0x46U,
        0xceU, 0xe9U, 0x8eU, 0x9bU, 0x12U, 0xe9U, 0x19U, 0x7dU
    };
    static const uint8_t expected[TV_AES_BLOCK_BYTES] = {
        0x6bU, 0xc1U, 0xbeU, 0xe2U, 0x2eU, 0x40U, 0x9fU, 0x96U,
        0xe9U, 0x3dU, 0x7eU, 0x11U, 0x73U, 0x93U, 0x17U, 0x2aU
    };
    uint8_t iv[TV_AES_IV_BYTES] = {
        0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU, 0x0eU, 0x0fU
    };
    uint8_t  out[TV_AES_BLOCK_BYTES] = { 0U };

    uint16_t decLen = s_provider.aesCbcDecrypt(
        ciphertext,
        static_cast<uint16_t>(sizeof(ciphertext)),
        out,
        key,
        static_cast<uint8_t>(sizeof(key)),
        iv,
        false);

    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(TV_AES_BLOCK_BYTES), decLen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, TV_AES_BLOCK_BYTES);
}

/******************************************************************
 * 7. AES-128-CBC bit-padding round-trip
 *    Input shorter than one block → padded to 16 bytes on encrypt,
 *    stripped back to original length on decrypt.
 ******************************************************************/

TEST_CASE("aesCbc bit-padding round-trip", "[crypto_provider]")
{
    static const uint8_t key[TV_AES_KEY_BYTES] = {
        0x2bU, 0x7eU, 0x15U, 0x16U, 0x28U, 0xaeU, 0xd2U, 0xa6U,
        0xabU, 0xf7U, 0x15U, 0x88U, 0x09U, 0xcfU, 0x4fU, 0x3cU
    };
    static const uint8_t iv_init[TV_AES_IV_BYTES] = {
        0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU, 0x0eU, 0x0fU
    };
    static const uint8_t plaintext[TV_BIT_PAD_INPUT_BYTES] = {
        0x01U, 0x02U, 0x03U
    };

    uint8_t iv_enc[TV_AES_IV_BYTES]        = { 0U };
    uint8_t iv_dec[TV_AES_IV_BYTES]        = { 0U };
    uint8_t ciphertext[TV_AES_BLOCK_BYTES] = { 0U };
    uint8_t recovered[TV_AES_BLOCK_BYTES]  = { 0U };

    memcpy(iv_enc, iv_init, TV_AES_IV_BYTES);
    memcpy(iv_dec, iv_init, TV_AES_IV_BYTES);

    uint16_t encLen = s_provider.aesCbcEncrypt(
        plaintext,
        static_cast<uint16_t>(sizeof(plaintext)),
        ciphertext,
        key,
        static_cast<uint8_t>(sizeof(key)),
        iv_enc,
        true);

    /* 3 bytes + 0x80 marker → padded to one full block */
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(TV_AES_BLOCK_BYTES), encLen);

    uint16_t decLen = s_provider.aesCbcDecrypt(
        ciphertext,
        encLen,
        recovered,
        key,
        static_cast<uint8_t>(sizeof(key)),
        iv_dec,
        true);

    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(TV_BIT_PAD_INPUT_BYTES), decLen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(plaintext, recovered, TV_BIT_PAD_INPUT_BYTES);
}

/******************************************************************
 * 8. ECDH — two-party shared secret symmetry on secp256r1
 ******************************************************************/

TEST_CASE("ecdh shared secret symmetry secp256r1", "[crypto_provider]")
{
    uint8_t pubA[TV_EC_PUBKEY_BYTES]   = { 0U };
    uint8_t privA[TV_EC_COORD_BYTES]   = { 0U };
    uint8_t pubB[TV_EC_PUBKEY_BYTES]   = { 0U };
    uint8_t privB[TV_EC_COORD_BYTES]   = { 0U };
    uint8_t secretA[TV_EC_COORD_BYTES] = { 0U };
    uint8_t secretB[TV_EC_COORD_BYTES] = { 0U };

    const uECC_Curve_t* curve = uECC_secp256r1();

    bool okA = s_provider.makeKey(pubA, privA, curve);
    bool okB = s_provider.makeKey(pubB, privB, curve);

    TEST_ASSERT_TRUE(okA);
    TEST_ASSERT_TRUE(okB);

    bool ecdhA = s_provider.ecdh(pubB, privA, secretA, curve);
    bool ecdhB = s_provider.ecdh(pubA, privB, secretB, curve);

    TEST_ASSERT_TRUE(ecdhA);
    TEST_ASSERT_TRUE(ecdhB);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(secretA, secretB, TV_EC_COORD_BYTES);
}

/******************************************************************
 * 9. random — returns true; two draws from the TRNG must differ
 ******************************************************************/

TEST_CASE("random returns true and produces distinct draws", "[crypto_provider]")
{
    uint8_t buf1[TV_RANDOM_BYTES] = { 0U };
    uint8_t buf2[TV_RANDOM_BYTES] = { 0U };

    bool ok1 = s_provider.random(buf1, TV_RANDOM_BYTES);
    bool ok2 = s_provider.random(buf2, TV_RANDOM_BYTES);

    TEST_ASSERT_TRUE(ok1);
    TEST_ASSERT_TRUE(ok2);
    /* P(collision of two independent 32-byte TRNG draws) < 2^-256. */
    TEST_ASSERT_NOT_EQUAL(0, memcmp(buf1, buf2, TV_RANDOM_BYTES));
}
