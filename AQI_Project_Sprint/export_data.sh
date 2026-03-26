#!/bin/bash
# Export air quality data from InfluxDB on Pi to a local CSV
# Usage: ./export_data.sh [time_range]
# Example: ./export_data.sh -7d   (default: -30d)

PI_USER="george"
PI_HOST="172.20.10.10"
INFLUX_TOKEN="Ah89PHWEm5j8dIIjAH4cKyQwARelf2dQCa6hbSkJPMxic6Tida_j0adFxAVoA0J3Kdc7CIvFPOMHPTwgascf3Q=="
INFLUX_ORG="Secondspark"
INFLUX_BUCKET="mazly"

RANGE="${1:--30d}"
TIMESTAMP=$(date +"%Y-%m-%d_%H-%M-%S")
OUTPUT=~/Desktop/air_quality_${TIMESTAMP}.csv

echo "Exporting data from last ${RANGE} ..."

ssh -o PreferredAuthentications=password -o PubkeyAuthentication=no \
    "${PI_USER}@${PI_HOST}" \
    "docker exec influxdb influx query \
      --org '${INFLUX_ORG}' \
      --token '${INFLUX_TOKEN}' \
      'from(bucket:\"${INFLUX_BUCKET}\") |> range(start: ${RANGE}) |> filter(fn: (r) => r._measurement == \"air_quality\") |> pivot(rowKey:[\"_time\"], columnKey: [\"_field\"], valueColumn: \"_value\")' \
      --raw" > "${OUTPUT}"

if [ $? -eq 0 ] && [ -s "${OUTPUT}" ]; then
    echo "Done! Saved to: ${OUTPUT}"
else
    echo "Failed or empty result. Check SSH credentials and that the Pi stack is running."
    rm -f "${OUTPUT}"
fi
