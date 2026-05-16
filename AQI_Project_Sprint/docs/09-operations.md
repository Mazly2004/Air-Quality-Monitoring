# 9. Operations & Troubleshooting

## 9.1 Verification checklist

Run after any change, top to bottom — stop at the first failure.

| # | Check | Command / UI |
|---|---|---|
| 1 | Pi reachable | `ping 172.20.10.10` |
| 2 | Containers up | `docker compose ps` on Pi |
| 3 | MQTT broker accepts subs | `mosquitto_sub -h 172.20.10.10 -t 'sensors/#' -v` |
| 4 | ESP32 publishing | Serial monitor shows `Publishing JSON: …` |
| 5 | Telegraf consuming | `docker logs telegraf | grep -i mqtt` |
| 6 | InfluxDB has rows | UI query: `from(bucket:"mazly") |> range(start: -5m)` |
| 7 | Grafana datasource | **Connections → Data sources → InfluxDB → Save & Test** |
| 8 | Dashboard renders | Grafana dashboard shows live values |
| 9 | Failover works | Stop `mosquitto` on Pi 1; data arrives on Pi 2 within ~15 s |
| 10 | Recovery works | Start `mosquitto` on Pi 1; ESP32 reconnects within ~60 s |

## 9.2 Logs

```bash
# all primary services
ssh pi@172.20.10.10 'cd ~/aqm && docker compose logs -f --tail=200'

# single service
docker logs -f mosquitto
docker logs -f telegraf
docker logs -f influxdb
docker logs -f grafana

# backup pi
ssh pi@172.20.10.11 'cd ~/aqm && docker compose -f docker-compose-backup.yml logs -f --tail=200'
```

ESP32 logs come from the serial monitor (`pio device monitor`).

## 9.3 Exporting data

A helper script is provided: [export_data.sh](../export_data.sh).

```bash
# last 30 days (default)
./export_data.sh

# custom range
./export_data.sh -7d
./export_data.sh -2024-01-01T00:00:00Z
```

It SSHes into Pi 1, runs `influx query` inside the container, pivots fields into columns, and writes a CSV to `~/Desktop/air_quality_<timestamp>.csv`.

Before running:

- Update `PI_USER`, `PI_HOST`, `INFLUX_TOKEN`, `INFLUX_ORG`, `INFLUX_BUCKET` in the script.
- Make sure SSH password auth is allowed on the Pi (or remove the `-o PreferredAuthentications=password` and use keys).

## 9.4 Backups

| What | How |
|---|---|
| InfluxDB | `docker exec influxdb influx backup /tmp/backup --org Secondspark --token <admin token>` then `docker cp influxdb:/tmp/backup ./backup-YYYYMMDD` |
| Grafana | Volume `grafana-storage` — `docker run --rm -v grafana-storage:/data -v $PWD:/out alpine tar czf /out/grafana-backup.tgz -C /data .` |
| TimescaleDB | `docker exec timescaledb_research pg_dump -U aqm_user airquality_research > research-YYYYMMDD.sql` |

Store backups off‑device.

## 9.5 Common failures

| Symptom | Likely cause | Fix |
|---|---|---|
| Telegraf logs `Network Error: dial tcp: lookup mosquitto: no such host` | Service started before `mosquitto` | `docker compose restart telegraf` |
| `unauthorized: unauthorized access` from Telegraf to Influx | Wrong token or org | Re‑paste token in `telegraf.conf`; restart |
| Grafana datasource error `400 Bad Request` | Bucket name mismatch | Confirm `mazly` exists in InfluxDB UI; fix `defaultBucket` in [influxdb.yml](../server/grafana/provisioning/datasources/influxdb.yml) |
| ESP32 loops `MQTT connection failed... rc=-2` | Wrong broker IP or network | Confirm Pi reachable; ESP32 on same SSID; firewall on Pi allows `:1883` |
| No data on backup during simulated failover | Backup Telegraf using primary token/org | See [06-server-backup.md §6.3](06-server-backup.md#63-point-the-backup-telegraf-at-the-seeded-token) |
| `Wi‑Fi Failed!` on LCD repeatedly | 5 GHz‑only SSID | ESP32 only supports 2.4 GHz; create a 2.4 GHz SSID |
| LCD blank after reflashing | Wrong I²C address | Watch boot logs for the I²C scan output; update `LiquidCrystal_I2C lcd(0xNN, …)` |
| `docker compose: command not found` | Old Pi OS | Use `docker-compose` (with hyphen) or update to Bookworm |

## 9.6 Resetting cleanly

> Destructive — wipes data.

```bash
# primary
ssh pi@172.20.10.10 'cd ~/aqm && docker compose down -v'
# backup
ssh pi@172.20.10.11 'cd ~/aqm && docker compose -f docker-compose-backup.yml down -v'
```

Then redo the InfluxDB first‑run setup ([§5.3](05-server-primary.md#3-first-run-influxdb-setup)).

## 9.7 Upgrading container images

```bash
docker compose pull
docker compose up -d
```

Pin major versions (e.g. `influxdb:2.7`) in compose to avoid surprise breakage from `:latest` tags. Mosquitto, Telegraf and Grafana currently use `:latest` — pin them once you’ve settled on versions that work.
