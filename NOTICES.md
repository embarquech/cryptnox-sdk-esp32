# Third-party notices

`cryptnox-sdk-esp32` is distributed under a dual LGPL-3.0 / commercial
license (see [README.md](README.md) and `LICENSE` for details). Portions
of this codebase derive from or include third-party software released
under separate, permissive licenses. The original copyright notices and
license texts are reproduced below as required by those licenses.

---

## components/pn532/ — Adafruit_PN532 (BSD-3-Clause)

The PN532 driver in `components/pn532/pn532.c` and
`components/pn532/include/pn532.h` is derived from the Adafruit_PN532
Arduino library. The frame layout, command codes (`PN532_PREAMBLE`,
`PN532_STARTCODE2`, `PN532_HOSTTOPN532`, `PN532_INDATAEXCHANGE`, …),
SAMConfig / InListPassiveTarget / InDataExchange sequences and ACK
frame match the upstream Adafruit driver. Significant additions on top
include the I²C transport (via the IDF v5.x i2c_master API), the
extended-frame (8-byte header) parser for responses larger than 255 B,
and several ESP-IDF-specific concurrency / timeout adaptations.

Upstream: https://github.com/adafruit/Adafruit-PN532

```
Software License Agreement (BSD License)

Copyright (c) 2012, Adafruit Industries
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holders nor the
   names of its contributors may be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

If the I²C path was cross-referenced against the Seeed Studio PN532
fork (https://github.com/Seeed-Studio/PN532, MIT-licensed), that work
is hereby also acknowledged.

---

## components/epd/ — WeActStudio EpaperModule

`components/epd/epd.c`, the EPD font tables and the public header are
ported from WeAct Studio's e-paper module sample code (Raspberry Pi
reference implementation). The original is published on the WeAct
Studio GitHub organisation and Taobao/AliExpress storefronts.

Upstream: https://github.com/WeActStudio/WeActStudio.EpaperModule

The upstream repository does not carry an explicit `LICENSE` file at
the time of porting; the sample code is published as a reference for
buyers of WeAct Studio's e-paper modules. Until WeAct Studio publishes
an explicit license, downstream commercial redistribution of these
files should be cleared with WeAct Studio.

---

## examples/usdc_signing/main/keccak256.{cpp,h} — Keccak-f[1600] reference algorithm

The Keccak (SHA-3) sponge construction is a public-domain algorithm
designed by Bertoni, Daemen, Peeters and Van Assche. The file
`examples/usdc_signing/main/keccak256.cpp` is a mechanical
implementation written for this project from the FIPS 202 specification;
it does not derive from any specific implementation under copyright.
The round-constant table `kRC[]` and rotation offsets in `kROT[]` are
the canonical constants from the Keccak specification.

---

## Espressif and LVGL components

Components fetched from the ESP Component Registry (`espressif/*`,
`lvgl/lvgl`, `atanisoft/esp_lcd_touch_xpt2046`, etc.) retain their
original licenses as shipped. See each component's `LICENSE` /
`license.txt` inside `managed_components/`.

---

## cryptnox-sdk-cpp (submodule)

The core C++ SDK is included as a git submodule at `cryptnox-sdk-cpp/`
and carries its own dual LGPL-3.0 / commercial license. See that
repository's `README.md` and `COMMERCIAL.md` for terms.
