#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define RXD2 16  
#define TXD2 17

// WiFi Configuration - UPDATE THESE!
const char* WIFI_SSID = "Mazly";
const char* WIFI_PASSWORD = "oliver12345";

// MQTT Configuration - UPDATE WITH YOUR PI's IP!
const char* MQTT_SERVER = "172.20.10.3";
const int MQTT_PORT = 1883;
const char* MQTT_TOPIC = "sensors/esp32_01";

WiFiClient espClient;
PubSubClient client(espClient);
bool wifiConnected = false; 

// LCD Display settings (I2C)
// Common I2C addresses: 0x27 or 0x3F
// Format: LiquidCrystal_I2C(address, columns, rows)
LiquidCrystal_I2C lcd(0x27, 20, 4);  // Changed to 0x27 based on I2C scan

// ZPHS01B Data packet structure (25 bytes)
// Format: FF 86 [22 data bytes] [checksum]
uint8_t dataBuffer[25];
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
  lcd.setCursor(0, 0);
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
    Serial.printf("MQTT Server: %s:%d\n", MQTT_SERVER, MQTT_PORT);
    
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

// Reconnect to MQTT broker
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      lcd.setCursor(0, 3);
      lcd.print("MQTT: OK            ");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5s");
      
      lcd.setCursor(0, 3);
      lcd.print("MQTT: Failed        ");
      delay(5000);
    }
  }
}

// Send measurement data to MQTT broker
void sendDataMQTT(float pm25, float pm10, float co2, float tvoc, float temp, float humidity, float hcho) {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    Serial.println("[MQTT] Skipping - WiFi not connected");
    return;
  }
  
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  
  // Create JSON payload
  char buffer[256];
  snprintf(buffer, sizeof(buffer), 
    "{\"pm25\":%.1f,\"pm10\":%.1f,\"co2\":%.0f,\"tvoc\":%.0f,\"temp\":%.1f,\"humidity\":%.1f,\"hcho\":%.3f}",
    pm25, pm10, co2, tvoc, temp, humidity, hcho);
  
  Serial.print("[MQTT] Publishing: ");
  Serial.println(buffer);
  
  // Publish to the topic
  if (client.publish(MQTT_TOPIC, buffer)) {
    Serial.println("[MQTT] ✓ Data sent successfully!");
  } else {
    Serial.println("[MQTT] ✗ Publish failed!");
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
      Serial.printf("WARNING: Found address 0x%02X - update code if LCD doesn't work!\n", foundAddress);
    }
  }
  Serial.println();
  
  // Initialize LCD Display (I2C)
  lcd.init();
  lcd.backlight();
  
  Serial.println("LCD initialized at address 0x3F");
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
  
  // Initialize MQTT client
  if (wifiConnected) {
    client.setServer(MQTT_SERVER, MQTT_PORT);
    reconnectMQTT();
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

  // Debugging: If Recv is 00, we are definitely reading the wrong index
  if (checksum != dataBuffer[24]) {
    Serial.printf("[WARNING] Checksum mismatch. Calc: %02X, Recv: %02X\n", checksum, dataBuffer[24]);
  }

  // --- DATA EXTRACTION (Shifted/Corrected Mapping) ---
  // If your numbers are still crazy, we swap the order (High << 8 | Low)
  uint16_t pm1_0  = (uint16_t)dataBuffer[2] << 8 | dataBuffer[3];
  uint16_t pm2_5  = (uint16_t)dataBuffer[4] << 8 | dataBuffer[5];
  uint16_t pm10   = (uint16_t)dataBuffer[6] << 8 | dataBuffer[7];
  uint16_t co2    = (uint16_t)dataBuffer[8] << 8 | dataBuffer[9];
  uint16_t tvoc   = (uint16_t)dataBuffer[10] << 8 | dataBuffer[11];

  // Temperature math: (High << 8 | Low - 400) / 10
  int16_t rawTemp = (int16_t)dataBuffer[12] << 8 | dataBuffer[13];
  float finalTemp = (rawTemp - 400) / 10.0;

  // Humidity math: (High << 8 | Low) / 10
  uint16_t rawHumi = (uint16_t)dataBuffer[14] << 8 | dataBuffer[15];
  float finalHumi = rawHumi / 10.0;

  uint16_t ch2o   = (uint16_t)dataBuffer[16] << 8 | dataBuffer[17]; 
  uint16_t co     = (uint16_t)dataBuffer[18] << 8 | dataBuffer[19];
  uint16_t o3     = (uint16_t)dataBuffer[20] << 8 | dataBuffer[21];
  uint16_t no2    = (uint16_t)dataBuffer[22] << 8 | dataBuffer[23];

  // --- SERIAL OUTPUT (Your Requested Format) ---
  Serial.println("\n========== ZPHS01B Sensor Data ==========");
  Serial.printf("PM1.0:         %d ug/m3\n", pm1_0);
  Serial.printf("PM2.5:         %d ug/m3\n", pm2_5);
  Serial.printf("PM10:          %d ug/m3\n", pm10);
  Serial.printf("CO2:           %d ppm\n", co2);
  Serial.printf("TVOC:          %d ppb\n", tvoc);
  Serial.printf("Temperature:   %.1f C\n", finalTemp);
  Serial.printf("Humidity:      %.1f %%\n", finalHumi);
  Serial.printf("HCHO (CH2O):   %d ug/m3\n", ch2o);
  Serial.printf("CO:            %d ppm\n", co);
  Serial.printf("O3:            %d ppb\n", o3);
  Serial.printf("NO2:           %d ppb\n", no2);
  Serial.println("=========================================\n");

  // --- MQTT PUBLISHING ---
  sendDataMQTT(pm2_5, pm10, co2, tvoc, finalTemp, finalHumi, ch2o / 1000.0);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32_AQI_Station")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5s");
      delay(5000);
    }
  }
}

void loop() {
  // 1. Maintain MQTT Connection
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // 2. Request data from the sensor every 5 seconds
  static unsigned long lastRequest = 0;
  if (millis() - lastRequest > 5000) {
    requestSensorData(); // Sends FF 01 86...
    lastRequest = millis();
  }

  // 3. Listen for the 25-byte ZPHS01B response
  if (Serial2.available() >= 25) {
    // Check if the first byte is 0xFF
    if (Serial2.peek() != 0xFF) {
      Serial2.read(); // Clear out junk byte if not 0xFF
      return;
    }

    // Read the 25 bytes into your dataBuffer
    Serial2.readBytes(dataBuffer, 25);
    
    // Use the parsing function you already wrote!
    parseZPHS01B();
  }
}