# 5. Primary Server (Raspberry Pi 1)

Brings up Mosquitto + InfluxDB + Telegraf + Grafana on **Raspberry Pi 1** at `172.20.10.10`.

Compose file: [server/docker-compose.yml](../server/docker-compose.yml).

## 5.1 Prepare the Pi

1. Install Raspberry Pi OS 64‑bit, enable SSH.
2. Set a static IP `172.20.10.10` (or a DHCP reservation).
3. Install Docker:

   ```bash
   curl -fsSL https://get.docker.com | sudo sh
   sudo usermod -aG docker $USER
   newgrp docker
   ```

4. Copy the project to the Pi:

   ```bash
   # from your laptop
   scp -r server/ pi@172.20.10.10:~/aqm
   ssh pi@172.20.10.10
   cd ~/aqm
   ```

## 5.2 Start the stack

```bash
docker compose up -d
docker compose ps
```

Expected containers:

| Container | Image | Port |
|---|---|---|
| `mosquitto` | `eclipse-mosquitto:latest` | `1883/tcp` |
| `influxdb`  | `influxdb:2.7` | `8086/tcp` |
| `telegraf`  | `telegraf:latest` | — |
| `grafana`   | `grafana/grafana-oss:latest` | `3000/tcp` |

Volumes `influxdb-data` and `grafana-storage` persist data across restarts.

## 5.3 First‑run InfluxDB setup

Unlike the backup stack ([06-server-backup.md](06-server-backup.md)), the primary compose **does not** pre‑seed InfluxDB. You must complete the web setup once:

1. Open `http://172.20.10.10:8086`.
2. Click **Get Started**.
3. Create:
   - Username: e.g. `admin`
   - Password: anything strong
   - **Organization:** `Secondspark` *(must match Telegraf + Grafana)*
   - **Bucket:** `mazly` *(must match Telegraf)*
4. Copy the **API token** shown after setup.
5. Replace the token in:
   - [server/telegraf/telegraf.conf](../server/telegraf/telegraf.conf) → `token = "…"`
   - [server/grafana/provisioning/datasources/influxdb.yml](../server/grafana/provisioning/datasources/influxdb.yml) → `secureJsonData.token`

Then restart so the new token is picked up:

```bash
docker compose restart telegraf grafana
```

> The token in the repo is a placeholder from initial development. **Replace it** before deploying anywhere reachable from the internet — see [10-security-and-secrets.md](10-security-and-secrets.md).

## 5.4 Verify

Watch Telegraf:

```bash
docker logs -f telegraf
```

You should see lines like `mqtt_consumer: connection established` and (once the ESP32 publishes) `Wrote batch of N metrics`.

Subscribe to MQTT directly (from the Pi or any host on the same network):

```bash
mosquitto_sub -h 172.20.10.10 -t 'sensors/#' -v
```

Run a Flux query in InfluxDB UI to confirm rows:

```flux
from(bucket: "mazly")
  |> range(start: -5m)
  |> filter(fn: (r) => r._measurement == "air_quality")
```

## 5.5 Common lifecycle commands

```bash
# stop everything
docker compose down

# stop + DELETE all data (destructive)
docker compose down -v

# follow logs of one service
docker logs -f mosquitto

# update images
docker compose pull && docker compose up -d
```

## 5.6 What lives where on disk

| Path | Purpose |
|---|---|
| `server/mosquitto/config/mosquitto.conf` | MQTT broker config (anonymous, port 1883) |
| `server/telegraf/telegraf.conf` | MQTT → InfluxDB mapping |
| `server/grafana/provisioning/` | Auto‑provisioned datasource + dashboards folder |
| Docker volume `influxdb-data` | Time‑series data |
| Docker volume `grafana-storage` | Grafana DB, plugins, user prefs |
