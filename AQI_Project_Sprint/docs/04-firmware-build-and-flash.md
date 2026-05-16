# 4. Firmware: Build, Configure & Flash

The firmware lives in [src/main.cpp](../src/main.cpp). Build system is **PlatformIO** ([platformio.ini](../platformio.ini)).

## 4.1 Configure the build target

Open [platformio.ini](../platformio.ini) and set the serial port for your machine:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
upload_speed = 115200
monitor_speed = 115200

; macOS / Linux example:
upload_port  = /dev/cu.usbserial-0001
monitor_port = /dev/cu.usbserial-0001

; Windows example:
; upload_port  = COM5
; monitor_port = COM5

lib_deps =
    marcoschwartz/LiquidCrystal_I2C@^1.1.4
    knolleary/PubSubClient@^2.8
```

Find the port:

- **macOS / Linux:** `ls /dev/cu.usb*` (macOS) or `ls /dev/ttyUSB*` (Linux)
- **Windows:** Device Manager → Ports (COM & LPT)

## 4.2 Configure firmware constants

Edit the top of [src/main.cpp](../src/main.cpp):

```c
// Wi‑Fi
const char *WIFI_SSID     = "Mazly";
const char *WIFI_PASSWORD = "oliver12345";

// MQTT brokers (must be reachable from the ESP32's Wi‑Fi)
const char *MQTT_PRIMARY_SERVER   = "172.20.10.10";
const int   MQTT_PRIMARY_PORT     = 1883;
const char *MQTT_SECONDARY_SERVER = "172.20.10.11";
const int   MQTT_SECONDARY_PORT   = 1883;

const char *MQTT_TOPIC = "sensors/esp32_01";
```

> ⚠️ Don’t commit real Wi‑Fi passwords. Consider moving them to `include/secrets.h` and adding it to `.gitignore` — see [10-security-and-secrets.md](10-security-and-secrets.md).

## 4.3 Build

From the project root:

```bash
pio run
```

PlatformIO will download the ESP32 toolchain, install `LiquidCrystal_I2C` and `PubSubClient`, and produce a firmware binary in `.pio/build/esp32dev/`.

## 4.4 Flash

```bash
pio run --target upload
```

If upload fails:

- Hold the **BOOT** button on the ESP32 while upload starts.
- Lower the upload speed in `platformio.ini` (`upload_speed = 460800` or `115200`).
- Verify cable supports data (some USB cables are power‑only).

## 4.5 Serial monitor

```bash
pio device monitor
```

On boot you should see:

```
=== Connecting to WiFi ===
SSID: Mazly
✓ WiFi Connected!
IP Address: 192.168.x.x
MQTT Primary: 172.20.10.10:1883, Backup: 172.20.10.11:1883
...
Trying primary MQTT broker...
✓ MQTT connected to PRIMARY broker
```

The LCD shows Wi‑Fi state, the assigned IP, then MQTT broker status.

## 4.6 Re‑enabling the real sensor

The default `loop()` publishes **mock** data every 10 s. To use the actual ZPHS01B:

1. Open [src/main.cpp](../src/main.cpp) and find the `loop()` function.
2. Comment out the mock block guarded by `lastSend`.
3. Uncomment the block that reads `Serial2.available() >= 25` and calls `parseZPHS01B()`.
4. Ensure `requestSensorData()` is called at the desired cadence (every 10 s is fine).
5. Rebuild and re‑flash.

## 4.7 Pinning a known‑good toolchain (optional)

PlatformIO updates `espressif32` frequently. To pin:

```ini
platform = espressif32@6.7.0
```

## 4.8 Troubleshooting build/flash

| Symptom | Likely cause | Fix |
|---|---|---|
| `A fatal error occurred: Failed to connect to ESP32` | Bootloader not entered | Hold BOOT while flashing; check cable |
| Port not listed | Driver missing | Install CP210x or CH340 driver |
| `Wi‑Fi Failed!` on LCD | Wrong SSID/password | Re‑check creds; 2.4 GHz only |
| LCD shows nothing | Wrong I²C address | Read the bus scan output; update `lcd(0x27, …)` |
| MQTT never connects | Brokers unreachable | Ping the Pi from your phone on the same Wi‑Fi; check firewall |
