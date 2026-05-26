/**
 * @file Sign.ino
 * @brief Minimal Cryptnox example: sign a 32-byte hash on the secp256k1 curve.
 *
 * Wiring & prerequisites:
 *   - PN532 NFC reader on SPI, with SS on pin @ref PN532_SS_PIN.
 *   - A Cryptnox card initialised with a known PIN AND a loaded seed
 *     (use the Cryptnox CLI: @c cryptnox @c initialize then
 *     @c cryptnox @c seed @c generate).
 *   - @ref DEMO_PIN must match the PIN set on the card.
 *
 * What the sketch does in each loop iteration:
 *   1. Connect to the card and establish the secure channel.
 *   2. Sign a 32-byte hash on the secp256k1 curve (key type
 *      @ref CW_SIGN_CURR_K1, signature type @ref CW_SIGN_SIG_ECDSA_LOW_S,
 *      PIN included in the sign payload @ref CW_SIGN_WITH_PIN).
 *   3. Print the result, securely wipe local secrets, disconnect.
 *
 * @warning On @ref CW_SIGN_PIN_INCORRECT (0x82) the sketch enters an
 *          infinite halt: every wrong PIN attempt decrements the card's
 *          retry counter and reaching 0 permanently blocks the PIN.
 *          Recovery then requires the PUK via @c cryptnox @c unblock_pin.
 *          Verify @ref DEMO_PIN before retrying.
 *
 * @note The PIN "000000000" used by the project examples is a demo
 *       placeholder. In real deployments set a strong PIN, never commit
 *       source files containing a real PIN, and keep this value out of any
 *       version-controlled config.
 *
 * @note The hash filled with 0x01 below is a test pattern. In real use
 *       replace it with the SHA-256 (or Keccak-256 for Ethereum) digest of
 *       the transaction you want the card to sign.
 */
#include <CryptnoxSDK.h>
#include <SPI.h>

/** @brief SPI slave-select (CS) pin connected to the PN532 module. */
#define PN532_SS_PIN  (10U)

/**
 * @brief Demo PIN used by this example. Must match the PIN that the card
 *        was initialised with (4–9 ASCII digits).
 */
#define DEMO_PIN      "000000000"

/** @brief Delay between loop iterations in milliseconds. */
static const uint32_t LOOP_DELAY_MS = 1000U;

/** @brief Arduino logger adapter — emits SDK diagnostics on @c Serial. */
ArduinoLoggerAdapter   serialAdapter;

/** @brief PN532 transport adapter over SPI. */
PN532Adapter           nfc(serialAdapter, PN532_SS_PIN, &SPI);

/** @brief Crypto provider (AES / SHA / micro-ecc / TRNG bridge for Arduino). */
ArduinoCryptoProvider  cryptoProvider;

/** @brief High-level Cryptnox wallet wiring the three adapters together. */
CryptnoxWallet         wallet(nfc, serialAdapter, cryptoProvider);

/**
 * @brief Arduino setup hook.
 *
 * Brings up @c Serial, the SPI bus and the PN532 reader. Halts on init
 * failure (no reader detected) so the user can inspect the Serial output.
 */
void setup() {
    serialAdapter.begin(115200UL);
    delay(LOOP_DELAY_MS);          /* wait for USB-serial to enumerate */
    SPI.begin();

    if (!wallet.begin()) {
        (void)serialAdapter.println(F("PN532 init failed"));
        while (true) {}
    }
}

/**
 * @brief Arduino main loop.
 *
 * One full sign session per iteration on the happy path:
 *   - @ref CryptnoxWallet::connect opens the secure channel,
 *   - @ref CryptnoxWallet::sign signs the test hash with the on-card key,
 *   - sensitive stack buffers are wiped via @ref CW_Utils::secure_wipe,
 *   - @ref CryptnoxWallet::disconnect tears the channel down.
 *
 * On @ref CW_SIGN_PIN_INCORRECT the sketch enters an infinite halt to
 * protect the card's retry counter. Other non-OK error codes are reported
 * but the loop continues (those errors do not consume PIN tries).
 *
 * The 1 s delay between successful iterations gives time to remove/replace
 * the card and keeps the Serial output readable.
 */
void loop() {
    CW_SecureSession session{};
    bool connected = wallet.connect(session);

    if (!connected) {
        (void)serialAdapter.println(F("Card not detected"));
    }

    if (connected) {
        uint8_t hash[CW_HASH_SIZE] = {};
        (void)memset(hash, 0x01, sizeof(hash));   /* replace with SHA-256 of your tx */

        CW_SignRequest req(session,
                           CW_SIGN_CURR_K1,
                           CW_SIGN_SIG_ECDSA_LOW_S,
                           CW_SIGN_WITH_PIN);
        req.hash       = hash;
        req.hashLength = sizeof(hash);
        (void)CW_Utils::safe_memcpy(req.pin, sizeof(req.pin),
                                    reinterpret_cast<const uint8_t*>(DEMO_PIN),
                                    strlen(DEMO_PIN));

        CW_SignResult sig = wallet.sign(req);
        if (sig.errorCode == CW_OK) {
            (void)serialAdapter.println(F("Signature OK"));

            (void)serialAdapter.print(F("  r = "));
            for (uint8_t i = 0U; i < static_cast<uint8_t>(CW_HASH_SIZE); i++) {
                uint8_t b = sig.signature[CW_SIG_R_OFFSET + i];
                if (b < 0x10U) {
                    (void)serialAdapter.print(F("0"));
                }
                (void)serialAdapter.print(b, HEX);
            }
            (void)serialAdapter.println();

            (void)serialAdapter.print(F("  s = "));
            for (uint8_t i = 0U; i < static_cast<uint8_t>(CW_HASH_SIZE); i++) {
                uint8_t b = sig.signature[CW_SIG_S_OFFSET + i];
                if (b < 0x10U) {
                    (void)serialAdapter.print(F("0"));
                }
                (void)serialAdapter.print(b, HEX);
            }
            (void)serialAdapter.println();

            (void)serialAdapter.print(F("  errorCode = 0x"));
            (void)serialAdapter.println(sig.errorCode, HEX);
        } else if (sig.errorCode == CW_SIGN_PIN_INCORRECT) {
            /* CRITICAL: do NOT loop — each wrong PIN attempt burns a retry. */
            (void)serialAdapter.println(F("Wrong PIN — halting to protect retry counter"));
            (void)CW_Utils::secure_wipe(hash, sizeof(hash));
            (void)CW_Utils::secure_wipe(sig.signature, sizeof(sig.signature));
            wallet.disconnect(session);
            while (true) {}
        } else {
            (void)serialAdapter.print(F("Sign failed: 0x"));
            (void)serialAdapter.println(sig.errorCode, HEX);
        }

        (void)CW_Utils::secure_wipe(hash, sizeof(hash));
        (void)CW_Utils::secure_wipe(sig.signature, sizeof(sig.signature));
    }

    wallet.disconnect(session);
    delay(LOOP_DELAY_MS);
}
