#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>

#define RXD2 16
#define TXD2 17

// WiFi Configuration - UPDATE THESE!
const char *WIFI_SSID = "INNOV_HUB";
const char *WIFI_PASSWORD = "wkProg219!";

// MQTT Configuration - High Availability Setup
// Primary MQTT Broker (Raspberry Pi 1)
const char *MQTT_PRIMARY_SERVER = "10.140.10.140";
const int MQTT_PRIMARY_PORT = 1883;

// Secondary MQTT Broker (Raspberry Pi 2) - Backup
const char *MQTT_SECONDARY_SERVER = "172.20.10.11";
const int MQTT_SECONDARY_PORT = 1883;

const char *MQTT_TOPIC = "sensors/outdoor/esp32_01";

// Failover state tracking
bool usingPrimaryBroker = true;
unsigned long lastPrimaryRetry = 0;
const unsigned long PRIMARY_RETRY_INTERVAL = 60000; // Try primary every 60s
int connectionAttempts = 0;
const int MAX_RETRY_ATTEMPTS = 3;

WiFiClient espClient;
PubSubClient client(espClient);
bool wifiConnected = false;

// LCD Display settings (I2C)
// Common I2C addresses: 0x27 or 0x3F
// Format: LiquidCrystal_I2C(address, columns, rows)
LiquidCrystal_I2C lcd(0x27, 20, 4); // Changed to 0x27 based on I2C scan

// ZPHS01B Data packet structure (26 bytes)
// Format: FF 86 [22 data bytes] [2 checksum bytes]
uint8_t dataBuffer[26];
uint8_t bufferIndex = 0;

// Command to request data from ZPHS01B (Question and Answer mode)
void requestSensorData() {
  // Send request command: FF 01 86 00 00 00 00 00 79
  uint8_t cmd[] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
  Serial2.write(cmd, 9);
}

void reconnect();

// Connect to WiFi
void connectWiFi() {
  Serial.println("\n=== Connecting to WiFi ===");
  Serial.printf("SSID: %s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  lcd.clear();
  lcd.setCursor(0, 0);-------
  lcd.print("WiFi Connecting...");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n✓ WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.printf("MQTT Primary: %s:%d, Backup: %s:%d\n", MQTT_PRIMARY_SERVER,
                  MQTT_PRIMARY_PORT, MQTT_SECONDARY_SERVER,
                  MQTT_SECONDARY_PORT);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
  } else {
    wifiConnected = false;
    Serial.println("\n✗ WiFi Connection Failed");
    Serial.println("Continuing without WiFi...");

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed!");
    lcd.setCursor(0, 1);
    lcd.print("Local Mode Only");
    delay(2000);
  }
}

// Reconnect to MQTT broker with automatic failover
void reconnect() {
  while (!client.connected()) {
    // Periodically try to return to primary broker if using secondary
    if (!usingPrimaryBroker &&
        (millis() - lastPrimaryRetry > PRIMARY_RETRY_INTERVAL)) {
      Serial.println("[HA] Attempting to reconnect to primary broker...");
      client.setServer(MQTT_PRIMARY_SERVER, MQTT_PRIMARY_PORT);

      if (client.connect("ESP32_AQI_Outdoor_01")) {
        usingPrimaryBroker = true;
        connectionAttempts = 0;
        Serial.println("[HA] ✓ Reconnected to PRIMARY broker!");

        lcd.setCursor(0, 3);
        lcd.print("MQTT: Primary       ");
        return;
      }
      lastPrimaryRetry = millis();
      Serial.println("[HA] Primary still unavailable, staying on backup");
    }

    // Determine which broker to try
    const char *currentServer =
        usingPrimaryBroker ? MQTT_PRIMARY_SERVER : MQTT_SECONDARY_SERVER;
    int currentPort =
        usingPrimaryBroker ? MQTT_PRIMARY_PORT : MQTT_SECONDARY_PORT;
    const char *brokerName = usingPrimaryBroker ? "PRIMARY" : "BACKUP";

    Serial.printf("[HA] Attempting MQTT connection to %s (%s:%d)...\n",
                  brokerName, currentServer, currentPort);

    // Set the current broker
    client.setServer(currentServer, currentPort);

    // Attempt connection
    if (client.connect("ESP32_AQI_Outdoor_01")) {
      connectionAttempts = 0;
      Serial.printf("[HA] ✓ Connected to %s broker!\n", brokerName);

      lcd.setCursor(0, 3);
      if (usingPrimaryBroker) {
        lcd.print("MQTT: Primary       ");
      } else {
        lcd.print("MQTT: Backup        ");
      }
      return;
    } else {
      connectionAttempts++;
      Serial.printf("[HA] ✗ Failed to connect to %s (rc=%d), attempt %d/%d\n",
                    brokerName, client.state(), connectionAttempts,
                    MAX_RETRY_ATTEMPTS);

      lcd.setCursor(0, 3);
      lcd.print("MQTT: Connecting... ");

      // If primary fails after max attempts, switch to secondary
      if (usingPrimaryBroker && connectionAttempts >= MAX_RETRY_ATTEMPTS) {
        Serial.println(
            "[HA] ⚠ Primary broker unreachable, failing over to BACKUP");
        usingPrimaryBroker = false;
        connectionAttempts = 0;
        lastPrimaryRetry = millis();

        lcd.setCursor(0, 3);
        lcd.print("MQTT: Failover...   ");
        delay(1000);
        continue; // Try secondary immediately
      }

      // Exponential backoff: 2s, 4s, 8s (cap at 3 to avoid overflow)
      int cappedAttempts = min(connectionAttempts, 3);
      int backoffDelay = min(2000 * (1 << cappedAttempts), 8000);
      Serial.printf("[HA] Retrying in %dms...\n", backoffDelay);
      delay(backoffDelay);
    }
  }
}

// Send measurement data to MQTT broker
void sendDataMQTT(float pm25, float pm10, float co2, float tvoc, float temp,
                  float humidity, float hcho, float co, float o3, float no2) {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    Serial.println("[MQTT] Skipping - WiFi not connected");
    return;
  }

  if (!client.connected()) {
    reconnect();
  }

  // Create JSON payload with proper decimal precision for Telegraf
  char buffer[512];
  snprintf(buffer, sizeof(buffer),
           "{\"pm25\":%.2f,\"pm10\":%.2f,\"co2\":%.2f,\"tvoc\":%.2f,\"temp\":%."
           "2f,\"hum\":%.2f,\"hcho\":%.4f,\"co\":%.2f,\"o3\":%.3f,\"no2\":%.3f}",
           pm25, pm10, co2, tvoc, temp, humidity, hcho, co, o3, no2);

  Serial.print("[MQTT] Publishing: ");
  Serial.println(buffer);

  // Publish to the topic
  if (client.publish(MQTT_TOPIC, buffer)) {
    Serial.println("[MQTT] ✓ Success");
  } else {
    Serial.println("[MQTT] ✗ Failed");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ESP32 Air Quality Monitor (ZPHS01B) ===");

  // Initialize I2C and scan for devices
  Wire.begin();

  Serial.println("\nScanning I2C bus...");
  byte count = 0;
  byte foundAddress = 0;
  for (byte i = 1; i < 127; i++) {
    Wire.beginTransmission(i);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C device found at address 0x%02X\n", i);
      foundAddress = i;
      count++;
    }
  }
  if (count == 0) {
    Serial.println("No I2C devices found! Check wiring!");
  } else {
    Serial.printf("Found %d I2C device(s)\n", count);
    if (foundAddress != 0x3F && foundAddress != 0x27) {
      Serial.printf(
          "WARNING: Found address 0x%02X - update code if LCD doesn't work!\n",
          foundAddress);
    }
  }
  Serial.println();

  // Initialize LCD Display (I2C)
  lcd.init();
  lcd.backlight();

  Serial.println("LCD initialized at address 0x27");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ZPHS01B Monitor");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(2000);
  lcd.clear();

  // Initialize Serial2 for sensor
  Serial.println("Initializing Serial2 for ZPHS01B sensor...");
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  Serial.println("Serial2 initialized.");
  Serial.println("===========================================\n");

  // Connect to WiFi
  connectWiFi();

  // Initialize MQTT client (start with primary broker)
  if (wifiConnected) {
    Serial.println("[HA] Initializing MQTT with PRIMARY broker");
    client.setServer(MQTT_PRIMARY_SERVER, MQTT_PRIMARY_PORT);
    usingPrimaryBroker = true;
    reconnect();
  }

  delay(1000);
}

void parseZPHS01B() {
  // ZPHS01B response packet: FF 86 [22 data bytes] [checksum] (25 bytes total)
  // Let's ensure we are actually looking at a valid header
  if (dataBuffer[0] != 0xFF || dataBuffer[1] != 0x86) {
    return;
  }

  // Calculate checksum (sum of bytes 1-23, then negate + 1)
  uint8_t checksum = 0;
  for (int i = 1; i < 24; i++) {
    checksum += dataBuffer[i];
  }
  checksum = (~checksum) + 1;

  if (checksum != dataBuffer[25]) {
    Serial.printf("[WARNING] Checksum mismatch. Calc: %02X, Recv: %02X\n",
                  checksum, dataBuffer[25]);
    return;
  }

  // --- DATA EXTRACTION ---
  uint16_t pm1_0 = (uint16_t)dataBuffer[2] << 8 | dataBuffer[3];
  uint16_t pm2_5 = (uint16_t)dataBuffer[4] << 8 | dataBuffer[5];
  uint16_t pm10  = (uint16_t)dataBuffer[6] << 8 | dataBuffer[7];
  uint16_t co2   = (uint16_t)dataBuffer[8] << 8 | dataBuffer[9];
  uint8_t  tvoc  = dataBuffer[10]; // TVOC is a single byte

  // Temperature: bytes 11-12, big-endian, formula: (raw - 435) * 0.1
  uint16_t rawTempU = (uint16_t)dataBuffer[11] << 8 | dataBuffer[12];
  float finalTemp = (rawTempU - 435) * 0.1f;

  // Humidity: bytes 13-14, big-endian, formula: raw * 0.1 = RH%
  uint16_t rawHumi = (uint16_t)dataBuffer[13] << 8 | dataBuffer[14];
  float finalHumi = rawHumi * 0.1f;

  uint16_t ch2o = (uint16_t)dataBuffer[15] << 8 | dataBuffer[16];
  uint16_t co   = (uint16_t)dataBuffer[17] << 8 | dataBuffer[18];
  uint16_t o3   = (uint16_t)dataBuffer[19] << 8 | dataBuffer[20];
  uint16_t no2  = (uint16_t)dataBuffer[21] << 8 | dataBuffer[22];

  // --- SERIAL OUTPUT (Your Requested Format) ---
  Serial.println("\n========== ZPHS01B Sensor Data ==========");
  Serial.printf("PM1.0:         %d ug/m3\n", pm1_0);
  Serial.printf("PM2.5:         %d ug/m3\n", pm2_5);
  Serial.printf("PM10:          %d ug/m3\n", pm10);
  Serial.printf("CO2:           %d ppm\n", co2);
  Serial.printf("TVOC:          %d ppb\n", tvoc);
  Serial.printf("Temperature:   %.1f C\n", finalTemp);
  Serial.printf("Humidity:      %.1f %%\n", finalHumi);
  Serial.printf("HCHO (CH2O):   %.3f mg/m3\n", ch2o * 0.001f);
  Serial.printf("CO:            %.1f ppm\n", co * 0.1f);
  Serial.printf("O3:            %.2f ppm\n", o3 * 0.01f);
  Serial.printf("NO2:           %.2f ppm\n", no2 * 0.01f);
  Serial.println("=========================================\n");

  // --- SENSOR VALIDATION ---
  // Skip MQTT publish if sensor readings are invalid
  if (isnan(finalTemp) || finalTemp < -40 || finalTemp > 85) {
    Serial.println(
        "[ERROR] Invalid temperature reading, skipping MQTT publish.");
    return;
  }
  if (isnan(finalHumi) || finalHumi < 0 || finalHumi > 100) {
    Serial.println("[ERROR] Invalid humidity reading, skipping MQTT publish.");
    return;
  }

  // --- LCD UPDATE ---
  lcd.clear();
  char line[21];
  snprintf(line, sizeof(line), "PM2.5:%d PM10:%d", pm2_5, pm10);
  lcd.setCursor(0, 0);
  lcd.print(line);
  snprintf(line, sizeof(line), "CO2:%d TVOC:%d", co2, tvoc);
  lcd.setCursor(0, 1);
  lcd.print(line);
  snprintf(line, sizeof(line), "Temp:%.1fC Hum:%.1f%%", finalTemp, finalHumi);
  lcd.setCursor(0, 2);
  lcd.print(line);
  lcd.setCursor(0, 3);
  lcd.print(usingPrimaryBroker ? "MQTT: Primary       " : "MQTT: Backup        ");

  // --- MQTT PUBLISHING ---
  sendDataMQTT(pm2_5, pm10, co2, tvoc, finalTemp, finalHumi, ch2o * 0.001f, co * 0.1f, o3 * 0.01f, no2 * 0.01f);
}

void loop() {
  // 1. Maintain MQTT Connection
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // 2. Request sensor data every 5 seconds
  static unsigned long lastRequest = 0;
  if (millis() - lastRequest > 5000) {
    requestSensorData();
    Serial.println("[SENSOR] Request sent, waiting for response...");
    lastRequest = millis();
  }

  // 3. Read sensor response (25-byte packet)
  while (Serial2.available()) {
    uint8_t b = Serial2.read();

    // Hunt for packet header 0xFF
    if (bufferIndex == 0 && b != 0xFF) {
      continue;
    }
    // Second byte must be 0x86
    if (bufferIndex == 1 && b != 0x86) {
      bufferIndex = 0;
      continue;
    }

    dataBuffer[bufferIndex++] = b;

    if (bufferIndex == 26) {
      bufferIndex = 0;
      parseZPHS01B();
    }
  }
}
