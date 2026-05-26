/**
 * @file Connect.ino
 * @brief Minimal Cryptnox example: open a secure channel and fetch card info.
 *
 * Wiring & prerequisites:
 *   - PN532 NFC reader on SPI, with SS on pin @ref PN532_SS_PIN.
 *   - A Cryptnox card initialised (the secure channel itself does not need
 *     a PIN, but the card must be programmed; use @c cryptnox @c initialize).
 *
 * What the sketch does in each loop iteration:
 *   1. Connect to the card and establish the secure channel
 *      (@ref CryptnoxWallet::connect).
 *   2. On success, ask the card for its info
 *      (@ref CryptnoxWallet::getCardInfo).
 *   3. Disconnect.
 *
 * This sketch never submits a PIN, so it cannot lock the card. It is the
 * safest starting point to validate that the wiring and the secure channel
 * work end to end before moving to @c VerifyPin or @c Sign examples.
 */
#include <CryptnoxSDK.h>
#include <SPI.h>

/** @brief SPI slave-select (CS) pin connected to the PN532 module. */
#define PN532_SS_PIN  (10U)

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
 * Brings up @c Serial, the SPI bus and the PN532 reader, then prints the
 * PN532 firmware version as a sanity check that the reader is alive and
 * speaking. Halts on init failure so the user can inspect the Serial output.
 */
void setup() {
    serialAdapter.begin(115200UL);
    delay(LOOP_DELAY_MS);          /* wait for USB-serial to enumerate */
    SPI.begin();

    if (!wallet.begin()) {
        (void)serialAdapter.println(F("PN532 init failed"));
        while (true) {}
    }

    /* Prints IC chip, firmware version and supported NFC features. */
    (void)nfc.printFirmwareVersion();
}

/**
 * @brief Arduino main loop.
 *
 * One full secure-channel session per iteration:
 *   - @ref CryptnoxWallet::connect performs SELECT + manufacturer cert
 *     verification + ECDH key agreement + mutual authentication. When it
 *     returns @c true the channel is mathematically established
 *     (@c session.aesKey, @c session.macKey, @c session.iv are populated
 *     and authenticated against the card).
 *   - @ref CryptnoxWallet::getCardInfo fetches card identity over the channel.
 *   - @ref CryptnoxWallet::disconnect tears the channel down and zeroes
 *     the session keys.
 *
 * The 1 s delay between iterations gives time to remove/replace the card
 * and keeps the Serial output readable.
 */
void loop() {
    CW_SecureSession session{};
    bool connected = wallet.connect(session);

    if (connected) {
        (void)serialAdapter.println(F("Card connected, secure channel established"));

        CW_CardInfo info{};
        bool infoOk = wallet.getCardInfo(session, &info);
        if (infoOk) {
            (void)serialAdapter.print(F("Owner name : "));
            (void)serialAdapter.println(info.name);
            (void)serialAdapter.print(F("Owner email: "));
            (void)serialAdapter.println(info.email);
        } else {
            (void)serialAdapter.println(F("getCardInfo failed (channel error or parse error)"));
        }
    } else {
        (void)serialAdapter.println(F("Card not detected or secure channel failed"));
    }

    wallet.disconnect(session);
    delay(LOOP_DELAY_MS);
}
