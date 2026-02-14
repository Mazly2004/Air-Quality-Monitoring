-- TimescaleDB Initialization Script
-- Creates schema for Air Quality Monitoring data with time-series optimizations

-- Enable TimescaleDB extension
CREATE EXTENSION IF NOT EXISTS timescaledb;

-- Create main sensor data table
CREATE TABLE IF NOT EXISTS sensor_data (
    time            TIMESTAMPTZ NOT NULL,
    device_id       TEXT NOT NULL,
    pm25            DOUBLE PRECISION,
    pm10            DOUBLE PRECISION,
    co2             DOUBLE PRECISION,
    tvoc            DOUBLE PRECISION,
    temperature     DOUBLE PRECISION,
    humidity        DOUBLE PRECISION,
    hcho            DOUBLE PRECISION,
    -- Additional metadata
    mqtt_broker     TEXT,  -- Which broker sent this data (primary/backup)
    PRIMARY KEY (time, device_id)
);

-- Convert to hypertable (TimescaleDB's optimized time-series table)
SELECT create_hypertable('sensor_data', 'time', if_not_exists => TRUE);

-- Create indexes for common queries
CREATE INDEX IF NOT EXISTS idx_device_time ON sensor_data (device_id, time DESC);
CREATE INDEX IF NOT EXISTS idx_pm25 ON sensor_data (pm25, time DESC);
CREATE INDEX IF NOT EXISTS idx_broker ON sensor_data (mqtt_broker, time DESC);

-- Create continuous aggregate for 1-hour averages
CREATE MATERIALIZED VIEW IF NOT EXISTS sensor_data_hourly
WITH (timescaledb.continuous) AS
SELECT 
    time_bucket('1 hour', time) AS hour,
    device_id,
    AVG(pm25) as avg_pm25,
    AVG(pm10) as avg_pm10,
    AVG(co2) as avg_co2,
    AVG(tvoc) as avg_tvoc,
    AVG(temperature) as avg_temperature,
    AVG(humidity) as avg_humidity,
    AVG(hcho) as avg_hcho,
    MIN(pm25) as min_pm25,
    MAX(pm25) as max_pm25,
    COUNT(*) as sample_count
FROM sensor_data
GROUP BY hour, device_id;

-- Add refresh policy for continuous aggregate (refresh every hour)
SELECT add_continuous_aggregate_policy('sensor_data_hourly',
    start_offset => INTERVAL '2 hours',
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

-- Function to handle duplicate data (called by Telegraf)
CREATE OR REPLACE FUNCTION insert_sensor_data(
    p_time TIMESTAMPTZ,
    p_device_id TEXT,
    p_pm25 DOUBLE PRECISION,
    p_pm10 DOUBLE PRECISION,
    p_co2 DOUBLE PRECISION,
    p_tvoc DOUBLE PRECISION,
    p_temperature DOUBLE PRECISION,
    p_humidity DOUBLE PRECISION,
    p_hcho DOUBLE PRECISION,
    p_mqtt_broker TEXT
) RETURNS VOID AS $$
BEGIN
    INSERT INTO sensor_data (
        time, device_id, pm25, pm10, co2, tvoc, 
        temperature, humidity, hcho, mqtt_broker
    ) VALUES (
        p_time, p_device_id, p_pm25, p_pm10, p_co2, p_tvoc,
        p_temperature, p_humidity, p_hcho, p_mqtt_broker
    )
    ON CONFLICT (time, device_id) DO UPDATE SET
        -- Keep the first value (from primary broker if both send)
        mqtt_broker = CASE 
            WHEN sensor_data.mqtt_broker = 'primary' THEN sensor_data.mqtt_broker
            ELSE EXCLUDED.mqtt_broker
        END;
END;
$$ LANGUAGE plpgsql;

-- Create view for easy querying of latest data
CREATE OR REPLACE VIEW latest_sensor_readings AS
SELECT DISTINCT ON (device_id)
    device_id,
    time,
    pm25,
    pm10,
    co2,
    tvoc,
    temperature,
    humidity,
    hcho,
    mqtt_broker
FROM sensor_data
ORDER BY device_id, time DESC;

-- Create view for data quality metrics
CREATE OR REPLACE VIEW data_quality_metrics AS
SELECT
    date_trunc('hour', time) as hour,
    COUNT(*) as total_records,
    COUNT(DISTINCT device_id) as active_devices,
    SUM(CASE WHEN mqtt_broker = 'primary' THEN 1 ELSE 0 END) as from_primary,
    SUM(CASE WHEN mqtt_broker = 'backup' THEN 1 ELSE 0 END) as from_backup,
    AVG(EXTRACT(EPOCH FROM (time - LAG(time) OVER (PARTITION BY device_id ORDER BY time)))) as avg_interval_seconds
FROM sensor_data
GROUP BY hour
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
