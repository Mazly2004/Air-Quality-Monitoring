# 6. Backup Server (Raspberry Pi 2)

Brings up Mosquitto + InfluxDB + Telegraf on **Raspberry Pi 2** at `172.20.10.11`. Grafana is intentionally disabled by default to save resources.

Compose file: [server/docker-compose-backup.yml](../server/docker-compose-backup.yml).

## 6.1 Differences from the primary

| Item | Primary | Backup |
|---|---|---|
| Container names | `mosquitto`, `influxdb`, … | `mosquitto_backup`, `influxdb_backup`, … |
| InfluxDB org | `Secondspark` | `AQI_Backup` |
| InfluxDB bucket | `mazly` | `air_quality_backup` |
| InfluxDB token | created via UI | **pre‑seeded** by env vars |
| Grafana | enabled (`:3000`) | commented out |
| Network | default | `aqm_backup` bridge |
| Volume | `influxdb-data` | `influxdb-backup-data` |

The backup InfluxDB is created automatically on first run because the compose file sets:

```yaml
environment:
  DOCKER_INFLUXDB_INIT_MODE: setup
  DOCKER_INFLUXDB_INIT_USERNAME: admin
  DOCKER_INFLUXDB_INIT_PASSWORD: adminpassword123
  DOCKER_INFLUXDB_INIT_ORG: AQI_Backup
  DOCKER_INFLUXDB_INIT_BUCKET: air_quality_backup
  DOCKER_INFLUXDB_INIT_ADMIN_TOKEN: backup-token-change-this-in-production
```

> Change the password and admin token **before** exposing this Pi anywhere beyond your LAN ([10-security-and-secrets.md](10-security-and-secrets.md)).

## 6.2 Bring up the backup

```bash
# from the laptop
scp -r server/ pi@172.20.10.11:~/aqm

ssh pi@172.20.10.11
cd ~/aqm
docker compose -f docker-compose-backup.yml up -d
docker compose -f docker-compose-backup.yml ps
```

## 6.3 Point the backup Telegraf at the seeded token

The repo’s `telegraf.conf` is shared between primary and backup. On the **backup Pi only**, edit `server/telegraf/telegraf.conf` to use:

```toml
[[outputs.influxdb_v2]]
  urls         = ["http://influxdb:8086"]
  token        = "backup-token-change-this-in-production"
  organization = "AQI_Backup"
  bucket       = "air_quality_backup"
```

Then:

```bash
docker compose -f docker-compose-backup.yml restart telegraf
```

> The cleanest long‑term fix is to split this into `server/telegraf/telegraf-primary.conf` and `server/telegraf/telegraf-backup.conf`. Tracked as a follow‑up.

## 6.4 Test failover

1. Confirm the ESP32 is publishing to the primary (`mosquitto_sub` on Pi 1).
2. On Pi 1: `docker compose stop mosquitto`.
3. Within ~6 s + 3×backoff the ESP32 logs:

   ```
   ✗ MQTT connection failed... rc=-2
   ⚠ Primary unreachable. Switching to BACKUP broker.
   ✓ MQTT connected to BACKUP broker
   ```

4. On Pi 2: `mosquitto_sub -h 172.20.10.11 -t 'sensors/#' -v` should now show messages.
5. Bring primary back: `docker compose start mosquitto`. Within ~60 s the ESP32 reconnects to primary.

## 6.5 Optional: enable backup Grafana

If you want to view backup data while primary is down, uncomment the `grafana` block in [server/docker-compose-backup.yml](../server/docker-compose-backup.yml) and the matching volume. Use port **`3001`** to avoid clashing with the primary Grafana if you ever run them on the same host.
