<div align="center">

<img src="https://github.com/user-attachments/assets/6ce54a27-8fb6-48e6-9d1f-da144f43425a"/>

### cryptnox-sdk-esp32

ESP32 SDK for managing Cryptnox smart card wallets

</div>

# BasicUsage — End-to-End Walkthrough (SPI or I²C)

A single self-contained ESP-IDF project that exercises the **full
Cryptnox flow** on ESP32: pick SPI or I²C at build time, open the
secure channel, sign a 32-byte hash, wipe the secrets, disconnect.
Reads as a checklist of every step a production firmware will perform.

If you only need **one** of the steps, see the focused examples:

| You want… | See |
|-----------|-----|
| The secure channel + card identity | [Connect](../Connect/README.md) |
| A PIN verification flow | [VerifyPin](../VerifyPin/README.md) |
| A signature without the rest | [Sign](../Sign/README.md) |
| A real Ethereum tx broadcast | [UsdcSigning](../UsdcSigning/README.md) |

## Requirements

| Component | Details |
|-----------|---------|
| **Hardware Wallet** | Cryptnox Hardware Wallet, initialised **and** seeded |
| **NFC reader** | PN532 wired on **SPI** (default) or **I²C** — see [hardware setup](../../README.md#hardware-setup) |
| **Board** | ESP32-S3-DevKitC-1 |
| **Toolchain** | ESP-IDF v5.5 |

## Quick start

1. **Pick the bus** at the top of `main/main.cpp` — set exactly one of:

   ```cpp
   #define SPI_ENABLED  1     // MOSI=11, MISO=13, SCLK=12, CS=10
   #define I2C_ENABLED  0     // (set to 1 for I²C; mutually exclusive)
   ```

2. **Set the PIN** to match `cryptnox init`:

   ```cpp
   #define DEFAULT_PIN  "000000000"
   ```

3. **Edit `main/config.h`** with your Wi-Fi credentials.

4. Build, flash and monitor:

   ```bash
   cd examples/BasicUsage
   idf.py set-target esp32s3                # once
   idf.py build flash monitor
   ```

5. Exit the monitor with `Ctrl-]`. Place the card on the PN532
   antenna.

### Expected output

```
I (1280) basic_usage: Card connected and secure channel established
I (1290) basic_usage: Signing test hash...
I (1450) basic_usage: Signature received (64 bytes raw r||s)
I (1450) basic_usage:   R[0..7]: 7C 1F 3A 92 5E 0B 8C D4
I (1450) basic_usage:   S[0..7]: 12 E0 BC 4F A7 88 09 67
I (1450) basic_usage: Card processed successfully
```

> [!NOTE]
> The SDK uses the PN532 extended-frame transport internally so
> manufacturer-certificate pages up to ~411 bytes deliver correctly
> on both SPI and I²C.

## How it works

```
 app_main():
   nvs_flash_init()
   wifi_init() + esp_wifi_connect()        Wi-Fi → seeds the HW TRNG
   spi_bus_init()  /  i2c_bus_init()       Depending on SPI_ENABLED / I2C_ENABLED
   wallet.begin()                          PN532 reset + firmware probe

 loop():
   wallet.connect(session)                 SELECT + cert verify + ECDH
                                           + mutual authentication
   Build CW_SignRequest:
     keyType        = CW_SIGN_CURR_K1
     signatureType  = CW_SIGN_SIG_ECDSA_LOW_S
     pinLessMode    = CW_SIGN_WITH_PIN
     hash[32]       = 0x01 × 32            (test pattern)
     pin[]          = DEFAULT_PIN
   wallet.sign(req)                        SIGN APDU under the channel
   secure_wipe(hash, signature)            Zero local copies
   wallet.disconnect(session)              Zero session keys
   vTaskDelay(pdMS_TO_TICKS(1000))
```

## Step-by-step code

**1. Interface selection** — exactly one of:

```cpp
#define SPI_ENABLED  1
#define I2C_ENABLED  0
```

On `SPI_ENABLED == 1` the firmware brings up the SPI3 host with
MOSI=11, MISO=13, SCLK=12, CS=10. On `I2C_ENABLED == 1` it brings
up `I2C_NUM_0` with the pin map documented at the top of `main.cpp`.

**2. Bring up the radio, bus, and reader:**

```cpp
ESP_ERROR_CHECK(nvs_flash_init());
wifi_init();                                // brings Wi-Fi up → seeds TRNG

#if SPI_ENABLED
    spi_bus_init();
#elif I2C_ENABLED
    i2c_bus_init();
#endif

if (!wallet.begin()) {
    ESP_LOGE(TAG, "PN532 init failed");
    vTaskDelay(portMAX_DELAY);
}
```

**3. Open the channel, sign, and wipe:**

```cpp
CW_SecureSession session;
if (wallet.connect(session)) {
    uint8_t testHash[CW_HASH_SIZE];
    memset(testHash, 0x01, sizeof(testHash));   // replace with SHA-256(tx)

    CW_SignRequest req(session, CW_SIGN_CURR_K1,
                       CW_SIGN_SIG_ECDSA_LOW_S, CW_SIGN_WITH_PIN);
    req.hash       = testHash;
    req.hashLength = sizeof(testHash);
    CW_Utils::safe_memcpy(req.pin, sizeof(req.pin),
                          reinterpret_cast<const uint8_t*>(DEFAULT_PIN),
                          DEFAULT_PIN_LEN);

    CW_SignResult sig = wallet.sign(req);
    // sig.signature = r[32] || s[32], sig.errorCode == CW_OK on success

    CW_Utils::secure_wipe(testHash, sizeof(testHash));
    CW_Utils::secure_wipe(sig.signature, sizeof(sig.signature));
}
wallet.disconnect(session);
```

## Hardening for production

This example is a demo. Before shipping firmware to end-users:

- **Silence the SDK logs.** The `ESP32Logger` writes through
  `ESP_LOG*`. Set `CONFIG_LOG_DEFAULT_LEVEL_NONE=y` (or
  `esp_log_level_set("*", ESP_LOG_NONE)` at boot) to stop the SDK
  from emitting APDU and key material on UART0 in shipping firmware.
- **Move the PIN off flash.** A hardcoded `DEFAULT_PIN` lives in
  `.rodata` and is recoverable via JTAG / firmware dump. In production
  read the PIN at runtime (capacitive keypad, BLE prompt, secure
  element) and call `CW_Utils::secure_wipe` on the buffer after
  `wallet.sign()`.
- **Guard the PIN retry counter.** Apply the halt-on-wrong-PIN
  pattern from [VerifyPin](../VerifyPin/README.md) /
  [Sign](../Sign/README.md) so the loop cannot exhaust the on-card
  counter.

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `Please enable exactly one of SPI_ENABLED / I2C_ENABLED` (build error) | Both macros are 0 or both are 1 | Set exactly one to 1 |
| `PN532 init failed` | Reader wiring / bus mode switches | Check VCC = 3.3 V, the switches and the configured pins — see [hardware setup](../../README.md#hardware-setup) |
| Wi-Fi never connects | Wrong SSID / password, or 5 GHz-only network | ESP32-S3 is 2.4 GHz only — verify `main/config.h` |
| `Sign failed, errorCode: 0x81` | No seed on the card | `cryptnox seed generate` |
| `Sign failed, errorCode: 0x82` | Wrong PIN | Edit `DEFAULT_PIN`, re-flash — see [VerifyPin](../VerifyPin/README.md) |

## License

`cryptnox-sdk-esp32` is dual-licensed:

- **LGPL-3.0** for open-source projects and proprietary projects that comply with LGPL requirements
- **Commercial license** for projects that require a proprietary license without LGPL obligations (see [COMMERCIAL.md](../../COMMERCIAL.md) for details)

For commercial inquiries, contact: contact@cryptnox.com
