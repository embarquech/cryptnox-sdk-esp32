<div align="center">

<img src="https://github.com/user-attachments/assets/6ce54a27-8fb6-48e6-9d1f-da144f43425a"/>

### cryptnox-sdk-esp32

ESP32 SDK for managing Cryptnox smart card wallets

</div>

# Sign — ECDSA secp256k1 on a 32-Byte Hash

Sign an arbitrary 32-byte digest on the Cryptnox Hardware Wallet using
the **secp256k1** curve (Bitcoin, Ethereum, BSC, Polygon, …). The
private key never leaves the card; the ESP32 only ever sees the hash
and the resulting `(r, s)`.

> [!WARNING]
> Every wrong PIN attempt decrements an on-card retry counter
> (typically 3–5 tries). At zero the PIN is permanently blocked.
> The firmware halts on `CW_SIGN_PIN_INCORRECT` (`0x82`) — do not
> remove the `vTaskDelay(portMAX_DELAY)` guard, and verify
> `DEMO_PIN` before flashing.

## Requirements

| Component | Details |
|-----------|---------|
| **Hardware Wallet** | Cryptnox Hardware Wallet, initialised **and** seeded |
| **NFC reader** | PN532 over SPI — MOSI=11, MISO=13, SCLK=12, CS=10 (see [hardware setup](../../README.md#hardware-setup)) |
| **Board** | ESP32-S3-DevKitC-1 |
| **Toolchain** | ESP-IDF v5.5 |

`main/config.h` must contain valid `WIFI_SSID` / `WIFI_PASSWORD` —
the radio is started on boot to seed the hardware TRNG before any
crypto runs.

Provision the card from a host with a PC/SC reader and the
[Cryptnox CLI](https://github.com/cryptnox/cryptnox-cli):

```bash
cryptnox init                 # sets the PIN + PUK
cryptnox seed generate        # generates a BIP39 seed
```

Without a seed the SDK returns `CW_SIGN_NO_KEY_LOADED` (`0x81`).

## Quick start

1. **Edit `DEMO_PIN`** in `main/main.cpp` to match your card's PIN.
2. **Edit `main/config.h`** with your Wi-Fi credentials.
3. Build, flash and monitor:

   ```bash
   cd examples/Sign
   idf.py set-target esp32s3                # once
   idf.py build flash monitor
   ```

4. Exit the monitor with `Ctrl-]`. Place the card on the PN532 antenna.

### Expected output

```
I (1280) sign: Card connected, secure channel established
I (1450) sign: Signature OK
I (1450) sign:   r = 7C1F3A925E0B8CD4920FAB7C53E1B9D81F2A4C76E9D805BC5D2EAD12C3FA47CB
I (1450) sign:   s = 12E0BC4FA7880967DA1F0EBD83C2B791580AC4D62E16039F7B4CDDF13E2A89C5
I (1450) sign:   errorCode = 0x0
```

`r` and `s` together form the 64-byte raw ECDSA signature
(`signature[0..31]` = r, `signature[32..63]` = s). The card returns a
**canonical low-S** signature (S ≤ n/2), so the output is directly
forwardable to any chain that enforces BIP-62.

## How it works

```
 wallet.connect(session)        SELECT + cert verify + ECDH + mutual auth
        │
 Build CW_SignRequest:
   keyType        = CW_SIGN_CURR_K1         (secp256k1, current key)
   signatureType  = CW_SIGN_SIG_ECDSA_LOW_S (canonical low-S, BIP-62)
   pinLessMode    = CW_SIGN_WITH_PIN        (PIN included in the payload)
   hash[32]       = your digest             (here a test pattern 0x01 × 32)
   pin[]          = DEMO_PIN
        │
 wallet.sign(req)               Secured SIGN APDU. The card checks the
        │                       PIN, signs the hash with its secp256k1
        │                       private key, and replies with r ‖ s.
        │
 secure_wipe(hash, signature)   Zero local copies
        │
 wallet.disconnect(session)     Zero session keys
```

### Choosing the key path

`CW_SIGN_CURR_K1` signs with the card's **current** secp256k1 key
(usually `m/`). To derive a BIP-44 sub-key first, set `derivePath` /
`derivePathLength` on the request and use `CW_SIGN_DERIVE_K1` — see
[UsdcSigning](../UsdcSigning/README.md) for a worked example
(`m/44'/60'/0'/0/0` for Ethereum).

### PIN: in payload vs pre-verified

| Mode | Round-trips | Notes |
|------|------------:|-------|
| `CW_SIGN_WITH_PIN` (this example) | 1 | PIN included inside the SIGN APDU; one shot |
| `CW_SIGN_PINLESS` after `verifyPin()` | 2 | One PIN verification covers many subsequent signatures |

Pre-verifying is preferable when you sign more than one hash per
session.

## Step-by-step code

**Build the sign request:**

```cpp
uint8_t hash[CW_HASH_SIZE];
memset(hash, 0x01, sizeof(hash));        // replace with SHA-256 of your tx

CW_SignRequest req(session,
                   CW_SIGN_CURR_K1,
                   CW_SIGN_SIG_ECDSA_LOW_S,
                   CW_SIGN_WITH_PIN);
req.hash       = hash;
req.hashLength = sizeof(hash);
CW_Utils::safe_memcpy(req.pin, sizeof(req.pin),
                      reinterpret_cast<const uint8_t*>(DEMO_PIN),
                      strlen(DEMO_PIN));
```

**Sign and handle the result:**

```cpp
CW_SignResult sig = wallet.sign(req);
if (sig.errorCode == CW_OK) {
    // sig.signature = r[32] || s[32] — raw, ready to forward / DER-encode
} else if (sig.errorCode == CW_SIGN_PIN_INCORRECT) {
    CW_Utils::secure_wipe(hash, sizeof(hash));
    CW_Utils::secure_wipe(sig.signature, sizeof(sig.signature));
    wallet.disconnect(session);
    vTaskDelay(portMAX_DELAY);            // protect the retry counter
}
```

**Wipe secrets** before the next iteration:

```cpp
CW_Utils::secure_wipe(hash, sizeof(hash));
CW_Utils::secure_wipe(sig.signature, sizeof(sig.signature));
wallet.disconnect(session);
```

`CW_Utils::secure_wipe` is a `volatile`-pointer memset that the
compiler cannot elide — required to keep secrets from lingering in
RAM.

## Error codes

| `errorCode` | Meaning | Action |
|-------------|---------|--------|
| `CW_OK` (`0x00`) | Signature OK | Use `sig.signature` |
| `CW_SIGN_NO_KEY_LOADED` (`0x81`) | Card has no seed | `cryptnox seed generate` |
| `CW_SIGN_PIN_INCORRECT` (`0x82`) | Wrong PIN | Halt — fix `DEMO_PIN` before re-running |
| other | Channel error / unexpected SW | Check the raw status word printed by the SDK |

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `Sign failed: 0x81` | No seed on card | `cryptnox seed generate` |
| `Sign failed: 0x82` | Wrong PIN | Edit `DEMO_PIN`, re-flash — do **not** keep retrying |
| `APDU exchange failed!` | NFC link dropped mid-exchange | Hold the card steady through the LED pulse |
| `Card not detected` | Card not on the antenna | Bring the card within ~1 cm of the antenna |

## License

`cryptnox-sdk-esp32` is dual-licensed:

- **LGPL-3.0** for open-source projects and proprietary projects that comply with LGPL requirements
- **Commercial license** for projects that require a proprietary license without LGPL obligations (see [COMMERCIAL.md](../../COMMERCIAL.md) for details)

For commercial inquiries, contact: contact@cryptnox.com
