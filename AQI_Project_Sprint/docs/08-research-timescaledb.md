# 8. Research Stack — TimescaleDB

The third stack is **optional** and meant for academic comparison between **TimescaleDB** (PostgreSQL extension) and **InfluxDB** (specialist TSDB).

Compose file: [server/docker-compose-timescaledb.yml](../server/docker-compose-timescaledb.yml).

**Deploy on a lab server or cloud VM**, *not* on the Pis (resource heavy).

## 8.1 Services

| Service | Image | Port | Role |
|---|---|---|---|
| `timescaledb_research` | `timescale/timescaledb:latest-pg15` | `5432` | PostgreSQL + TimescaleDB |
| `telegraf_research` | `telegraf:latest` | — | Subscribes to BOTH MQTT brokers, writes to TimescaleDB |
| `pgadmin_research` | `dpage/pgadmin4:latest` | `5050` | DB admin UI |
| `grafana_timescale` | `grafana/grafana-oss:latest` | `3001` | Separate Grafana for TimescaleDB |

## 8.2 Configure

Edit the environment block in [docker-compose-timescaledb.yml](../server/docker-compose-timescaledb.yml) under `telegraf_research`:

```yaml
environment:
  PGHOST: timescaledb
  PGPORT: "5432"
  PGDATABASE: airquality_research
  PGUSER: aqm_user
  PGPASSWORD: change_this_password_in_production
  MQTT_PRIMARY: tcp://172.20.10.10:1883
  MQTT_BACKUP:  tcp://172.20.10.11:1883
```

Also change `POSTGRES_PASSWORD` and `PGADMIN_DEFAULT_PASSWORD` before deploying.

## 8.3 Bring up

```bash
cd server
docker compose -f docker-compose-timescaledb.yml up -d
docker compose -f docker-compose-timescaledb.yml ps
```

On first start, [timescaledb/init.sql](../server/timescaledb/init.sql) runs and creates:

- `sensor_data` hypertable (90‑day retention)
- `sensor_data_hourly` continuous aggregate (refresh hourly)
- `db_performance_metrics` hypertable
- `insert_sensor_data()` function (de‑duplicates between primary and backup MQTT)
- `latest_sensor_readings` and `data_quality_metrics` views

## 8.4 Verify

Postgres CLI from the container:

```bash
docker exec -it timescaledb_research \
  psql -U aqm_user -d airquality_research \
  -c "SELECT count(*) FROM sensor_data;"
```

A quick top‑10:

```bash
docker exec -it timescaledb_research \
  psql -U aqm_user -d airquality_research \
  -c "SELECT * FROM latest_sensor_readings LIMIT 10;"
```

## 8.5 pgAdmin

- URL: `http://<server>:5050`
- User: `admin@airquality.local`
- Pass: `admin_password_change_this`
- Add a server with host `timescaledb`, port `5432`, db `airquality_research`, user `aqm_user`.

## 8.6 Grafana on TimescaleDB

- URL: `http://<server>:3001` (admin / admin)
- Add a **PostgreSQL** datasource with:
  - Host: `timescaledb:5432`
  - Database: `airquality_research`
  - User: `aqm_user`
  - Password: (yours)
  - SSL mode: `disable`
  - **TimescaleDB:** ✅ enable
- Example query (PM2.5 last hour):

  ```sql
  SELECT time, pm25
  FROM sensor_data
  WHERE time > now() - interval '1 hour'
  ORDER BY time;
  ```

## 8.7 Comparing performance vs InfluxDB

Useful baseline queries to log into `db_performance_metrics`:

```sql
EXPLAIN ANALYZE
SELECT time_bucket('1 hour', time) AS hour, avg(pm25)
FROM sensor_data
WHERE time > now() - interval '7 days'
GROUP BY hour ORDER BY hour;
```

For an equivalent Influx run, use Flux’s `aggregateWindow(every: 1h, fn: mean)`.
