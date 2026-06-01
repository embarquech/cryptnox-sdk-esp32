<div align="center">

<img src="https://github.com/user-attachments/assets/6ce54a27-8fb6-48e6-9d1f-da144f43425a"/>

### cryptnox-sdk-esp32

ESP32 SDK for managing Cryptnox smart card wallets

</div>

# Connect — Secure Channel + Card Info

Open the Cryptnox Hardware Wallet secure channel from an ESP32-S3 and
read back the card owner's **name** and **email**. This is the
**safest starting point**: the firmware never sends a PIN, so it
cannot lock the card.

## Requirements

| Component | Details |
|-----------|---------|
| **Hardware Wallet** | Cryptnox Hardware Wallet, initialised (`cryptnox init`) |
| **NFC reader** | PN532 over SPI — MOSI=11, MISO=13, SCLK=12, CS=10 (see [hardware setup](../../README.md#hardware-setup)) |
| **Board** | ESP32-S3-DevKitC-1 |
| **Toolchain** | ESP-IDF v5.5 |
| **Repository** | Cloned with submodules (`git clone --recurse-submodules …`) |

No PIN, no seed required. Wi-Fi credentials in `main/config.h` are
still mandatory — the radio is started on boot so it feeds the ESP32
hardware TRNG with full entropy before any crypto runs.

## Quick start

```bash
# from the repository root, after sourcing ESP-IDF env
cd examples/Connect
$EDITOR main/config.h                       # fill WIFI_SSID / WIFI_PASSWORD
idf.py set-target esp32s3                   # once
idf.py build flash monitor
```

Exit the serial monitor with `Ctrl-]`. Place the card on the PN532
antenna once the firmware reports `PN532 ready`.

### Expected output

```
I (520) wifi:wifi driver task: ...
I (1234) connect: Wi-Fi connected, TRNG seeded
I (1280) connect: PN532 firmware 1.6, features 0x07 (MIFARE + ISO-DEP + FeliCa)
I (2410) connect: Card connected, secure channel established
I (2420) connect: Owner name : Alice
I (2421) connect: Owner email: alice@cryptnox.com
```

## How it works

```
 wifi_init() + esp_wifi_connect()    Bring up Wi-Fi → TRNG ready
        │
 spi_bus_initialize()                Configure SPI3 host
        │
 wallet.begin()                      Reset the PN532, probe firmware
        │
 wallet.connect(session)             SELECT + cert chain verify
        │                            (secp256r1) + ECDH + mutual auth
        │                            → session.aesKey / macKey / iv
        │
 wallet.getCardInfo(session)         Secured APDU (AES-CBC + MAC)
        │                            Decrypt response, parse name & email
        │
 wallet.disconnect(session)          Zero session keys
```

## Step-by-step code

**Wire the adapters together** (declared once in `app_main`):

```cpp
ESP32Logger          logger;
Pn532NfcTransport    nfc(/*pn532*/ &pn532, logger);
ESP32CryptoProvider  cryptoProvider;
ESP32Platform        platform;
CryptnoxWallet       wallet(nfc, logger, cryptoProvider, platform);
```

**Bring up the radio + bus + reader:**

```cpp
ESP_ERROR_CHECK(nvs_flash_init());
wifi_init();                          // brings up Wi-Fi → seeds the HW TRNG
spi_bus_init();
if (!wallet.begin()) {
    ESP_LOGE(TAG, "PN532 init failed");
    vTaskDelay(portMAX_DELAY);
}
```

**Open the channel and read the card** in a tight loop:

```cpp
while (true) {
    CW_SecureSession session;
    if (wallet.connect(session)) {
        CW_CardInfo info;
        if (wallet.getCardInfo(session, &info)) {
            ESP_LOGI(TAG, "Owner name : %s", info.name);
            ESP_LOGI(TAG, "Owner email: %s", info.email);
        }
    }
    wallet.disconnect(session);
    vTaskDelay(pdMS_TO_TICKS(1000));
}
```

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `PN532 init failed` | Reader wiring / power / mode switches | Check VCC = 3.3 V, the MOSI/MISO/SCLK/CS pinout and the PN532 SPI mode switches (`SW0=HIGH, SW1=LOW`) — see [hardware setup](../../README.md#hardware-setup) |
| Wi-Fi never connects | Wrong SSID / password, or 5 GHz-only network | ESP32-S3 supports 2.4 GHz only — verify SSID, password, band in `main/config.h` |
| `Card not detected or secure channel failed` | Card off the antenna, or card not initialised | Bring the card within ~1 cm of the antenna; run `cryptnox init` if it is a brand-new card |
| `getCardInfo failed (channel error or parse error)` | Card initialised without an owner name/email | Re-run `cryptnox init` and fill in the owner fields |

## License

`cryptnox-sdk-esp32` is dual-licensed:

- **LGPL-3.0** for open-source projects and proprietary projects that comply with LGPL requirements
- **Commercial license** for projects that require a proprietary license without LGPL obligations (see [COMMERCIAL.md](../../COMMERCIAL.md) for details)

For commercial inquiries, contact: contact@cryptnox.com
