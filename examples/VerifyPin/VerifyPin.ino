/**
 * @file VerifyPin.ino
 * @brief Minimal Cryptnox example: open a secure channel and submit a PIN.
 *
 * Wiring & prerequisites:
 *   - PN532 NFC reader on SPI, with SS on pin @ref PN532_SS_PIN.
 *   - A Cryptnox card initialised with a known PIN (use the Cryptnox CLI:
 *     `cryptnox initialize`, then `cryptnox seed generate`).
 *   - @ref DEMO_PIN must match the PIN set on the card.
 *
 * What the sketch does in each loop iteration:
 *   1. Connect to the card and establish the secure channel.
 *   2. Submit @ref DEMO_PIN over the secure channel.
 *   3. Print "PIN accepted" if the card returned SW 0x9000, otherwise
 *      "PIN rejected (or channel error)". When the SW is not 0x9000 the SDK
 *      also prints the raw bytes (e.g. "Secured APDU: bad SW 0x63C2" means
 *      wrong PIN with 2 retries remaining).
 *   4. Disconnect.
 *
 * @warning Every wrong PIN attempt decrements the card's retry counter.
 *          Reaching 0 retries permanently blocks the PIN; recovery then
 *          requires the PUK via @c cryptnox unblock_pin. The sketch halts
 *          immediately on a failed @ref CryptnoxWallet::verifyPin to prevent
 *          repeated attempts that would exhaust the counter.
 *
 * @note The PIN "000000000" used by the project examples is a demo
 *       placeholder. In real deployments set a strong PIN, never commit
 *       source files containing a real PIN, and keep this value out of any
 *       version-controlled config.
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
 * One full secure-channel session per iteration on the happy path:
 *   - @ref CryptnoxWallet::connect opens the channel,
 *   - @ref CryptnoxWallet::verifyPin sends @ref DEMO_PIN,
 *   - @ref CryptnoxWallet::disconnect tears the channel down.
 *
 * If @ref CryptnoxWallet::verifyPin returns @c false the sketch enters an
 * infinite halt. This is deliberate: looping would resubmit the same wrong
 * PIN on every iteration and exhaust the card's retry counter within a few
 * seconds, permanently blocking the PIN.
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
        bool pinOk = wallet.verifyPin(session,
                                      reinterpret_cast<const uint8_t*>(DEMO_PIN),
                                      static_cast<uint8_t>(strlen(DEMO_PIN)));
        if (!pinOk) {
            /* CRITICAL: do NOT loop — each wrong PIN attempt decrements the
             * card's retry counter and permanently blocks the PIN at zero.
             * Fix DEMO_PIN before retrying. */
            (void)serialAdapter.println(F("PIN rejected — halting to protect retry counter"));
            wallet.disconnect(session);
            while (true) {}
        }

        (void)serialAdapter.println(F("PIN accepted"));
    }

    wallet.disconnect(session);
    delay(LOOP_DELAY_MS);
}
