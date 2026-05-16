# 7. Grafana

Grafana runs on the **primary Pi** at `http://172.20.10.10:3000`.

Provisioning files:

- Datasource: [server/grafana/provisioning/datasources/influxdb.yml](../server/grafana/provisioning/datasources/influxdb.yml)
- Dashboard provider: [server/grafana/provisioning/dashboards/provider.yml](../server/grafana/provisioning/dashboards/provider.yml)
- Dashboard JSON folder: `server/grafana/dashboards/`

## 7.1 First login

- URL: `http://172.20.10.10:3000`
- Username: `admin`
- Password: `admin` (set by `GF_SECURITY_ADMIN_PASSWORD` in [docker-compose.yml](../server/docker-compose.yml); change it on first login)

## 7.2 Datasource

The InfluxDB datasource is auto‑provisioned with:

| Field | Value |
|---|---|
| Name | `InfluxDB` |
| Query language | Flux |
| URL | `http://influxdb:8086` |
| Organization | `Secondspark` |
| Default bucket | `mazly` |
| Token | from `secureJsonData.token` in the provisioning yaml |

After completing [InfluxDB first‑run setup](05-server-primary.md#3-first-run-influxdb-setup):

1. Paste the real token into [influxdb.yml](../server/grafana/provisioning/datasources/influxdb.yml).
2. `docker compose restart grafana`.
3. In Grafana UI: **Connections → Data sources → InfluxDB → Save & Test** should return **Success**.

## 7.3 Building a starter dashboard

The repo contains an empty `server/grafana/dashboards/air_quality.json` placeholder. To build a working dashboard:

1. **Dashboards → New → Add visualization**, pick the `InfluxDB` datasource.
2. Paste a query, e.g. PM2.5 last hour:

   ```flux
   from(bucket: "mazly")
     |> range(start: -1h)
     |> filter(fn: (r) => r._measurement == "air_quality" and r._field == "pm25")
     |> aggregateWindow(every: 30s, fn: mean, createEmpty: false)
     |> yaxis(label: "µg/m³")
   ```

3. Repeat for `pm10`, `co2`, `tvoc`, `temp`, `hum`, `hcho`.
4. Save the dashboard → **Settings → JSON Model → Save to file** as `server/grafana/dashboards/air_quality.json` so it’s auto‑loaded on next start (the provisioning provider re‑reads that folder every 10 s).

## 7.4 Useful Flux snippets

**Latest reading per field:**

```flux
from(bucket: "mazly")
  |> range(start: -10m)
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> last()
```

**Hourly averages, 24 h:**

```flux
from(bucket: "mazly")
  |> range(start: -24h)
  |> filter(fn: (r) => r._measurement == "air_quality")
  |> aggregateWindow(every: 1h, fn: mean, createEmpty: false)
```

**Failover audit (which broker received data):** add a `host`/`broker` tag in Telegraf if you need this; the default config does not tag broker identity. For now, query the **backup** InfluxDB on Pi 2 separately.
