# AQI Project Documentation

Complete documentation for the **Air Quality Monitoring (AQM)** system: ESP32 + ZPHS01B sensor → MQTT → InfluxDB / TimescaleDB → Grafana, with high-availability failover across two Raspberry Pis.

## Documentation Index

| # | Document | What it covers |
|---|---|---|
| 1 | [01-getting-started.md](01-getting-started.md) | Prerequisites, repo layout, quick start |
| 2 | [02-architecture.md](02-architecture.md) | System architecture, data flow, failover behaviour |
| 3 | [03-hardware-setup.md](03-hardware-setup.md) | Wiring, I²C addresses, sensor protocol |
| 4 | [04-firmware-build-and-flash.md](04-firmware-build-and-flash.md) | PlatformIO build, configuration, flashing the ESP32 |
| 5 | [05-server-primary.md](05-server-primary.md) | Bringing up the primary Raspberry Pi stack |
| 6 | [06-server-backup.md](06-server-backup.md) | Bringing up the backup Raspberry Pi stack |
| 7 | [07-grafana.md](07-grafana.md) | Grafana datasource, login, and dashboards |
| 8 | [08-research-timescaledb.md](08-research-timescaledb.md) | Optional research/comparison stack on TimescaleDB |
| 9 | [09-operations.md](09-operations.md) | Day‑2 ops: logs, exports, failover testing, troubleshooting |
| 10 | [10-security-and-secrets.md](10-security-and-secrets.md) | Tokens, passwords, what to change before going public |

## TL;DR — End‑to‑End Bring‑Up

1. **Pi 1 (primary):** install Docker, copy `server/`, run `docker compose up -d` ([05](05-server-primary.md)).
2. **Pi 2 (backup):** same, but use `docker-compose-backup.yml` ([06](06-server-backup.md)).
3. **InfluxDB:** open `http://172.20.10.10:8086`, complete first‑run setup, copy the API token into `server/telegraf/telegraf.conf` and `server/grafana/provisioning/datasources/influxdb.yml` ([05](05-server-primary.md#3-first-run-influxdb-setup)).
4. **ESP32:** set Wi‑Fi creds and serial port in `src/main.cpp` and `platformio.ini`, then `pio run -t upload` ([04](04-firmware-build-and-flash.md)).
5. **Grafana:** open `http://172.20.10.10:3000` (admin / admin), verify the InfluxDB datasource, build a dashboard ([07](07-grafana.md)).
6. **Verify:** subscribe to MQTT and check Influx for `air_quality` measurement ([09](09-operations.md#verification)).
