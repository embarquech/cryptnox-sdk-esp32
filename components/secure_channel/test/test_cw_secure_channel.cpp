#include "unity.h"
#include "CW_SecureChannel.h"
#include "CW_Defs.h"
#include "CW_Utils.h"
#include "esp32_crypto_provider.h"
#include "uECC.h"
#include "esp_random.h"
#include <string.h>
#include <stdio.h>

/******************************************************************
 * 1. Size / offset constants
 ******************************************************************/

#define SC_AES_BLOCK_BYTES          (16U)
#define SC_AES_KEY_BYTES            (32U)
#define SC_EC_COORD_BYTES           (32U)
#define SC_EC_PUBKEY_BYTES          (64U)

/* Certificate layout: 'C'[1] + nonce[8] + 0x04[1] + X[32] + Y[32] = 74 bytes */
#define SC_CERT_MARKER_BYTES         (1U)
#define SC_CERT_NONCE_BYTES          (8U)
#define SC_CERT_KEY65_BYTES         (65U)
#define SC_CERT_TOTAL_BYTES         (74U)
#define SC_CERT_KEY_OFFSET          (SC_CERT_MARKER_BYTES + SC_CERT_NONCE_BYTES)

/* APDU response sizes (data + 2-byte SW) */
#define SC_SW_BYTES                  (2U)
#define SC_SELECT_RESP_BYTES        (26U)
#define SC_GET_CERT_RESP_BYTES     (148U)
#define SC_OPEN_SC_RESP_BYTES       (34U)
#define SC_MUTUAL_AUTH_RESP_BYTES   (66U)
#define SC_SALT_BYTES               (32U)
#define SC_IV_BYTES                 (16U)
#define SC_SHA512_OUT_BYTES         (64U)

/* Secure messaging APDU layout (outgoing) */
#define SC_APDU_HEADER_LEN           (4U)
#define SC_APDU_LC_OFFSET            (4U)
#define SC_APDU_MAC_OFFSET           (5U)   /* header[4] + Lc[1] */
#define SC_APDU_MAC_BYTES           (16U)

/* Card response used in the AES-CBC round-trip test */
#define SC_CARD_RESP_PAYLOAD_BYTES   (4U)
#define SC_CARD_RESP_TOTAL_BYTES     (SC_CARD_RESP_PAYLOAD_BYTES + SC_SW_BYTES)

/* Mock scripting limits */
#define MOCK_MAX_SCRIPTS             (8U)
#define MOCK_MAX_RESP_BYTES        (255U)
#define MOCK_UART_BAUD_RATE    (115200UL)

/* Known P-256 public key (NIST FIPS 186-4 test vector) used as the
 * card's ephemeral key in mock-transport tests.
 * 64 bytes = X[32] || Y[32] (no 0x04 prefix). */
static const uint8_t K_CARD_EPHEMERAL_PUB[SC_EC_PUBKEY_BYTES] = {
    /* X */
    0x60U, 0xfeU, 0xd4U, 0xbaU, 0x25U, 0x5aU, 0x9dU, 0x31U,
    0xc9U, 0x61U, 0xebU, 0x74U, 0xc6U, 0x35U, 0x6dU, 0x68U,
    0xc0U, 0x49U, 0xb8U, 0x92U, 0x3bU, 0x61U, 0xfaU, 0x6cU,
    0xe6U, 0x69U, 0x62U, 0x2eU, 0x60U, 0xf2U, 0x9fU, 0xb6U,
    /* Y */
    0x79U, 0x03U, 0xfeU, 0x10U, 0x08U, 0xb8U, 0xbcU, 0x99U,
    0xa4U, 0x1aU, 0xe9U, 0xe9U, 0x56U, 0x28U, 0xbcU, 0x64U,
    0xf2U, 0xf1U, 0xb2U, 0x0cU, 0x2dU, 0x7eU, 0x9fU, 0x51U,
    0x77U, 0xa3U, 0xc2U, 0x94U, 0xd4U, 0x46U, 0x22U, 0x99U
};

/* Known Kenc / Kmac for the AES-CBC round-trip test (AES-256 key from
 * NIST SP 800-38A, extended to 32 bytes for the second key). */
static const uint8_t K_TEST_KENC[SC_AES_KEY_BYTES] = {
    0x60U, 0x3dU, 0xebU, 0x10U, 0x15U, 0xcaU, 0x71U, 0xbeU,
    0x2bU, 0x73U, 0xaeU, 0xf0U, 0x85U, 0x7dU, 0x77U, 0x81U,
    0x1fU, 0x35U, 0x2cU, 0x07U, 0x3bU, 0x61U, 0x08U, 0xd7U,
    0x2dU, 0x98U, 0x10U, 0xa3U, 0x09U, 0x14U, 0xdfU, 0xf4U
};
static const uint8_t K_TEST_KMAC[SC_AES_KEY_BYTES] = {
    0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
    0x08U, 0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU, 0x0eU, 0x0fU,
    0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
    0x18U, 0x19U, 0x1aU, 0x1bU, 0x1cU, 0x1dU, 0x1eU, 0x1fU
};

/* Card response payload returned by the reflective mock (4 bytes + SW). */
static const uint8_t K_CARD_RESP_PLAINTEXT[SC_CARD_RESP_TOTAL_BYTES] = {
    0xdeU, 0xadU, 0xbeU, 0xefU, 0x90U, 0x00U
};

/******************************************************************
 * 2. MockLogger — no-op implementation
 ******************************************************************/

class MockLogger : public CW_Logger {
public:
    bool begin(unsigned long = MOCK_UART_BAUD_RATE) override {
        return true;
    }
    void print(const __FlashStringHelper*) override {
    }
    void print(const char*) override {
    }
    void print(char) override {
    }
    void print(uint8_t, int = DEC) override {
    }
    void print(uint16_t, int = DEC) override {
    }
    void print(uint32_t, int = DEC) override {
    }
    void print(int, int = DEC) override {
    }
    void println() override {
    }
    void println(const __FlashStringHelper*) override {
    }
    void println(const char*) override {
    }
    void println(char) override {
    }
    void println(uint8_t, int = DEC) override {
    }
    void println(uint16_t, int = DEC) override {
    }
    void println(uint32_t, int = DEC) override {
    }
    void println(int, int = DEC) override {
    }
    ~MockLogger() override {
    }
};

/******************************************************************
 * 3. ScriptedMockNfcTransport — returns pre-loaded response buffers
 ******************************************************************/

struct MockScriptEntry {
    uint8_t data[MOCK_MAX_RESP_BYTES];
    uint8_t len;
    bool    succeed;
};

class ScriptedMockNfcTransport : public CW_NfcTransport {
public:
    MockScriptEntry scripts[MOCK_MAX_SCRIPTS]{};
    uint8_t         scriptCount = 0U;
    uint8_t         callIdx     = 0U;

    void reset() {
        scriptCount = 0U;
        callIdx     = 0U;
        (void)memset(scripts, 0, sizeof(scripts));
    }

    void addScript(const uint8_t* data, uint8_t len, bool succeed = true) {
        if (scriptCount < MOCK_MAX_SCRIPTS) {
            (void)memcpy(scripts[scriptCount].data, data, static_cast<size_t>(len));
            scripts[scriptCount].len     = len;
            scripts[scriptCount].succeed = succeed;
            scriptCount++;
        }
    }

    bool begin() override {
        return true;
    }
    bool inListPassiveTarget() override {
        return true;
    }
    void resetReader() override {
    }
    bool printFirmwareVersion() override {
        return true;
    }

    bool sendAPDU(const uint8_t* apdu, uint8_t apduLen,
                  uint8_t* response, uint8_t& responseLen) override {
        bool result = false;
        (void)apdu;
        (void)apduLen;

        if (callIdx < scriptCount) {
            const MockScriptEntry& e = scripts[callIdx];
            callIdx++;
            if (e.succeed) {
                (void)memcpy(response, e.data, static_cast<size_t>(e.len));
                responseLen = e.len;
                result = true;
            }
        }
        return result;
    }

    ~ScriptedMockNfcTransport() override {
    }
};

/******************************************************************
 * 4. ReflectiveMockNfcTransport — computes a valid encrypted response
 *    for the AES-CBC-MAC round-trip test.
 *
 *    Expected incoming APDU layout (from CW_SecureChannel::aesCbcEncrypt):
 *      header[4] | Lc[1] | MAC[16] | ciphertext[N]
 *
 *    The mock extracts the sent MAC, encrypts K_CARD_RESP_PLAINTEXT using
 *    the session Kenc with that MAC as IV (matching what aesCbcDecrypt
 *    expects), and wraps it in the response MAC.
 ******************************************************************/

class ReflectiveMockNfcTransport : public CW_NfcTransport {
public:
    const CW_SecureSession* session = nullptr;
    CW_CryptoProvider*      crypto  = nullptr;

    bool begin() override {
        return true;
    }
    bool inListPassiveTarget() override {
        return true;
    }
    void resetReader() override {
    }
    bool printFirmwareVersion() override {
        return true;
    }

    bool sendAPDU(const uint8_t* apdu, uint8_t apduLen,
                  uint8_t* response, uint8_t& responseLen) override {
        bool result = false;

        if ((apdu != NULL) &&
            (apduLen > static_cast<uint8_t>(SC_APDU_MAC_OFFSET + SC_APDU_MAC_BYTES))) {
            /* Step 1: extract sentMAC — used as IV when the channel decrypts the response */
            uint8_t sentMacIv[SC_AES_BLOCK_BYTES] = { 0U };
            (void)memcpy(sentMacIv, apdu + SC_APDU_MAC_OFFSET, SC_AES_BLOCK_BYTES);

            /* Step 2: encrypt K_CARD_RESP_PLAINTEXT using Kenc, sentMAC as IV, bit-padding */
            uint8_t cipherResp[SC_AES_BLOCK_BYTES * 2U] = { 0U };
            uint8_t encIv[SC_AES_BLOCK_BYTES] = { 0U };
            (void)memcpy(encIv, sentMacIv, SC_AES_BLOCK_BYTES);

            uint16_t cipherRespLen = crypto->aesCbcEncrypt(
                K_CARD_RESP_PLAINTEXT,
                static_cast<uint16_t>(sizeof(K_CARD_RESP_PLAINTEXT)),
                cipherResp,
                session->aesKey,
                static_cast<uint8_t>(sizeof(session->aesKey)),
                encIv,
                true);

            /* Step 3: build the response MAC input:
             *   [totalDataLen as 1st byte of a 16-byte zero block] || [cipherResp] */
            uint8_t totalDataLen = static_cast<uint8_t>(SC_AES_BLOCK_BYTES + cipherRespLen);
            uint8_t macInput[SC_AES_BLOCK_BYTES + SC_AES_BLOCK_BYTES * 2U] = { 0U };
            macInput[0U] = totalDataLen;
            (void)memcpy(macInput + SC_AES_BLOCK_BYTES, cipherResp,
                         static_cast<size_t>(cipherRespLen));

            uint16_t macInputLen = static_cast<uint16_t>(SC_AES_BLOCK_BYTES) + cipherRespLen;

            uint8_t macZeroIv[SC_AES_BLOCK_BYTES] = { 0U };
            uint8_t macOut[SC_AES_BLOCK_BYTES + SC_AES_BLOCK_BYTES * 2U] = { 0U };
            uint16_t macOutLen = crypto->aesCbcEncrypt(
                macInput,
                macInputLen,
                macOut,
                session->macKey,
                static_cast<uint8_t>(sizeof(session->macKey)),
                macZeroIv,
                false);

            /* Step 4: responseMac = last 16 bytes of macOut */
            const uint8_t* responseMac =
                macOut + macOutLen - static_cast<uint16_t>(SC_AES_BLOCK_BYTES);

            /* Step 5: assemble response = responseMac[16] || cipherResp[N] || SW[2] */
            uint8_t totalRespLen = static_cast<uint8_t>(
                static_cast<uint16_t>(SC_AES_BLOCK_BYTES) + cipherRespLen + SC_SW_BYTES);

            (void)memcpy(response, responseMac, SC_AES_BLOCK_BYTES);
            (void)memcpy(response + SC_AES_BLOCK_BYTES, cipherResp,
                         static_cast<size_t>(cipherRespLen));
            response[SC_AES_BLOCK_BYTES + cipherRespLen]             = 0x90U;
            response[SC_AES_BLOCK_BYTES + cipherRespLen + 1U] = 0x00U;
            responseLen = totalRespLen;
            result = true;
        }

        return result;
    }

    ~ReflectiveMockNfcTransport() override {
    }
};

/******************************************************************
 * 5. TestCryptoProvider — ESP32CryptoProvider with the WiFi/BT
 *    entropy-readiness gate removed.
 *
 *    ESP32CryptoProvider::random() returns false when neither WiFi
 *    nor BT is active, which would cause mutuallyAuthenticate() to
 *    abort during unit tests.  This subclass calls esp_fill_random()
 *    directly so tests run without a radio.
 ******************************************************************/

class TestCryptoProvider : public ESP32CryptoProvider {
public:
    bool random(uint8_t* dest, unsigned size) override {
        bool result = false;
        if ((dest != NULL) && (size > 0U)) {
            esp_fill_random(dest, static_cast<size_t>(size));
            result = true;
        }
        return result;
    }
    ~TestCryptoProvider() override {
    }
};

/******************************************************************
 * 6. Static instances shared across tests
 ******************************************************************/

static TestCryptoProvider         s_crypto;
static MockLogger                 s_logger;
static ScriptedMockNfcTransport   s_scriptedTransport;
static ReflectiveMockNfcTransport s_reflectiveTransport;

/******************************************************************
 * 6. checkStatusWord tests
 ******************************************************************/

TEST_CASE("checkStatusWord: SW 0x9000 returns true", "[secure_channel]")
{
    static const uint8_t resp[] = {
        0x01U, 0x02U, 0x03U, 0x04U, 0x90U, 0x00U
    };
    CW_SecureChannel channel(s_scriptedTransport, s_logger, s_crypto);

    bool ok = channel.checkStatusWord(resp, static_cast<uint8_t>(sizeof(resp)),
                                      0x90U, 0x00U);

    TEST_ASSERT_TRUE(ok);
}

TEST_CASE("checkStatusWord: SW mismatch returns false", "[secure_channel]")
{
    static const uint8_t resp[] = {
        0x01U, 0x02U, 0x6aU, 0x82U
    };
    CW_SecureChannel channel(s_scriptedTransport, s_logger, s_crypto);

    bool ok = channel.checkStatusWord(resp, static_cast<uint8_t>(sizeof(resp)),
                                      0x90U, 0x00U);

    TEST_ASSERT_FALSE(ok);
}

TEST_CASE("checkStatusWord: response shorter than 2 bytes returns false", "[secure_channel]")
{
    static const uint8_t resp[] = { 0x90U };
    CW_SecureChannel channel(s_scriptedTransport, s_logger, s_crypto);

    bool ok = channel.checkStatusWord(resp, static_cast<uint8_t>(sizeof(resp)),
                                      0x90U, 0x00U);

    TEST_ASSERT_FALSE(ok);
}

/******************************************************************
 * 7. extractCardEphemeralKey tests
 ******************************************************************/

TEST_CASE("extractCardEphemeralKey: extracts 64-byte key from synthetic certificate",
          "[secure_channel]")
{
    /* Build a 74-byte synthetic certificate:
     *   cert[0]    = 'C' marker
     *   cert[1..8] = nonce (arbitrary)
     *   cert[9]    = 0x04 (uncompressed prefix — dropped by the extractor)
     *   cert[10..41] = X coordinate (known pattern)
     *   cert[42..73] = Y coordinate (known pattern) */
    uint8_t cert[SC_CERT_TOTAL_BYTES] = { 0U };
    cert[0U] = 0x43U;  /* 'C' */
    for (uint8_t i = 0U; i < static_cast<uint8_t>(SC_CERT_NONCE_BYTES); i++) {
        cert[static_cast<size_t>(SC_CERT_MARKER_BYTES) + i] = static_cast<uint8_t>(i + 1U);
    }
    cert[SC_CERT_KEY_OFFSET] = 0x04U;  /* uncompressed prefix */
    for (uint8_t i = 1U; i < static_cast<uint8_t>(SC_CERT_KEY65_BYTES); i++) {
        cert[static_cast<size_t>(SC_CERT_KEY_OFFSET) + i] = static_cast<uint8_t>(0xA0U + i);
    }

    uint8_t pubKey[SC_EC_PUBKEY_BYTES]    = { 0U };
    uint8_t fullKey65[SC_CERT_KEY65_BYTES] = { 0U };

    CW_SecureChannel channel(s_scriptedTransport, s_logger, s_crypto);
    bool ok = channel.extractCardEphemeralKey(cert, pubKey, fullKey65);

    TEST_ASSERT_TRUE(ok);

    /* fullKey65[0] must be the 0x04 prefix */
    TEST_ASSERT_EQUAL_HEX8(0x04U, fullKey65[0U]);

    /* pubKey = fullKey65[1..64] = cert[10..73] */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(cert + SC_CERT_KEY_OFFSET + 1U, pubKey,
                                  SC_EC_PUBKEY_BYTES);
}

TEST_CASE("extractCardEphemeralKey: null cert pointer returns false", "[secure_channel]")
{
    uint8_t pubKey[SC_EC_PUBKEY_BYTES] = { 0U };

    CW_SecureChannel channel(s_scriptedTransport, s_logger, s_crypto);
    bool ok = channel.extractCardEphemeralKey(NULL, pubKey, NULL);

    TEST_ASSERT_FALSE(ok);
}

/******************************************************************
 * 8. Protocol-flow tests with ScriptedMockNfcTransport
 ******************************************************************/

TEST_CASE("selectApdu: succeeds when transport returns SW 0x9000", "[secure_channel]")
{
    uint8_t resp[SC_SELECT_RESP_BYTES] = { 0U };
    resp[SC_SELECT_RESP_BYTES - 2U] = 0x90U;
    resp[SC_SELECT_RESP_BYTES - 1U] = 0x00U;

    s_scriptedTransport.reset();
    s_scriptedTransport.addScript(resp, static_cast<uint8_t>(sizeof(resp)));

    CW_SecureChannel channel(s_scriptedTransport, s_logger, s_crypto);
    bool ok = channel.selectApdu();

    TEST_ASSERT_TRUE(ok);
}

TEST_CASE("selectApdu: fails when transport returns error SW", "[secure_channel]")
{
    uint8_t resp[SC_SELECT_RESP_BYTES] = { 0U };
    resp[SC_SELECT_RESP_BYTES - 2U] = 0x6aU;
    resp[SC_SELECT_RESP_BYTES - 1U] = 0x82U;

    s_scriptedTransport.reset();
    s_scriptedTransport.addScript(resp, static_cast<uint8_t>(sizeof(resp)));

    CW_SecureChannel channel(s_scriptedTransport, s_logger, s_crypto);
    bool ok = channel.selectApdu();

    TEST_ASSERT_FALSE(ok);
}

TEST_CASE("getCardCertificate: extracts 146 certificate bytes from mock response",
          "[secure_channel]")
{
    /* Build a 148-byte scripted response:
     *   bytes[0..145] = certificate data
     *   bytes[146..147] = SW 0x90 0x00 */
    uint8_t resp[SC_GET_CERT_RESP_BYTES] = { 0U };
    for (uint8_t i = 0U; i < static_cast<uint8_t>(SC_GET_CERT_RESP_BYTES - 2U); i++) {
        resp[i] = static_cast<uint8_t>(i + 1U);
    }
    resp[SC_GET_CERT_RESP_BYTES - 2U] = 0x90U;
    resp[SC_GET_CERT_RESP_BYTES - 1U] = 0x00U;

    s_scriptedTransport.reset();
    s_scriptedTransport.addScript(resp, static_cast<uint8_t>(sizeof(resp)));

    uint8_t certBuf[SC_GET_CERT_RESP_BYTES] = { 0U };
    uint8_t certLen = 0U;

    CW_SecureChannel channel(s_scriptedTransport, s_logger, s_crypto);
    bool ok = channel.getCardCertificate(certBuf, certLen);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(SC_GET_CERT_RESP_BYTES - 2U), certLen);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(resp, certBuf, certLen);
}

TEST_CASE("openSecureChannel: extracts 32-byte salt from mock response", "[secure_channel]")
{
    /* Build 34-byte scripted response: 32-byte salt + SW */
    uint8_t resp[SC_OPEN_SC_RESP_BYTES] = { 0U };
    for (uint8_t i = 0U; i < static_cast<uint8_t>(SC_SALT_BYTES); i++) {
        resp[i] = static_cast<uint8_t>(0xA0U + i);
    }
    resp[SC_OPEN_SC_RESP_BYTES - 2U] = 0x90U;
    resp[SC_OPEN_SC_RESP_BYTES - 1U] = 0x00U;

    s_scriptedTransport.reset();
    s_scriptedTransport.addScript(resp, static_cast<uint8_t>(sizeof(resp)));

    uint8_t salt[SC_SALT_BYTES]              = { 0U };
    uint8_t clientPub[SC_EC_PUBKEY_BYTES]    = { 0U };
    uint8_t clientPriv[SC_EC_COORD_BYTES]    = { 0U };
    const uECC_Curve_t* curve = uECC_secp256r1();

    CW_SecureChannel channel(s_scriptedTransport, s_logger, s_crypto);
    bool ok = channel.openSecureChannel(salt, clientPub, clientPriv, curve);

    TEST_ASSERT_TRUE(ok);

    /* Salt must match the first 32 bytes of the scripted response */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(resp, salt, SC_SALT_BYTES);

    /* Client keypair must have been generated (non-zero) */
    uint8_t zeroPub[SC_EC_PUBKEY_BYTES] = { 0U };
    uint8_t zeroPriv[SC_EC_COORD_BYTES] = { 0U };
    TEST_ASSERT_NOT_EQUAL(0, memcmp(clientPub,  zeroPub,  SC_EC_PUBKEY_BYTES));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(clientPriv, zeroPriv, SC_EC_COORD_BYTES));
}

TEST_CASE("mutuallyAuthenticate: sets session IV to first 16 bytes of mock response",
          "[secure_channel]")
{
    /* Scripted 66-byte response for MUTUALLY AUTHENTICATE:
     *   bytes[0..15]  = rolling IV (known pattern)
     *   bytes[16..63] = dummy encrypted data
     *   bytes[64..65] = SW 0x90 0x00 */
    uint8_t resp[SC_MUTUAL_AUTH_RESP_BYTES] = { 0U };
    for (uint8_t i = 0U; i < static_cast<uint8_t>(SC_IV_BYTES); i++) {
        resp[i] = static_cast<uint8_t>(0xC0U + i);
    }
    resp[SC_MUTUAL_AUTH_RESP_BYTES - 2U] = 0x90U;
    resp[SC_MUTUAL_AUTH_RESP_BYTES - 1U] = 0x00U;

    s_scriptedTransport.reset();
    s_scriptedTransport.addScript(resp, static_cast<uint8_t>(sizeof(resp)));

    /* Use the known P-256 public key as the card's ephemeral key.
     * Any valid point on secp256r1 works here — ECDH must not fail. */
    uint8_t cardEphemeralPub[SC_EC_PUBKEY_BYTES] = { 0U };
    (void)memcpy(cardEphemeralPub, K_CARD_EPHEMERAL_PUB, SC_EC_PUBKEY_BYTES);

    uint8_t clientPub[SC_EC_PUBKEY_BYTES]  = { 0U };
    uint8_t clientPriv[SC_EC_COORD_BYTES]  = { 0U };
    uint8_t salt[SC_SALT_BYTES]            = { 0U };
    const uECC_Curve_t* curve              = uECC_secp256r1();

    bool keyOk = s_crypto.makeKey(clientPub, clientPriv, curve);
    TEST_ASSERT_TRUE(keyOk);

    CW_SecureSession session;
    CW_SecureChannel channel(s_scriptedTransport, s_logger, s_crypto);
    bool ok = channel.mutuallyAuthenticate(session, salt, clientPub, clientPriv,
                                           curve, cardEphemeralPub);

    TEST_ASSERT_TRUE(ok);

    /* Session IV must be the first 16 bytes of the scripted response */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(resp, session.iv, SC_IV_BYTES);

    /* Session keys must be non-zero (ECDH + SHA-512 derivation ran) */
    uint8_t zeroKey[SC_AES_KEY_BYTES] = { 0U };
    TEST_ASSERT_NOT_EQUAL(0, memcmp(session.aesKey, zeroKey, SC_AES_KEY_BYTES));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(session.macKey, zeroKey, SC_AES_KEY_BYTES));
}

/******************************************************************
 * 9. Key derivation correctness test
 *
 *    The secure channel derives Kenc and Kmac as:
 *      sha512( ecdh_secret || "Cryptnox Basic CommonPairingData" || salt )
 *    Kenc = output[0..31], Kmac = output[32..63].
 *
 *    This test verifies the derivation is consistent by computing it
 *    independently and comparing with what SHA-512 over the same input
 *    produces directly through the crypto provider.
 ******************************************************************/

TEST_CASE("key derivation: ECDH + SHA-512 split yields distinct Kenc and Kmac",
          "[secure_channel]")
{
    const uECC_Curve_t* curve = uECC_secp256r1();

    /* Generate two ephemeral keypairs */
    uint8_t pubA[SC_EC_PUBKEY_BYTES]  = { 0U };
    uint8_t privA[SC_EC_COORD_BYTES]  = { 0U };
    uint8_t pubB[SC_EC_PUBKEY_BYTES]  = { 0U };
    uint8_t privB[SC_EC_COORD_BYTES]  = { 0U };

    bool okA = s_crypto.makeKey(pubA, privA, curve);
    bool okB = s_crypto.makeKey(pubB, privB, curve);
    TEST_ASSERT_TRUE(okA);
    TEST_ASSERT_TRUE(okB);

    /* ECDH: both sides must yield the same shared secret */
    uint8_t secretAB[SC_EC_COORD_BYTES] = { 0U };
    uint8_t secretBA[SC_EC_COORD_BYTES] = { 0U };
    bool ecdhAB = s_crypto.ecdh(pubB, privA, secretAB, curve);
    bool ecdhBA = s_crypto.ecdh(pubA, privB, secretBA, curve);
    TEST_ASSERT_TRUE(ecdhAB);
    TEST_ASSERT_TRUE(ecdhBA);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(secretAB, secretBA, SC_EC_COORD_BYTES);

    /* Manually derive Kenc/Kmac with a known salt */
    static const uint8_t SALT[SC_SALT_BYTES] = {
        0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U,
        0x99U, 0xaaU, 0xbbU, 0xccU, 0xddU, 0xeeU, 0xffU, 0x00U,
        0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U,
        0x09U, 0x0aU, 0x0bU, 0x0cU, 0x0dU, 0x0eU, 0x0fU, 0x10U
    };

    uint8_t concat[SC_EC_COORD_BYTES + CW_PAIRING_DATA_BYTES + SC_SALT_BYTES] = { 0U };
    (void)memcpy(concat, secretAB, SC_EC_COORD_BYTES);
    (void)memcpy(concat + SC_EC_COORD_BYTES, CW_PAIRING_DATA, CW_PAIRING_DATA_BYTES);
    (void)memcpy(concat + SC_EC_COORD_BYTES + CW_PAIRING_DATA_BYTES, SALT, SC_SALT_BYTES);

    uint8_t sha512Out[SC_SHA512_OUT_BYTES] = { 0U };
    s_crypto.sha512(concat, sizeof(concat), sha512Out);

    /* Kenc = sha512[0..31], Kmac = sha512[32..63] — they must differ */
    uint8_t* derivedKenc = sha512Out;
    uint8_t* derivedKmac = sha512Out + SC_AES_KEY_BYTES;

    TEST_ASSERT_NOT_EQUAL(0, memcmp(derivedKenc, derivedKmac, SC_AES_KEY_BYTES));

    /* Cross-check: running mutuallyAuthenticate with these inputs must set
     * the same Kenc/Kmac in the session (one scripted APDU response). */
    uint8_t mutualResp[SC_MUTUAL_AUTH_RESP_BYTES] = { 0U };
    mutualResp[SC_MUTUAL_AUTH_RESP_BYTES - 2U] = 0x90U;
    mutualResp[SC_MUTUAL_AUTH_RESP_BYTES - 1U] = 0x00U;

    s_scriptedTransport.reset();
    s_scriptedTransport.addScript(mutualResp,
                                   static_cast<uint8_t>(sizeof(mutualResp)));

    CW_SecureSession session;
    CW_SecureChannel channel(s_scriptedTransport, s_logger, s_crypto);

    /* publicB acts as the "card" ephemeral key; privA is the client key.
     * The ECDH inside mutuallyAuthenticate will compute secretAB. */
    bool ok = channel.mutuallyAuthenticate(session, SALT, pubA, privA, curve, pubB);
    TEST_ASSERT_TRUE(ok);

    TEST_ASSERT_EQUAL_HEX8_ARRAY(derivedKenc, session.aesKey, SC_AES_KEY_BYTES);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(derivedKmac, session.macKey, SC_AES_KEY_BYTES);
}

/******************************************************************
 * 10. AES-CBC-MAC round-trip: aesCbcEncrypt + aesCbcDecrypt
 *
 *     Uses a manually set session (known Kenc/Kmac) and the
 *     ReflectiveMockNfcTransport which computes a valid card response.
 *     Verifies the decrypted payload matches K_CARD_RESP_PLAINTEXT[0..3].
 ******************************************************************/

TEST_CASE("aesCbcEncrypt/aesCbcDecrypt: round-trip via reflective mock returns card payload",
          "[secure_channel]")
{
    /* Set up a session with known keys and a zero IV */
    CW_SecureSession session;
    (void)memcpy(session.aesKey, K_TEST_KENC, SC_AES_KEY_BYTES);
    (void)memcpy(session.macKey, K_TEST_KMAC, SC_AES_KEY_BYTES);
    (void)memset(session.iv, 0x00U, SC_IV_BYTES);

    /* Wire the reflective mock to this session */
    s_reflectiveTransport.session = &session;
    s_reflectiveTransport.crypto  = &s_crypto;

    CW_SecureChannel channel(s_reflectiveTransport, s_logger, s_crypto);

    /* Arbitrary plaintext command payload */
    static const uint8_t plaintext[]   = { 0xAAU, 0xBBU, 0xCCU };
    static const uint8_t apduHeader[]  = { 0x80U, 0x01U, 0x00U, 0x00U };

    uint8_t  decryptedOut[32U] = { 0U };
    uint16_t decryptedLen      = 0U;

    bool ok = channel.aesCbcEncrypt(
        session,
        apduHeader,
        static_cast<uint16_t>(sizeof(apduHeader)),
        plaintext,
        static_cast<uint16_t>(sizeof(plaintext)),
        decryptedOut,
        &decryptedLen);

    TEST_ASSERT_TRUE(ok);

    /* decryptedLen = card response payload without the two SW bytes */
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(SC_CARD_RESP_PAYLOAD_BYTES),
                              decryptedLen);

    /* Decrypted payload must match K_CARD_RESP_PLAINTEXT (excl. SW) */
    TEST_ASSERT_EQUAL_HEX8_ARRAY(K_CARD_RESP_PLAINTEXT, decryptedOut,
                                  SC_CARD_RESP_PAYLOAD_BYTES);
}
