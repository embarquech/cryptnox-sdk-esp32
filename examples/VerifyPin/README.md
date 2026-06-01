<div align="center">

<img src="https://github.com/user-attachments/assets/6ce54a27-8fb6-48e6-9d1f-da144f43425a"/>

### cryptnox-sdk-esp32

ESP32 SDK for managing Cryptnox smart card wallets

</div>

# VerifyPin — PIN Verification Over the Secure Channel

Open the Cryptnox Hardware Wallet secure channel and submit a PIN. The
firmware prints `PIN accepted` on success and **halts** the loop on a
wrong PIN so it cannot exhaust the on-card retry counter.

> [!WARNING]
> **Read this before flashing.** Every wrong PIN attempt decrements
> an on-card retry counter (typically 3–5 tries). At zero the PIN is
> permanently blocked; recovery then requires the PUK via
> `cryptnox unblock_pin`. This example halts on a wrong PIN — do not
> remove the `vTaskDelay(portMAX_DELAY)` guard, and verify `DEMO_PIN`
> matches the value used during `cryptnox init` **before** flashing.

## Requirements

| Component | Details |
|-----------|---------|
| **Hardware Wallet** | Cryptnox Hardware Wallet, initialised with a known PIN |
| **NFC reader** | PN532 over SPI — MOSI=11, MISO=13, SCLK=12, CS=10 (see [hardware setup](../../README.md#hardware-setup)) |
| **Board** | ESP32-S3-DevKitC-1 |
| **Toolchain** | ESP-IDF v5.5 |

`main/config.h` must contain valid `WIFI_SSID` / `WIFI_PASSWORD` —
the radio is started on boot to seed the hardware TRNG before any
crypto runs.

## Quick start

1. **Edit `DEMO_PIN`** in `main/main.cpp` to match your card's PIN.
2. **Create your runtime config from the template** (kept out of git so
   credentials never get committed) and fill in Wi-Fi:

   ```bash
   cp config.template.h main/config.h
   # then edit main/config.h: set WIFI_SSID / WIFI_PASSWORD
   ```

3. Build, flash and monitor:

   ```bash
   cd examples/VerifyPin
   idf.py set-target esp32s3                # once
   idf.py build flash monitor
   ```

4. Exit the monitor with `Ctrl-]`. Place the card on the PN532 antenna.

### Expected output

**Happy path** (every second):

```
I (1280) verify_pin: Card connected, secure channel established
I (1305) verify_pin: PIN accepted
```

**Wrong PIN** (firmware then halts):

```
E (1280) verify_pin: Secured APDU: bad SW 0x63C2
E (1281) verify_pin: PIN rejected — halting to protect retry counter
```

The SDK prints the raw `SW1 SW2` bytes on PIN failure:

- `0x63CN` — wrong PIN, **N** retries remaining
- `0x6983` — PIN already blocked (0 retries)

## How it works

```
 wallet.connect(session)            SELECT + cert verify + ECDH + mutual auth
        │
 wallet.verifyPin(session, ...)     Secured APDU (AES-CBC + MAC) carrying
        │                           the PIN. Card checks it locally and
        │                           replies with SW 0x9000 on success or
        │                           0x63CN on wrong PIN with N retries left.
        │
 wallet.disconnect(session)         Zero session keys
```

`verifyPin` returns `true` **only** when the card replies `0x9000`.

## Step-by-step code

```cpp
/* Replace with the PIN set on the card (4–9 ASCII digits). */
static const uint8_t DEMO_PIN[CW_MAX_PIN_LENGTH] = {
    '0', '0', '0', '0', '0', '0', '0', '0', '0'
};

CW_SecureSession session{};
if (!wallet.connect(session)) {
    ESP_LOGW(TAG, "Card not detected");
    wallet.disconnect(session);
    vTaskDelay(pdMS_TO_TICKS(1000));
    continue;
}

if (!wallet.verifyPin(session,
                      DEMO_PIN,
                      static_cast<uint8_t>(CW_MAX_PIN_LENGTH))) {
    ESP_LOGE(TAG, "PIN rejected — halting to protect retry counter");
    wallet.disconnect(session);
    vTaskDelay(portMAX_DELAY);          // CRITICAL: do NOT loop on failure
}

ESP_LOGI(TAG, "PIN accepted");
wallet.disconnect(session);
```

## Recovering a blocked PIN

If the PIN counter has reached zero, run the
[Cryptnox CLI](https://github.com/cryptnox/cryptnox-cli) on a host
with a PC/SC reader:

```bash
cryptnox unblock_pin
```

The CLI prompts for the **PUK** (set during `cryptnox init`).
Without the PUK the PIN cannot be reset and the on-card signing key
material is unrecoverable.

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `PN532 init failed` | Reader wiring / SPI mode switches | See [hardware setup](../../README.md#hardware-setup) |
| Wi-Fi never connects | Wrong SSID / password, or 5 GHz-only network | ESP32-S3 is 2.4 GHz only — verify `WIFI_SSID` / `WIFI_PASSWORD` in `main/config.h` |
| `Card not detected` | Card not on the antenna | Bring the card within ~1 cm of the antenna |
| `bad SW 0x63CN` | Wrong PIN, N retries remaining | Fix `DEMO_PIN`, re-flash |
| `bad SW 0x6983` | PIN blocked (0 retries) | Run `cryptnox unblock_pin` with the PUK |
| `mutuallyAuthenticate failed` | Non-Cryptnox card or seed mismatch | Verify the card was set up with `cryptnox init` |

## License

`cryptnox-sdk-esp32` is dual-licensed:

- **LGPL-3.0** for open-source projects and proprietary projects that comply with LGPL requirements
- **Commercial license** for projects that require a proprietary license without LGPL obligations (see [COMMERCIAL.md](../../COMMERCIAL.md) for details)

For commercial inquiries, contact: contact@cryptnox.com
