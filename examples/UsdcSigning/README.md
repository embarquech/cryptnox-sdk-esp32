<div align="center">

<img src="https://github.com/user-attachments/assets/6ce54a27-8fb6-48e6-9d1f-da144f43425a"/>

### cryptnox-sdk-esp32

ESP32 SDK for managing Cryptnox smart card wallets

</div>

# UsdcSigning — Broadcast a Real EIP-1559 USDC Transfer on Sepolia

End-to-end demonstration of the Cryptnox Hardware Wallet on an
ESP32-S3: connect to a Wi-Fi access point, fetch the nonce and fee
parameters over JSON-RPC, build and Keccak-256-hash the unsigned
EIP-1559 transaction, sign it on the card
(BIP-44 `m/44'/60'/0'/0/0`, secp256k1, canonical low-S), recover
`yParity`, broadcast the signed transaction, and print the on-chain
tx hash.

The private key never leaves the card. The ESP32 only ever holds the
`Account` derived from the card's public key, the unsigned tx hash,
and the resulting `(r, s)`.

> [!WARNING]
> Every wrong PIN attempt decrements an on-card retry counter. At
> zero the PIN is permanently blocked. The firmware halts on
> `CW_SIGN_PIN_INCORRECT` — verify `CARD_PIN` in `main/config.h`
> matches the value used during `cryptnox init` **before** flashing.

## Requirements

| Component | Details |
|-----------|---------|
| **Hardware Wallet** | Cryptnox Hardware Wallet, initialised **and** seeded |
| **Wallet funding** | The address derived from `m/44'/60'/0'/0/0` on the card needs Sepolia ETH (for gas) and Sepolia USDC. Faucets: [Sepolia ETH](https://sepoliafaucet.com/) · [Circle USDC](https://faucet.circle.com/) |
| **NFC reader** | PN532 over **SPI** or **I²C** — selected by `SPI_ENABLED` / `I2C_ENABLED` at the top of `main.cpp`; see [hardware setup](../../README.md#hardware-setup) |
| **Board** | ESP32-S3-DevKitC-1 (or any ESP32 family — Wi-Fi required) |
| **Toolchain** | ESP-IDF v5.5 |
| **RPC endpoint** | A Sepolia JSON-RPC endpoint — [PublicNode](https://ethereum-sepolia-rpc.publicnode.com) (no signup, default) or [Infura](https://app.infura.io/) |

## Quick start

1. **Create `main/config.h`** by copying the template:

   ```bash
   cp config.template.h main/config.h
   ```

   Fill in at minimum: `WIFI_SSID`, `WIFI_PASSWORD`, `CARD_PIN`,
   `ADDR_FROM` (the address derived from `m/44'/60'/0'/0/0` on the
   card), `ADDR_TO` (recipient), `AMOUNT_USDC` (token base units —
   1 USDC = 1 000 000).

   > [!IMPORTANT]
   > `main/config.h` is gitignored — never commit it.

2. Build, flash and monitor:

   ```bash
   cd examples/UsdcSigning
   idf.py set-target esp32s3                # once
   idf.py build flash monitor
   ```

3. Exit the monitor with `Ctrl-]`. Place the card on the PN532
   antenna when the firmware prompts for it.

### Expected output

```
I (1234) usdc_signing: Wi-Fi connected, TRNG seeded
I (1290) usdc_signing: PN532 ready
I (1340) usdc_signing: fetchNonce: status=200 nonce=0x5
I (1342) usdc_signing: [HASH]: 0x6679a2cd3064046397addbb97004b606df9281f624409fd36d2d24832db59c29
I (1500) usdc_signing: Card connected, secure channel established
I (1740) usdc_signing: [SIG r]: 7C1F3A925E0B8CD4920FAB7C53E1B9D81F2A4C76E9D805BC5D2EAD12C3FA47CB
I (1741) usdc_signing: [SIG s]: 12E0BC4FA7880967DA1F0EBD83C2B791580AC4D62E16039F7B4CDDF13E2A89C5
I (2030) usdc_signing: yParity: 1
I (2031) usdc_signing: Sending...
I (2350) usdc_signing: [RPC] HTTP 200
I (2351) usdc_signing: [tx] hash=0xab12cd34ef56...
I (2351) usdc_signing: Transaction sent successfully!
```

Paste the final `[tx] hash` into
[Sepolia Etherscan](https://sepolia.etherscan.io/) to watch
confirmation.

## How it works

```
 1. wifi_init() + esp_wifi_connect()
        │
 2. eth_rpc_fetch_nonce()              POST eth_getTransactionCount
        │                              over TLS via esp_http_client
        │
 3. Build Tx2 struct                   chainId=11155111, nonce, fees,
        │                              gasLimit, to, value=0, data
        │
 4. eth_rlp_encode_erc20_transfer()    0xa9059cbb || pad(to,32) ||
        │                              pad(AMOUNT_USDC,32)
        │
 5. eth_rlp_encode_unsigned_tx()       0x02 || rlp([chainId, nonce,
        │                              ..., [] accessList])
        │
 6. keccak256(rlp, len, hash)          EIP-2718 typed-tx pre-image (32 B)
        │
 7. wallet.connect(session)            Secure channel
        │
 8. Build CW_SignRequest:
      keyType   = CW_SIGN_DERIVE_K1    (BIP-44 m/44'/60'/0'/0/0)
      sigType   = CW_SIGN_SIG_ECDSA_LOW_S
      pinMode   = CW_SIGN_WITH_PIN
      hash      = keccak256(unsigned tx)
      pin       = CARD_PIN
        │
 9. wallet.sign(req)                   64-byte r || s
        │
10. eth_rpc_ecrecover_parity(...)      Call the ecrecover precompile
        │                              with v=27, then v=28 — keep the
        │                              one that returns ADDR_FROM
        │
11. eth_rlp_encode_signed_tx()         Same RLP plus the signature fields
        │
12. eth_rpc_send_raw_tx()              eth_sendRawTransaction → extract
        │                              "result":"0x…" tx hash
        │
13. secure_wipe(req.pin, ...)          Zero the PIN buffer
```

### TLS

The ESP-IDF HTTP client uses the bundled certificate bundle (set via
`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` in `sdkconfig.defaults`) so the
RPC connection is validated against the same root store ESP-IDF
applications use by default. No per-provider PEM is required as long
as the endpoint chains to one of the included roots.

### `yParity` recovery

EIP-1559 signatures carry a 1-bit `yParity` instead of EIP-155's
`v`. The card returns only `r` and `s`, so the firmware calls the
[`ecrecover` precompile](https://www.evm.codes/precompiled?fork=cancun)
at address `0x01` with `v = 27` (yParity = 0). If the recovered
address matches `ADDR_FROM` the firmware keeps yParity = 0;
otherwise it retries with `v = 28` (yParity = 1). One of the two
will match when the card's key and `ADDR_FROM` are consistent.

## Configuration reference

All fields live in `main/config.h` (gitignored). Start from
[`config.template.h`](config.template.h) and fill in:

### Wi-Fi

| Field | Required | Example |
|-------|:--------:|---------|
| `WIFI_SSID` | yes | `"MyHomeNetwork"` |
| `WIFI_PASSWORD` | yes | `"password"` |

> [!NOTE]
> The ESP32-S3 (and every Wi-Fi-capable ESP32 family member) supports
> **2.4 GHz** Wi-Fi only.

### RPC endpoint — choose one provider

**PublicNode** (free, no signup, default):

```c
#define RPC_URL    "https://ethereum-sepolia-rpc.publicnode.com"
```

**Infura** (free tier, requires API key):

```c
#define RPC_URL    "https://sepolia.infura.io/v3/<YOUR_INFURA_PROJECT_ID>"
```

### Wallet

| Field | Example |
|-------|---------|
| `CARD_PIN` | `"000000000"` (must match `cryptnox init`) |
| `ADDR_FROM` | `"4aadf6f331aea39e699db17c75a4b12d993956d2"` (lowercase hex, no `0x`) |

`ADDR_FROM` must equal the address derived from `m/44'/60'/0'/0/0`
on the card. If they disagree, the `yParity` recovery loop cannot
find a matching value and the firmware halts.

### Transaction

| Field | Default | Notes |
|-------|---------|-------|
| `ADDR_TO` | `"Cd7E5...c06e"` | Recipient address (hex, no `0x`) |
| `ADDR_USDC` | `"1c7D4...7238"` | USDC contract on Sepolia |
| `CHAIN_ID_SEPOLIA` | `11155111` | Hardcoded — no path to broadcast on mainnet by accident |
| `AMOUNT_USDC` | `1000000UL` | 1 USDC (6 decimals) |
| `MAX_PRIORITY_FEE` | `2000000000ULL` | 2 Gwei |
| `MAX_FEE` | `4000000000ULL` | 4 Gwei |
| `GAS_LIMIT_ERC20` | `60000ULL` | Standard ERC-20 transfer |

## Memory footprint

After `idf.py build`:

| Section | Size |
|---------|-----:|
| Flash (`.text` + `.rodata`) | ~900 KB |
| RAM at runtime | ~80 KB |

The bulk of the flash is the IDF Wi-Fi stack, mbedTLS, and the
bundled CA store (~250 KB). The Cryptnox SDK itself plus the
example's `eth_rlp` / `eth_rpc` / `keccak256` helpers sit around
~50 KB.

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `esp-tls: mbedtls_ssl_handshake returned -0x2700` | RPC's chain not in the bundled root store | Switch to PublicNode (Google Trust Services R4 is bundled) or add the custom root via the certificate-bundle component |
| Wi-Fi never connects | Wrong SSID / password, or a 5 GHz-only network | ESP32-S3 is 2.4 GHz only — verify `WIFI_SSID` / `WIFI_PASSWORD` |
| `tx.to is not a valid 40-char hex string` | Bad hex in `ADDR_TO` | 40 hex chars, no `0x`, lowercase preferred |
| `yParity determination failed!` | `ADDR_FROM` doesn't match the card's m/44'/60'/0'/0/0 address | Derive the address with the [Cryptnox CLI](https://github.com/cryptnox/cryptnox-cli) and copy it into `ADDR_FROM` |
| Tx broadcast OK but `tefPAST_NONCE` on Etherscan | Stale nonce from the RPC | Wait a few seconds and re-run; the firmware fetches the nonce just before signing |
| `Wrong PIN — halting to protect retry counter` | `CARD_PIN` does not match the card | Fix `CARD_PIN` — do **not** keep retrying, every attempt burns one of the on-card tries (see [VerifyPin](../VerifyPin/README.md)) |
| `Sign failed: 0x81` | Card has no seed | `cryptnox seed generate` |

## License

`cryptnox-sdk-esp32` is dual-licensed:

- **LGPL-3.0** for open-source projects and proprietary projects that comply with LGPL requirements
- **Commercial license** for projects that require a proprietary license without LGPL obligations (see [COMMERCIAL.md](../../COMMERCIAL.md) for details)

For commercial inquiries, contact: contact@cryptnox.com
