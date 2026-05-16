# 1. Getting Started

## 1.1 Prerequisites

### On your development machine (macOS / Linux / Windows)

| Tool | Why | Install |
|---|---|---|
| [VS Code](https://code.visualstudio.com/) | Editor | — |
| [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension | Build & flash ESP32 firmware | VS Code marketplace |
| Python 3.9+ | Required by PlatformIO core | `brew install python` / system installer |
| Git | Source control | `brew install git` |
| USB serial driver | To talk to the ESP32 | CP210x / CH340 driver for your board |
| (Optional) `mosquitto-clients` | `mosquitto_sub` for debugging | `brew install mosquitto` |
| (Optional) `influx` CLI | Manual queries / exports | `brew install influxdb-cli` |

### On each Raspberry Pi (server hosts)

| Tool | Why |
|---|---|
| Raspberry Pi OS 64‑bit (Bookworm or later) | Base OS |
| Docker Engine + Compose plugin | Runs the stack |
| Static IP (or DHCP reservation) | The ESP32 connects to fixed IPs `172.20.10.10` / `172.20.10.11` |
| SSH enabled | Remote admin and `export_data.sh` |

Install Docker on Pi:

```bash
curl -fsSL https://get.docker.com | sudo sh
sudo usermod -aG docker $USER
# log out / log back in
docker --version
docker compose version
```

## 1.2 Network plan

The firmware ships with two hard‑coded broker IPs. Either:

- Match those IPs on your network (recommended for first run), **or**
- Edit them in [src/main.cpp](../src/main.cpp) before flashing.

| Device | IP | Ports |
|---|---|---|
| Raspberry Pi 1 (Primary) | `172.20.10.10` | `1883` MQTT, `8086` InfluxDB, `3000` Grafana |
| Raspberry Pi 2 (Backup)  | `172.20.10.11` | `1883` MQTT, `8086` InfluxDB |
| Research server (optional) | DHCP | `5432`, `5050`, `3001` |
| ESP32 | DHCP | publishes only |

All devices must be on the **same Wi‑Fi SSID** (default in firmware: `Mazly`).

## 1.3 Repository layout

```
AQI_Project_Sprint/
├── src/main.cpp                          # ESP32 firmware (Arduino/PlatformIO)
├── platformio.ini                        # Build config (board, libs, ports)
├── server/
│   ├── docker-compose.yml                # Primary Pi stack
│   ├── docker-compose-backup.yml         # Backup Pi stack
│   ├── docker-compose-timescaledb.yml    # Research stack
│   ├── mosquitto/config/mosquitto.conf   # MQTT broker config
│   ├── telegraf/telegraf.conf            # MQTT → InfluxDB bridge
│   ├── timescaledb/init.sql              # Research DB schema
│   ├── timescaledb/telegraf.conf         # Telegraf for research stack
│   └── grafana/
│       ├── provisioning/datasources/influxdb.yml
│       ├── provisioning/dashboards/provider.yml
│       └── dashboards/                   # JSON dashboards
├── export_data.sh                        # Pull CSV from Pi’s InfluxDB
├── docs/                                 # ← you are here
├── MERMAID_CODE                          # Architecture diagram
└── failover_mermaidcode                  # Failover sequence diagram
```

## 1.4 Cloning

```bash
git clone <your-repo-url> AQI_Project_Sprint
cd AQI_Project_Sprint
```

Next: read [02-architecture.md](02-architecture.md), or skip straight to [05-server-primary.md](05-server-primary.md) to bring up Pi 1.
