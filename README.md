# cryptnox-sdk-esp32

ESP32-S3 project with PN532 NFC reader and WeAct Studio 4.2" e-paper display, built with ESP-IDF v5.5.

## Hardware

- **Board**: Keyestudio ESP32-S3 Pro (ESP32-S3-N16R8)
- **NFC**: PN532 module (Keyestudio HAT, Arduino R3 form factor)
- **Display**: WeAct Studio 4.2" e-paper (400x300, SSD1683)

## Pinout

Both peripherals share the same SPI bus (SPI2_HOST).

### SPI Bus (shared)

| Signal | ESP32-S3 GPIO | Arduino Header |
|--------|---------------|----------------|
| MOSI   | IO11          | D11            |
| SCLK   | IO12          | D13            |
| MISO   | IO13          | D12            |

> **Note:** GPIO 12 and 13 are swapped on the Keyestudio ESP32-S3 Pro Arduino header (D12 = IO13, D13 = IO12).

### PN532 (HAT)

| Signal | ESP32-S3 GPIO | Arduino Header |
|--------|---------------|----------------|
| CS     | IO10          | D10            |
| MOSI   | IO11          | D11            |
| MISO   | IO13          | D12            |
| SCK    | IO12          | D13            |

The PN532 HAT plugs directly into the Arduino R3 header. Set the HAT switches to **SPI mode**.

### WeAct 4.2" E-Paper

| E-Paper Pin | ESP32-S3 GPIO |
|-------------|---------------|
| VCC         | 3.3V          |
| GND         | GND           |
| SDA (MOSI)  | IO11          |
| SCL (SCLK)  | IO12          |
| CS          | IO38          |
| DC          | IO40          |
| RES (RST)   | IO41          |
| BUSY        | IO42          |

## Build

Requires ESP-IDF v5.5. Target is set to ESP32-S3 via `sdkconfig.defaults`.

```bash
idf.py build
idf.py flash monitor
```

Or use the ESP-IDF VS Code extension: **Build, Flash and Monitor**.
