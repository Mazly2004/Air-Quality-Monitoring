# 3. Hardware Setup

## 3.1 Bill of materials

| Item | Notes |
|---|---|
| ESP32 dev board (e.g. ESP32‑WROOM‑32) | Any board supported by `esp32dev` in PlatformIO |
| ZPHS01B multi‑gas sensor | UART, 5 V power, 3.3 V logic tolerant on TX |
| 20×4 I²C LCD (PCF8574 backpack, addr `0x27`) | Optional but useful |
| USB‑UART cable / on‑board USB | For flashing |
| Jumper wires + breadboard | — |
| 5 V power supply (≥1 A) | ZPHS01B and ESP32 |

## 3.2 Wiring

### ESP32 ↔ ZPHS01B (UART2)

| ESP32 pin | ZPHS01B pin | Notes |
|---|---|---|
| `GPIO16` (RXD2) | TX | Sensor → ESP32 |
| `GPIO17` (TXD2) | RX | ESP32 → Sensor |
| `5V` | VCC | Power |
| `GND` | GND | Common ground |

Defined in firmware:

```c
#define RXD2 16
#define TXD2 17
// ...
Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
```

### ESP32 ↔ 20×4 I²C LCD

| ESP32 pin | LCD pin |
|---|---|
| `GPIO21` (SDA) | SDA |
| `GPIO22` (SCL) | SCL |
| `5V` | VCC |
| `GND` | GND |

I²C address used by the firmware: `0x27`. A bus scan runs in `setup()` so you’ll see the detected address in the serial monitor.

If your backpack reports `0x3F`, update this line in [src/main.cpp](../src/main.cpp):

```c
LiquidCrystal_I2C lcd(0x27, 20, 4);
```

## 3.3 ZPHS01B protocol

Q&A mode. Command sent every reading:

```
FF 01 86 00 00 00 00 00 79
```

Response: 25 bytes — `FF 86 [22 data bytes] [checksum]`.

| Parameter | Unit | Bytes | Conversion |
|---|---|---|---|
| PM1.0 | µg/m³ | 2–3 | `H<<8 | L` |
| PM2.5 | µg/m³ | 4–5 | `H<<8 | L` |
| PM10 | µg/m³ | 6–7 | `H<<8 | L` |
| CO₂ | ppm | 8–9 | `H<<8 | L` |
| TVOC | ppb | 10–11 | `H<<8 | L` |
| Temperature | °C | 12–13 | `((H<<8|L) − 400) / 10` |
| Humidity | %RH | 14–15 | `(H<<8|L) / 10` |
| HCHO | µg/m³ | 16–17 | `H<<8 | L` |
| CO | ppm | 18–19 | `H<<8 | L` |
| O₃ | ppb | 20–21 | `H<<8 | L` |
| NO₂ | ppb | 22–23 | `H<<8 | L` |

Checksum: `(~sum(bytes 1..23)) + 1` (one byte).

## 3.4 Current hardware status

> The real ZPHS01B sensor is **not currently responding** in our test rig. The firmware therefore runs in **mock data mode**: realistic but randomised values are emitted every 10 s. The real‑sensor code path remains in [src/main.cpp](../src/main.cpp) and can be re‑enabled (see [04-firmware-build-and-flash.md §4.6](04-firmware-build-and-flash.md#46-re-enabling-the-real-sensor)).
