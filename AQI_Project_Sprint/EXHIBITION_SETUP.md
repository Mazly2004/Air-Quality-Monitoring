# Air Quality Monitoring — Exhibition Setup Guide

End-to-end, step-by-step instructions for setting up the **TimescaleDB research stack** on a brand-new PC for an exhibition demo. No prior context needed.

By the end of this guide you will have:

- Two ESP32 air-quality nodes (indoor + outdoor) publishing live readings over MQTT.
- A Mosquitto broker (on a Raspberry Pi) relaying those readings.
- A TimescaleDB hypertable (`sensor_data`) on the exhibition PC, fed by Telegraf.
- A live Grafana dashboard at `http://localhost:3001` showing PM2.5, CO₂, temperature, humidity, etc.
- Optional: pgAdmin at `http://localhost:5050` and/or the native pgAdmin desktop app for poking at the raw rows.

The full data path on the exhibition day:

```
ESP32 sensors  ──MQTT──►  Mosquitto (Raspberry Pi)  ──MQTT──►  telegraf_research  ──SQL──►  TimescaleDB  ──SQL──►  Grafana
```

---

## 0. What you need before you start

### Hardware

| Item | Notes |
|---|---|
| 2 × ESP32 dev boards | One labelled **indoor** (`esp32_02`), one **outdoor** (`esp32_01`). Already flashed and working. |
| 1 × Raspberry Pi running Mosquitto | This is the MQTT broker. The ESP32s already point at its IP. |
| 1 × Exhibition PC (Windows / macOS / Linux) | This is what this guide installs everything on. 8 GB RAM minimum. |
| Wi-Fi network the ESP32s already know | SSID/password are hard-coded in the firmware. The exhibition PC and the Pi must join the **same** Wi-Fi. |

### Network requirements

- All four devices (2× ESP32, Pi, exhibition PC) must be on the **same Wi-Fi subnet**.
- The Pi's IP address must match what is set in [src/main.cpp](src/main.cpp) `MQTT_PRIMARY_SERVER` and in the exhibition PC's docker-compose file `MQTT_PRIMARY`. **Currently both are set to `10.144.4.20`.**

If the Pi's IP changes at the exhibition venue, see **Section 9 — Troubleshooting**.

---

## 1. Install the required software on the exhibition PC

You only need three things:

### 1a. Git

- **Windows:** download from <https://git-scm.com/download/win> and install with defaults.
- **macOS:** install Xcode Command Line Tools: `xcode-select --install` (or `brew install git`).
- **Linux (Ubuntu/Debian):** `sudo apt update && sudo apt install -y git`.

Verify:
```bash
git --version
```

### 1b. Docker Desktop (includes Docker Compose)

Download and install **Docker Desktop** from <https://www.docker.com/products/docker-desktop>.

- **Windows:** enable WSL 2 when prompted. Restart when asked.
- **macOS:** drag to `/Applications`, open it, accept the privileged helper prompt.
- **Linux:** install Docker Engine + Docker Compose plugin per <https://docs.docker.com/engine/install/>.

After installation, **launch Docker Desktop** and wait for the whale icon in your tray/menu bar to become solid (= ready).

Verify:
```bash
docker --version
docker compose version
```

Both commands must work. If `docker compose version` says "command not found", you have an older `docker-compose` binary — most steps below still work, just replace `docker compose` with `docker-compose`.

### 1c. (Optional) pgAdmin desktop app

This is **not required** — the docker stack already includes a browser-based pgAdmin at `http://localhost:5050`. Install the desktop app only if you specifically want it.

- Download from <https://www.pgadmin.org/download/>.
- Run the installer with defaults.

> **Note:** You do **not** need to install PostgreSQL on the host PC. The database runs entirely inside a Docker container. Installing PostgreSQL on the host will only cause a port conflict on `5432` — avoid it unless you already have it for unrelated reasons (see Section 9).

---

## 2. Get the project code

Open a terminal (PowerShell on Windows, Terminal on macOS/Linux) and clone the repo:

```bash
cd ~/Documents
git clone <your-repo-url> AQI_Project_Sprint
cd AQI_Project_Sprint
```

Confirm you can see this layout:

```
AQI_Project_Sprint/
├── server/
│   ├── docker-compose-timescaledb.yml      ← we will use this one
│   ├── docker-compose.yml                  ← (legacy InfluxDB stack, not needed for the exhibition)
│   ├── timescaledb/
│   │   ├── init.sql
│   │   └── telegraf.conf
│   └── ...
├── src/
│   └── main.cpp                            ← ESP32 firmware
└── EXHIBITION_SETUP.md                     ← this file
```

---

## 3. Verify the MQTT broker (Raspberry Pi) is up

The ESP32s are already configured to publish to the Pi at **`10.144.4.20:1883`**. Before doing anything on the exhibition PC, make sure:

1. The Pi is powered on and on the venue's Wi-Fi.
2. Its IP address really is `10.144.4.20` (check with `hostname -I` on the Pi, or your router admin page).
3. Mosquitto is running on the Pi:
   ```bash
   # SSH into the Pi, then:
   sudo systemctl status mosquitto
   # or, if Mosquitto runs in Docker on the Pi:
   docker ps | grep mosquitto
   ```

From the exhibition PC, confirm you can reach the broker:

- **macOS / Linux:**
  ```bash
  nc -zv 10.144.4.20 1883
  ```
- **Windows (PowerShell):**
  ```powershell
  Test-NetConnection -ComputerName 10.144.4.20 -Port 1883
  ```

You should get a "succeeded / open" response. If not, fix that **first** — nothing downstream will work until the broker is reachable.

---

## 4. Adjust IPs if the Pi is at a different address at the venue

The Pi's IP is hard-coded in **two** places. If it has changed from `10.144.4.20`, edit both before launching the stack.

### 4a. `server/docker-compose-timescaledb.yml`

Open it and find this block (around line 58):

```yaml
- MQTT_PRIMARY=tcp://10.144.4.20:1883
- MQTT_BACKUP=tcp://172.20.10.11:1883
```

Change `10.144.4.20` to the Pi's current IP. The `MQTT_BACKUP` line can stay as-is or be removed — see **Section 9** for why.

### 4b. `src/main.cpp` (only if you also need to re-flash the ESP32s)

```cpp
const char *MQTT_PRIMARY_SERVER = "10.144.4.20";   // ← update if Pi IP changed
```

If the ESP32s are already publishing successfully to the existing broker, you do **not** need to re-flash. Only Section 4a is necessary for the exhibition PC.

---

## 5. Launch the TimescaleDB stack

From the project root:

```bash
cd server
docker compose -f docker-compose-timescaledb.yml up -d
```

Docker will pull four images on first run (takes a few minutes on a fresh PC):

| Container | Image | Purpose |
|---|---|---|
| `timescaledb_research` | `timescale/timescaledb:latest-pg15` | The database. |
| `telegraf_research` | `telegraf:latest` | MQTT → Postgres bridge. |
| `pgadmin_research` | `dpage/pgadmin4:latest` | Browser-based DB admin tool. |
| `grafana_timescale` | `grafana/grafana-oss:latest` | The dashboard at port 3001. |

When the pulls finish, you should see something like:

```
 ✔ Container timescaledb_research  Healthy
 ✔ Container grafana_timescale     Started
 ✔ Container telegraf_research     Started
 ✔ Container pgadmin_research      Started
```

Verify all four are running:

```bash
docker ps
```

You should see all four containers with status `Up …` and `timescaledb_research` showing `(healthy)`.

---

## 6. Verify data is landing in TimescaleDB

Wait about 30 seconds after `docker ps` shows everything healthy. Then check rows are arriving:

```bash
docker exec timescaledb_research psql -U aqm_user -d airquality_research -c \
  "SELECT location, device_id, COUNT(*) AS rows, MAX(time) AS last_seen
   FROM sensor_data
   GROUP BY location, device_id
   ORDER BY location;"
```

Expected output:

```
 location | device_id | rows |           last_seen
----------+-----------+------+-------------------------------
 indoor   | esp32_02  |   42 | 2026-05-16 15:29:17.700384+00
 outdoor  | esp32_01  |   83 | 2026-05-16 15:29:11.316756+00
```

If you see `(0 rows)`, jump to **Section 9 — Troubleshooting**.

---

## 7. Open Grafana and import the dashboard

### 7a. First login

1. In a browser, open **<http://localhost:3001>**.
2. Username: `admin`. Password: `admin`. You'll be prompted to set a new password — pick anything (e.g. `airquality`).

### 7b. Add the TimescaleDB data source

1. Left sidebar → **Connections** (plug icon) → **Data sources** → **+ Add new data source**.
2. Search for **PostgreSQL** and click it.
3. Fill in **exactly** as below:

   | Field | Value |
   |---|---|
   | Name | `TimescaleDB` |
   | Host URL | `timescaledb:5432` |
   | Database name | `airquality_research` |
   | Username | `aqm_user` |
   | Password | `change_this_password_in_production` |
   | TLS/SSL Mode | `disable` |
   | Version | `15` |
   | TimescaleDB | toggle **ON** |

   > **Why `timescaledb:5432` and not `localhost:5435`?** Grafana lives in the same Docker network as the database, so it uses the Docker DNS name `timescaledb` on the container's internal port `5432`. The host-side `5435` only matters for tools running directly on your PC (e.g. the native pgAdmin app).

4. Click **Save & test**. You should see ✅ `Database Connection OK`.

### 7c. Import the demo dashboard

Two options.

**Option 1 — use the dashboard JSON that ships with the repo:**

1. Top-right of any Grafana page → **+** → **Import dashboard**.
2. **Upload JSON file** → pick [server/grafana/dashboards/air_quality.json](server/grafana/dashboards/air_quality.json).
3. When prompted for the data source, pick **TimescaleDB**.
4. Click **Import**.

Note: that JSON was originally built for InfluxDB. Each panel will need its query rewritten to SQL. See **Section 8** for ready-to-paste SQL.

**Option 2 — build panels from scratch:**

Skip the import and create one panel per metric using the SQL templates in **Section 8**.

### 7d. Make it auto-refresh

In the top-right of the dashboard:

1. Time-range picker → set to **Last 15 minutes** (or **Last 5 minutes**).
2. Click the **circular refresh arrow** next to the time range → pick **10s**.
3. Click the **disk / save icon** to persist these settings.

You'll now see lines crawl forward as new readings arrive (~every 10 s).

---

## 8. Panel SQL — one per sensor field

For each panel: **Edit → Data source = `TimescaleDB` → paste the SQL → Apply → Save dashboard.**

Grafana will substitute `$__timeFilter(time)` with the dashboard time range automatically.

**PM1**
```sql
SELECT time AS "time", location AS metric, pm1 AS value
FROM sensor_data WHERE $__timeFilter(time) ORDER BY time;
```

**PM2.5**
```sql
SELECT time AS "time", location AS metric, pm25 AS value
FROM sensor_data WHERE $__timeFilter(time) ORDER BY time;
```

**PM10**
```sql
SELECT time AS "time", location AS metric, pm10 AS value
FROM sensor_data WHERE $__timeFilter(time) ORDER BY time;
```

**CO₂**
```sql
SELECT time AS "time", location AS metric, co2 AS value
FROM sensor_data WHERE $__timeFilter(time) ORDER BY time;
```

**TVOC**
```sql
SELECT time AS "time", location AS metric, tvoc AS value
FROM sensor_data WHERE $__timeFilter(time) ORDER BY time;
```

**Temperature**
```sql
SELECT time AS "time", location AS metric, temperature AS value
FROM sensor_data WHERE $__timeFilter(time) ORDER BY time;
```

**Humidity**
```sql
SELECT time AS "time", location AS metric, humidity AS value
FROM sensor_data WHERE $__timeFilter(time) ORDER BY time;
```

**HCHO (Formaldehyde)**
```sql
SELECT time AS "time", location AS metric, hcho AS value
FROM sensor_data WHERE $__timeFilter(time) ORDER BY time;
```

**CO (Carbon Monoxide)**
```sql
SELECT time AS "time", location AS metric, co AS value
FROM sensor_data WHERE $__timeFilter(time) ORDER BY time;
```

**O₃ (Ozone)**
```sql
SELECT time AS "time", location AS metric, o3 AS value
FROM sensor_data WHERE $__timeFilter(time) ORDER BY time;
```

**NO₂ (Nitrogen Dioxide)**
```sql
SELECT time AS "time", location AS metric, no2 AS value
FROM sensor_data WHERE $__timeFilter(time) ORDER BY time;
```

### Useful extras

**Heaviside / MAD PM2.5 spike flags** (algorithm output for the research story):
```sql
SELECT time AS "time", location AS metric, heaviside_pm25 AS value
FROM sensor_data WHERE $__timeFilter(time) ORDER BY time;
```

**Smoother lines at long time ranges** (uses TimescaleDB's `time_bucket`):
```sql
SELECT time_bucket($__interval, time) AS "time",
       location AS metric,
       AVG(pm25) AS value
FROM sensor_data
WHERE $__timeFilter(time)
GROUP BY 1, 2 ORDER BY 1;
```

**Latest single value** (for Stat / Gauge panels — "Current outdoor PM2.5"):
```sql
SELECT pm25 FROM sensor_data
WHERE location = 'outdoor' ORDER BY time DESC LIMIT 1;
```

### Tip — find which fields actually have data

Some firmware versions don't populate every field. Run this in the Grafana **Explore** view (or pgAdmin) to see which columns are empty:

```sql
SELECT
  COUNT(pm1) AS pm1, COUNT(pm25) AS pm25, COUNT(pm10) AS pm10,
  COUNT(co2) AS co2, COUNT(tvoc) AS tvoc, COUNT(hcho) AS hcho,
  COUNT(co)  AS co,  COUNT(o3)   AS o3,   COUNT(no2)  AS no2,
  COUNT(temperature) AS temperature, COUNT(humidity) AS humidity
FROM sensor_data;
```

Columns with `0` rows have no data — skip those panels.

---

## 9. Inspecting the raw database (optional)

You have two options. Pick whichever you prefer.

### 9a. pgAdmin in the browser (already running)

1. Open **<http://localhost:5050>**.
2. Login:
   - Email: `admin@example.com`
   - Password: `admin_password_change_this`
3. Right-click **Servers** → **Register** → **Server…**
4. **General tab:** Name = `TimescaleDB Research`
5. **Connection tab:**

   | Field | Value |
   |---|---|
   | Host name/address | `timescaledb` |
   | Port | `5432` |
   | Maintenance database | `airquality_research` |
   | Username | `aqm_user` |
   | Password | `change_this_password_in_production` |
   | Save password | ✓ |

6. **Save**.
7. Navigate to: **Servers → TimescaleDB Research → Databases → airquality_research → Schemas → public → Tables → `sensor_data`**.
8. Right-click `sensor_data` → **View/Edit Data → Last 100 Rows**.

### 9b. Native pgAdmin desktop app (if installed)

Same as above except the **Host name/address** is `localhost` and the **Port** is **`5435`** (because the desktop app goes through the host port mapping, not the Docker network).

| Field | Value |
|---|---|
| Host name/address | `localhost` |
| Port | `5435` |
| Maintenance database | `airquality_research` |
| Username | `aqm_user` |
| Password | `change_this_password_in_production` |

---

## 10. Daily operation (during the exhibition)

### Stop everything (at end of day)

```bash
cd server
docker compose -f docker-compose-timescaledb.yml stop
```

### Start everything (next morning)

```bash
cd server
docker compose -f docker-compose-timescaledb.yml start
```

Data persists across stops/starts because TimescaleDB and Grafana use named Docker volumes (`server_timescaledb-data`, `server_grafana-timescale-data`).

### Watch live logs (debugging)

```bash
docker logs -f telegraf_research        # is Telegraf reading from MQTT?
docker logs -f timescaledb_research     # is the DB healthy?
docker logs -f grafana_timescale        # any dashboard errors?
```

### Reset to a clean state (wipe all data)

⚠️ **This deletes every reading.** Only do this between demos if you want a fresh start.

```bash
cd server
docker compose -f docker-compose-timescaledb.yml down -v
docker compose -f docker-compose-timescaledb.yml up -d
```

The `init.sql` runs again on next startup and recreates the empty schema.

---

## 11. Troubleshooting

### "No rows in `sensor_data` after 1 minute"

Almost always one of two things.

**a) The exhibition PC can't reach the MQTT broker.** Re-run the broker test from Section 3. Check the Pi's IP. If it has changed, edit `MQTT_PRIMARY` in `server/docker-compose-timescaledb.yml` and recreate Telegraf:
```bash
docker compose -f docker-compose-timescaledb.yml up -d --force-recreate telegraf_research
```

**b) Telegraf is crash-looping because the backup broker is unreachable.** Look at the logs:
```bash
docker logs telegraf_research --tail 20
```
If you see `Error running agent: ... i/o timeout`, the workaround is to remove the backup line from the `servers` list in [server/timescaledb/telegraf.conf](server/timescaledb/telegraf.conf):
```toml
[[inputs.mqtt_consumer]]
  servers = [
    "${MQTT_PRIMARY}",
    # "${MQTT_BACKUP}",        # ← comment out when backup is offline
  ]
```
Then `docker compose -f docker-compose-timescaledb.yml up -d --force-recreate telegraf_research`.

### "`role aqm_user does not exist`" in pgAdmin

You're connected to a different Postgres than the one in the container — most likely a native Postgres on the host that's hogging port 5432. Either:
- Use **`localhost:5435`** in the desktop pgAdmin (not 5432), or
- Use the **browser pgAdmin at localhost:5050** with host `timescaledb` and port `5432` (which goes through Docker DNS).

### "Bind for 0.0.0.0:5435 failed: port is already allocated"

Some other container or app is holding port 5435 on your exhibition PC. Find it:
- **macOS/Linux:** `lsof -nP -iTCP:5435 -sTCP:LISTEN`
- **Windows (PowerShell):** `Get-NetTCPConnection -LocalPort 5435`

Either stop that process, or change `5435:5432` in `docker-compose-timescaledb.yml` to another free port (e.g. `5436:5432`) and use that new port in the desktop pgAdmin.

### "Grafana panels say 'No data'"

1. Click **Edit** on the panel → check the **Data source** dropdown is **TimescaleDB** (not InfluxDB).
2. Check the dashboard time range — it must include "now". `Last 15 minutes` is a safe default.
3. Run the panel's SQL in pgAdmin to confirm rows exist for the chosen time range.

### "I want to change the Pi's IP later"

Edit `MQTT_PRIMARY` in `server/docker-compose-timescaledb.yml`, then:
```bash
docker compose -f docker-compose-timescaledb.yml up -d --force-recreate telegraf_research
```
That's it — no DB wipe needed.

---

## 12. Quick reference

### URLs

| Service | URL | Login |
|---|---|---|
| Grafana (research) | <http://localhost:3001> | `admin` / `admin` (forced reset on first login) |
| pgAdmin (browser) | <http://localhost:5050> | `admin@example.com` / `admin_password_change_this` |
| TimescaleDB (from host) | `localhost:5435` | user `aqm_user`, pw `change_this_password_in_production` |
| TimescaleDB (from containers) | `timescaledb:5432` | same credentials |

### Files you may need to edit at the venue

| File | What to change | When |
|---|---|---|
| [server/docker-compose-timescaledb.yml](server/docker-compose-timescaledb.yml) | `MQTT_PRIMARY=tcp://<Pi IP>:1883` | If the Pi's IP changes |
| [server/timescaledb/telegraf.conf](server/timescaledb/telegraf.conf) | Comment out `"${MQTT_BACKUP}",` | If the backup broker is offline |
| [src/main.cpp](src/main.cpp) | `MQTT_PRIMARY_SERVER` | Only if re-flashing ESP32s |

### One-liners

```bash
# Start everything
docker compose -f server/docker-compose-timescaledb.yml up -d

# Stop everything
docker compose -f server/docker-compose-timescaledb.yml stop

# Restart just Telegraf (after editing broker IPs)
docker compose -f server/docker-compose-timescaledb.yml up -d --force-recreate telegraf_research

# See current row counts
docker exec timescaledb_research psql -U aqm_user -d airquality_research \
  -c "SELECT location, device_id, COUNT(*) FROM sensor_data GROUP BY 1,2;"

# Wipe and start fresh
docker compose -f server/docker-compose-timescaledb.yml down -v
docker compose -f server/docker-compose-timescaledb.yml up -d
```

---

You're done. On exhibition day, just open <http://localhost:3001> and the dashboard will be live.
