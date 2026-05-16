# 10. Security & Secrets

This project ships with **development defaults** that are intentionally permissive so the system runs out of the box on a private LAN. Before exposing any component to a wider network, replace the items in this checklist.

## 10.1 Secrets currently committed to the repo

| File | Field | Action |
|---|---|---|
| [src/main.cpp](../src/main.cpp) | `WIFI_SSID`, `WIFI_PASSWORD` | Move to a gitignored `include/secrets.h` |
| [server/telegraf/telegraf.conf](../server/telegraf/telegraf.conf) | `token` | Replace with the new token from InfluxDB first‑run setup |
| [server/grafana/provisioning/datasources/influxdb.yml](../server/grafana/provisioning/datasources/influxdb.yml) | `secureJsonData.token` | Same token as Telegraf |
| [server/docker-compose-backup.yml](../server/docker-compose-backup.yml) | `DOCKER_INFLUXDB_INIT_PASSWORD`, `DOCKER_INFLUXDB_INIT_ADMIN_TOKEN` | Rotate before deployment |
| [server/docker-compose.yml](../server/docker-compose.yml) | `GF_SECURITY_ADMIN_PASSWORD=admin` | Change on first login |
| [server/docker-compose-timescaledb.yml](../server/docker-compose-timescaledb.yml) | `POSTGRES_PASSWORD`, `PGADMIN_DEFAULT_PASSWORD` | Rotate |
| [export_data.sh](../export_data.sh) | `INFLUX_TOKEN` | Replace |

## 10.2 Move Wi‑Fi credentials out of source

1. Create `include/secrets.h` (already gitignored if you add the line below):

   ```c
   #pragma once
   #define WIFI_SSID_SECRET     "Mazly"
   #define WIFI_PASSWORD_SECRET "your-password"
   ```

2. Replace the constants in [src/main.cpp](../src/main.cpp):

   ```c
   #include "secrets.h"
   const char *WIFI_SSID     = WIFI_SSID_SECRET;
   const char *WIFI_PASSWORD = WIFI_PASSWORD_SECRET;
   ```

3. Add to `.gitignore`:

   ```
   include/secrets.h
   ```

## 10.3 Mosquitto: enable authentication

Default config allows anonymous clients:

```conf
listener 1883
allow_anonymous true
```

For anything beyond a closed LAN, replace with:

```conf
listener 1883
allow_anonymous false
password_file /mosquitto/config/passwd
```

Create the password file:

```bash
docker exec -it mosquitto mosquitto_passwd -c /mosquitto/config/passwd esp32
# enter password twice
docker compose restart mosquitto
```

Update the ESP32 to authenticate:

```c
client.connect(clientId.c_str(), "esp32", "your-password");
```

And Telegraf:

```toml
[[inputs.mqtt_consumer]]
  username = "esp32"
  password = "your-password"
```

## 10.4 TLS

- **MQTT:** enable `listener 8883`, mount certs, switch ESP32 to `WiFiClientSecure`.
- **InfluxDB:** run behind a reverse proxy (Caddy / Traefik / nginx) terminating TLS.
- **Grafana:** set `GF_SERVER_PROTOCOL=https` with mounted certs, or front with reverse proxy.

## 10.5 Network exposure

The compose files publish ports on `0.0.0.0` by default. To bind to LAN only, use explicit interface binding:

```yaml
ports:
  - "172.20.10.10:1883:1883"
  - "172.20.10.10:8086:8086"
  - "172.20.10.10:3000:3000"
```

## 10.6 OWASP quick‑check for this project

| Risk | Status |
|---|---|
| A01 Broken access control (anonymous MQTT) | Open — see §10.3 |
| A02 Cryptographic failures (no TLS) | Open — LAN only for now; see §10.4 |
| A05 Security misconfiguration (default Grafana admin/admin) | Open — change on first login |
| A07 Identification & auth (hard‑coded tokens) | Open — rotate per §10.1 |
| A08 Software & data integrity (unsigned firmware) | N/A for current scope |
| A09 Logging & monitoring | Partial — container logs only |

Track each item to closure before going beyond a private network.
