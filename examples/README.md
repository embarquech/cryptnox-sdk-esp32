<div align="center">

<img src="https://github.com/user-attachments/assets/6ce54a27-8fb6-48e6-9d1f-da144f43425a"/>

### cryptnox-sdk-esp32

ESP32 SDK for managing Cryptnox smart card wallets

</div>

# Examples

Standalone ESP-IDF projects that exercise the Cryptnox Hardware Wallet
over NFC (PN532). Each project ships with its own focused README so a
reader landing on a single example gets everything needed end-to-end.

## Prerequisites

| Component | Details |
|-----------|---------|
| **Hardware Wallet** | Cryptnox Hardware Wallet (firmware ≥ v1.6.0), initialised with a PIN — and a seed loaded for the signing examples |
| **NFC reader** | [PN532 NFC module](https://www.nxp.com/products/PN532) wired over **SPI** (default) or **I²C** — see [hardware setup](../README.md#hardware-setup) |
| **Board** | [ESP32-S3-DevKitC-1](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html) (Espressif reference dev kit) |
| **Toolchain** | [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/index.html) installed and sourced (`. $IDF_PATH/export.sh`) |
| **Repository** | This repository **cloned with submodules** (`git clone --recurse-submodules …`) |

Provision a card from a host with a PC/SC reader and the
[Cryptnox CLI](https://github.com/cryptnox/cryptnox-cli):

```bash
cryptnox init           # sets the PIN + PUK
cryptnox seed generate        # generates a BIP39 seed (required for signing)
```

> [!NOTE]
> Every example starts Wi-Fi on boot so the radio feeds the ESP32
> hardware TRNG with full entropy. Even the non-networked examples
> (`Connect`, `VerifyPin`, `Sign`, `BasicUsage`) need a valid
> `WIFI_SSID` / `WIFI_PASSWORD` in their `main/config.h`.

## Available examples

| Example | What it does |
|---------|--------------|
| [Connect](Connect/README.md) | Opens the secure channel and reads back the card owner's name & email. **Safest first example** — no PIN, no signing, can't lock the card. |
| [VerifyPin](VerifyPin/README.md) | Opens the secure channel and submits a PIN. Halts on a wrong PIN to protect the on-card retry counter. |
| [Sign](Sign/README.md) | Signs a 32-byte hash with the card's secp256k1 key. Returns the raw `r ‖ s` signature ready to broadcast. |
| [BasicUsage](BasicUsage/README.md) | End-to-end walkthrough in one project: pick SPI **or** I²C, open the channel, sign a hash. Good reference for production wiring. |
| [UsdcSigning](UsdcSigning/README.md) | Real-world flow: build an EIP-1559 USDC transfer, sign it on the card, broadcast it on Sepolia. |

## How to build and run an example

From the project root (where `idf.py`'s parent `CMakeLists.txt` lives):

```bash
. $IDF_PATH/export.sh                       # source ESP-IDF env (once per shell)
cd examples/<ExampleName>
cp main/config.h main/config.h.bak          # if you want to keep the template
$EDITOR main/config.h                       # fill WIFI_SSID / WIFI_PASSWORD (+ extras for UsdcSigning)
idf.py set-target esp32s3                   # once, writes sdkconfig
idf.py build flash monitor                  # build, flash, open serial @ 115200 baud
```

Exit the monitor with `Ctrl-]`. Place the card on the PN532 antenna
when the firmware prompts for it.

> [!NOTE]
> All examples default to PIN `000000000` (nine zeros). If your card
> was initialised with a different PIN, edit the `DEFAULT_PIN` /
> `DEMO_PIN` / `CARD_PIN` macro at the top of the relevant source
> file before building.

## Adding a new example

Follow the conventions used by the existing projects:

- Place each project in its own subdirectory under `examples/`,
  named in **PascalCase**.
- Lay it out as an ESP-IDF project: `CMakeLists.txt` at the project
  root, a `main/` subdirectory with `main/CMakeLists.txt` + sources,
  and a `sdkconfig.defaults` capturing the project's build options.
- Start every source file with the SPDX + copyright header used by
  the rest of the repository.
- Add Doxygen tags (`@file`, `@example`, `@brief`) so the project
  surfaces correctly in the generated docs.
- If the project needs external secrets (Wi-Fi credentials, RPC keys,
  recipient addresses), ship a `config.template.h` next to it and add
  the runtime `config.h` to `.gitignore`. Never commit credentials.
- Ship a `README.md` next to the project and register it in the
  [Available examples](#available-examples) table above.

## License

`cryptnox-sdk-esp32` is dual-licensed:

- **LGPL-3.0** for open-source projects and proprietary projects that comply with LGPL requirements
- **Commercial license** for projects that require a proprietary license without LGPL obligations (see [COMMERCIAL.md](../COMMERCIAL.md) for details)

For commercial inquiries, contact: contact@cryptnox.com
