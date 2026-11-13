<div align="center">

<img src="https://github.com/user-attachments/assets/6ce54a27-8fb6-48e6-9d1f-da144f43425a"/>

### cryptnox-sdk-esp32

ESP32 SDK for managing Cryptnox smart card wallets

</div>

# TronSigning — Broadcast a Real TRX Transfer on TRON

End-to-end demonstration of the Cryptnox Hardware Wallet on an
ESP32-S3: connect to Wi-Fi, ask a TRON full node to build an unsigned
`TransferContract`, **verify locally that the returned transaction is
the one we asked for**, sign its `txID` on the card
(BIP-44 `m/44'/195'/0'/0/0`, secp256k1, canonical low-S), and
broadcast `r ‖ s ‖ v`.

The private key never leaves the card. The ESP32 only holds the
configured addresses, the verified 32-byte digest, and the resulting
`(r, s)`.

> [!NOTE]
> `config.template.h` points at the **Nile testnet**, so the default
> build moves test TRX only. The firmware signs and broadcasts a
> transfer on **every card tap**, in a loop with a 15 s pause — keep
> that in mind before switching `TRON_NODE_URL` to mainnet, where each
> tap spends real TRX.

> [!WARNING]
> Every wrong PIN attempt decrements an on-card retry counter. At
> zero the PIN is permanently blocked. Verify `CARD_PIN` in
> `main/config.h` matches the value used during `cryptnox init`
> **before** flashing.

## Requirements

| Component | Details |
|-----------|---------|
| **Hardware Wallet** | Cryptnox Hardware Wallet, initialised **and** seeded |
| **Wallet funding** | The address derived from `m/44'/195'/0'/0/0` on the card needs test TRX. Faucet: [nileex.io/join/getJoinPage](https://nileex.io/join/getJoinPage) — once per 24 h per wallet/IP. Fallback: `!nile <address>` to TronFAQBot in the [TRON dev Telegram](https://t.me/TronOfficialDevelopersGroupEn) / [Discord](https://discord.com/invite/hqKvyAM) |
| **NFC reader** | PN532 over **SPI** or **I²C** — selected by `SPI_ENABLED` / `I2C_ENABLED` at the top of `main.cpp`; see [hardware setup](../../README.md#hardware-setup) |
| **Board** | ESP32-S3-DevKitC-1 (or any ESP32 family — Wi-Fi required) |
| **Toolchain** | ESP-IDF v5.5 |
| **Node endpoint** | A TRON full node HTTP API — [TronGrid Nile](https://nile.trongrid.io) (no signup, default) or `https://api.trongrid.io` for mainnet |

## Quick start

1. **Create `main/config.h`** by copying the template:

   ```bash
   cp config.template.h main/config.h
   ```

   Fill in at minimum: `WIFI_SSID`, `WIFI_PASSWORD`, `CARD_PIN`,
   `TRON_ADDR_FROM` (your card's address at `m/44'/195'/0'/0/0` —
   derive it from that path's public key with
   `CW_Tron::addressFromPublicKey()`), `TRON_ADDR_TO` (recipient),
   `AMOUNT_SUN` (1 TRX = 1 000 000 SUN).

   > [!IMPORTANT]
   > `main/config.h` is gitignored — never commit it.

2. Build, flash and monitor:

   ```bash
   cd examples/TronSigning
   idf.py set-target esp32s3                # once
   idf.py build flash monitor
   ```

3. Exit the monitor with `Ctrl-]`. Place the card on the PN532
   antenna when the firmware prompts for it.

### Expected output

```
I (1234) tron_api: WiFi connected
I (1290) tron_signing: Ready — will send 1000000 SUN from TJ… to TW… on each card tap
I (1500) tron_signing: Hold Cryptnox card to reader to sign...
I (2100) tron_signing: Verified txID / hash to sign:
I (2101) tron_signing: 6f 3a … 91
I (2600) tron_signing: TX broadcast OK (v=0): {"result":true,"txid":"6f3a…91"}
```

Paste the `txid` into [Nile Tronscan](https://nile.tronscan.org/) — or
[Tronscan](https://tronscan.org/) when running against mainnet — to
watch confirmation.

## How it works

```
 1. tron_api_wifi_connect()             Wi-Fi up (also seeds the TRNG)
        │
 2. CW_Tron::decodeAddress()            Validate TRON_ADDR_FROM / _TO
        │                               (Base58Check + 0x41 prefix)
        │
 3. wallet.connect(session)             Secure channel on card tap
        │
 4. POST /wallet/createtransaction      Node returns txID, raw_data,
        │                               raw_data_hex
        │
 5. verify_transaction()                sha256(raw_data) == txID, and
        │                               raw_data carries the expected
        │                               sender, recipient and amount
        │
 6. Build CW_SignRequest:
      keyType = CW_SIGN_DERIVE_K1       CW_TRON_DERIVE_PATH
      sigType = CW_SIGN_SIG_ECDSA_LOW_S
      hash    = txID
        │
 7. wallet.sign(req)                    64-byte r || s
        │
 8. POST /wallet/broadcasttransaction    r || s || v, v=0 then v=1
```

### Why it is safe to let the node serialise the transaction

TRON transactions are protobuf, and the full node will happily build
one for you — which means a compromised or buggy endpoint could hand
back a transfer to a *different* address. Step 5 closes that hole
before the SIGN APDU is ever sent:

- `txID` must equal `sha256(raw_data_hex)` — so the digest the card
  signs is provably the digest of the bytes we inspected.
- `raw_data` must contain `0x0A 0x15 ‖ owner`, `0x12 0x15 ‖ to`
  (the `TransferContract` address fields) and `0x18 ‖ varint(amount)`.

Both checks run against the decoded values from `config.h`, so a
tampered transaction is rejected locally and the card never sees it.
This is a targeted field check rather than a full protobuf decoder —
sufficient for native TRX transfers, where each field is pinned by its
tag and length prefix.

### Signature parity (`v`)

TRON, like Ethereum, needs the recovery id appended to `r ‖ s`.
Recovering it requires an `ecrecover`, which no adapter interface
provides, so the firmware submits `v = 0` and falls back to `v = 1`.
A wrong parity recovers a different account, the node answers
`SIGERROR`, and nothing is committed — so the retry is safe.

### TLS

The ESP-IDF HTTP client uses the bundled certificate bundle (set via
`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` in `sdkconfig.defaults`), so the
node connection is validated against the default ESP-IDF root store.

## Configuration reference

All fields live in `main/config.h` (gitignored). Start from
[`config.template.h`](config.template.h):

| Field | Default | Notes |
|-------|---------|-------|
| `WIFI_SSID` / `WIFI_PASSWORD` | — | 2.4 GHz networks only |
| `TRON_NODE_URL` | `https://nile.trongrid.io` | Nile testnet. Use `https://api.trongrid.io` for mainnet — real TRX |
| `TRON_API_KEY` | `""` | Optional TronGrid key; empty sends no `TRON-PRO-API-KEY` header |
| `CARD_PIN` | — | Must match `cryptnox init` |
| `TRON_ADDR_FROM` | — | Card's address at `m/44'/195'/0'/0/0` (34 chars, starts with `T`) |
| `TRON_ADDR_TO` | — | Recipient |
| `AMOUNT_SUN` | `1000000ULL` | 1 TRX (6 decimals) |

## Memory footprint

After `idf.py build` (ESP-IDF v5.5, ESP32-S3):

| Section | Size |
|---------|-----:|
| `TronSigning.bin` | ~992 KB (34 % of the 1.5 MB app partition free) |

The bulk is the IDF Wi-Fi stack, mbedTLS, and the bundled CA store. The
Cryptnox SDK plus `CW_Tron` and this example's `tron_api` / `tron_verify`
helpers account for a few tens of KB. `sdkconfig.defaults` selects
`CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE` — the default 1 MB app
partition leaves only 3 % free.

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `TRON_ADDR_FROM is not a valid TRON address` | Typo — Base58Check checksum or `0x41` prefix failed | Copy the address again; it must be 34 chars starting with `T` |
| `createtransaction failed` | Node rejected the request (unfunded / unknown account, rate limit) | Fund the sender; set `TRON_API_KEY` if rate-limited |
| `txID does not match sha256(raw_data)` | Node response inconsistent or truncated | Raise `TX_JSON_SIZE` in `main.cpp`; retry against another endpoint |
| `raw_data: recipient address mismatch` | Node built a different transfer than requested | Do **not** bypass the check — verify `TRON_ADDR_TO` and the endpoint |
| `TX broadcast failed with both parity bits` | `TRON_ADDR_FROM` isn't the card's `m/44'/195'/0'/0/0` address | Re-derive it from that path's public key (`CW_Tron::addressFromPublicKey()`) and copy the result |
| `CONTRACT_VALIDATE_ERROR … balance is not sufficient` | Sender unfunded, or the recipient is a fresh account (activation costs ~1 TRX extra) | Top up the sender, or send to an already-activated address |
| `Sign failed: 0x81` | Card has no seed | `cryptnox seed generate` |
| `Wrong PIN` | `CARD_PIN` mismatch | Fix `CARD_PIN` — every attempt burns an on-card try (see [VerifyPin](../VerifyPin/README.md)) |

## License

`cryptnox-sdk-esp32` is dual-licensed:

- **LGPL-3.0** for open-source projects and proprietary projects that comply with LGPL requirements
- **Commercial license** for projects that require a proprietary license without LGPL obligations (see [COMMERCIAL.md](../../COMMERCIAL.md) for details)

For commercial inquiries, contact: contact@cryptnox.com
