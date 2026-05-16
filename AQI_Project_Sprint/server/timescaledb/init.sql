-- TimescaleDB Initialization Script
-- Creates schema for Air Quality Monitoring data with time-series optimizations
-- Schema matches the tags + fields written by server/timescaledb/telegraf.conf

-- Enable TimescaleDB extension
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- Create main sensor data table
-- NOTE: Telegraf's outputs.postgresql plugin inserts using lower-case column
-- names that match metric tag/field names exactly. Tags become TEXT columns,
-- fields become DOUBLE PRECISION / BIGINT columns.
CREATE TABLE IF NOT EXISTS sensor_data (
    time            TIMESTAMPTZ       NOT NULL,
    -- Tags (from telegraf.conf: topic regex + static tags)
    device_id       TEXT              NOT NULL,
    location        TEXT              NOT NULL DEFAULT 'unknown',
    topic           TEXT,
    host            TEXT,
    database        TEXT,
    deployment      TEXT,
    -- Sensor fields
    pm1             DOUBLE PRECISION,
    pm25            DOUBLE PRECISION,
    pm10            DOUBLE PRECISION,
    co2             DOUBLE PRECISION,
    tvoc            DOUBLE PRECISION,
    temperature     DOUBLE PRECISION,
    humidity        DOUBLE PRECISION,
    hcho            DOUBLE PRECISION,
    co              DOUBLE PRECISION,
    o3              DOUBLE PRECISION,
    no2             DOUBLE PRECISION,
    -- Algorithm flags from firmware
    heaviside_pm25  BIGINT,
    mad_spike_pm25  BIGINT,
    PRIMARY KEY (time, device_id, location)
);

-- Convert to hypertable (TimescaleDB's optimized time-series table)
SELECT create_hypertable('sensor_data', 'time', if_not_exists => TRUE);

-- Create indexes for common queries
CREATE INDEX IF NOT EXISTS idx_device_time   ON sensor_data (device_id, time DESC);
CREATE INDEX IF NOT EXISTS idx_location_time ON sensor_data (location,  time DESC);
CREATE INDEX IF NOT EXISTS idx_pm25          ON sensor_data (pm25,      time DESC);

-- Create continuous aggregate for 1-hour averages
CREATE MATERIALIZED VIEW IF NOT EXISTS sensor_data_hourly
WITH (timescaledb.continuous) AS
SELECT
    time_bucket('1 hour', time) AS hour,
    device_id,
    location,
    AVG(pm1)         as avg_pm1,
    AVG(pm25)        as avg_pm25,
    AVG(pm10)        as avg_pm10,
    AVG(co2)         as avg_co2,
    AVG(tvoc)        as avg_tvoc,
    AVG(temperature) as avg_temperature,
    AVG(humidity)    as avg_humidity,
    AVG(hcho)        as avg_hcho,
    MIN(pm25)        as min_pm25,
    MAX(pm25)        as max_pm25,
    COUNT(*)         as sample_count
FROM sensor_data
GROUP BY hour, device_id, location;

-- Add refresh policy for continuous aggregate (refresh every hour)
SELECT add_continuous_aggregate_policy('sensor_data_hourly',
    start_offset => INTERVAL '3 hours',
    end_offset => INTERVAL '1 hour',
    schedule_interval => INTERVAL '1 hour',
    if_not_exists => TRUE
);

-- Create retention policy: keep raw data for 90 days
SELECT add_retention_policy('sensor_data', 
    INTERVAL '90 days',
    if_not_exists => TRUE
);

-- Create table for database performance metrics
CREATE TABLE IF NOT EXISTS db_performance_metrics (
    time            TIMESTAMPTZ NOT NULL,
    metric_name     TEXT NOT NULL,
    value           DOUBLE PRECISION,
    notes           TEXT
);

SELECT create_hypertable('db_performance_metrics', 'time', if_not_exists => TRUE);

-- Create view for easy querying of latest data per (device, location)
CREATE OR REPLACE VIEW latest_sensor_readings AS
SELECT DISTINCT ON (device_id, location)
    device_id,
    location,
    time,
    pm1,
    pm25,
    pm10,
    co2,
    tvoc,
    temperature,
    humidity,
    hcho,
    co,
    o3,
    no2,
    heaviside_pm25,
    mad_spike_pm25
FROM sensor_data
ORDER BY device_id, location, time DESC;

-- Create view for data quality metrics
-- (compute per-row inter-arrival gaps via a subquery, then aggregate)
CREATE OR REPLACE VIEW data_quality_metrics AS
WITH gaps AS (
    SELECT
        date_trunc('hour', time) AS hour,
        location,
        device_id,
        EXTRACT(EPOCH FROM (time - LAG(time) OVER (PARTITION BY device_id, location ORDER BY time))) AS gap_seconds
    FROM sensor_data
)
SELECT
    hour,
    location,
    COUNT(*)                  AS total_records,
    COUNT(DISTINCT device_id) AS active_devices,
    AVG(gap_seconds)          AS avg_interval_seconds
FROM gaps
GROUP BY hour, location
ORDER BY hour DESC;

-- Grant permissions
GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO aqm_user;
GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO aqm_user;

-- Insert initial performance tracking record
INSERT INTO db_performance_metrics (time, metric_name, value, notes)
VALUES (NOW(), 'database_initialized', 1, 'TimescaleDB schema created successfully');

-- Print confirmation
DO $$ 
BEGIN
    RAISE NOTICE 'TimescaleDB schema initialized successfully!';
    RAISE NOTICE 'Database: airquality_research';
    RAISE NOTICE 'Main table: sensor_data (hypertable)';
    RAISE NOTICE 'Hourly aggregates: sensor_data_hourly';
    RAISE NOTICE 'Retention: 90 days for raw data';
END $$;
