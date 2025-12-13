#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define RXD2 16  
#define TXD2 17

// WiFi Configuration - UPDATE THESE!
const char* WIFI_SSID = "Mazly";
const char* WIFI_PASSWORD = "oliver12345";

// Backend API Configuration - UPDATE WITH YOUR PC's IP!
const char* API_URL = "http://172.20.10.3:8000/api/measurements";

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
    Serial.printf("Backend API: %s\n", API_URL);
    
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

// Send measurement data to backend via HTTP POST
void sendMeasurement(float pm25, float pm10, float co2, float tvoc, float temp, float humidity, float hcho) {
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Skipping - WiFi not connected");
    return;
  }
  
  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");
  
  // Build JSON payload
  String payload = "{";
  payload += "\"pm25\":" + String(pm25, 1) + ",";
  payload += "\"pm10\":" + String(pm10, 1) + ",";
  payload += "\"co2\":" + String(co2, 0) + ",";
  payload += "\"tvoc\":" + String(tvoc, 0) + ",";
  payload += "\"temp\":" + String(temp, 1) + ",";
  payload += "\"humidity\":" + String(humidity, 1) + ",";
  payload += "\"hcho\":" + String(hcho, 3);
  payload += "}";
  
  Serial.println("[HTTP] Sending to backend...");
  Serial.println(payload);
  
  int httpCode = http.POST(payload);
  
  if (httpCode > 0) {
    Serial.printf("[HTTP] Response code: %d\n", httpCode);
    if (httpCode == 200) {
      Serial.println("[HTTP] ✓ Data sent successfully!");
    } else {
      String response = http.getString();
      Serial.printf("[HTTP] Response: %s\n", response.c_str());
    }
  } else {
    Serial.printf("[HTTP] ✗ Request failed: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
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
  
  delay(1000);
}

void parseZPHS01B() {
  // ZPHS01B response packet: FF 86 [22 data bytes] [checksum] (25 bytes total)
  // Verify start byte and command response
  if (dataBuffer[0] != 0xFF) {
    Serial.println("[ERROR] Invalid start byte");
    return;
  }
  
  if (dataBuffer[1] != 0x86) {
    Serial.printf("[ERROR] Invalid command response: %02X\n", dataBuffer[1]);
    return;
  }
  
  // Calculate checksum (sum of bytes 1-23, then negate)
  uint8_t checksum = 0;
  for (int i = 1; i < 24; i++) {
    checksum += dataBuffer[i];
  }
  checksum = (~checksum) + 1; // Two's complement
  
  if (checksum != dataBuffer[24]) {
    Serial.printf("[WARNING] Checksum mismatch. Calculated: %02X, Received: %02X\n", checksum, dataBuffer[24]);
  }
  
  // Parse sensor data (starting from byte 2)
  uint16_t pm1_0  = (dataBuffer[2] << 8) | dataBuffer[3];
  uint16_t pm2_5  = (dataBuffer[4] << 8) | dataBuffer[5];
  uint16_t pm10   = (dataBuffer[6] << 8) | dataBuffer[7];
  uint16_t co2    = (dataBuffer[8] << 8) | dataBuffer[9];
  uint16_t tvoc   = (dataBuffer[10] << 8) | dataBuffer[11];
  int16_t  temp   = (dataBuffer[12] << 8) | dataBuffer[13]; // Signed
  uint16_t humi   = (dataBuffer[14] << 8) | dataBuffer[15];
  uint16_t ch2o   = (dataBuffer[16] << 8) | dataBuffer[17]; // Formaldehyde
  uint16_t co     = (dataBuffer[18] << 8) | dataBuffer[19];
  uint16_t o3     = (dataBuffer[20] << 8) | dataBuffer[21];
  uint16_t no2    = (dataBuffer[22] << 8) | dataBuffer[23];
  
  // Display results on Serial Monitor
  Serial.println("\n========== ZPHS01B Sensor Data ==========");
  Serial.printf("PM1.0:         %d μg/m³\n", pm1_0);
  Serial.printf("PM2.5:         %d μg/m³\n", pm2_5);
  Serial.printf("PM10:          %d μg/m³\n", pm10);
  Serial.printf("CO2:           %d ppm\n", co2);
  Serial.printf("TVOC:          %d ppb\n", tvoc);
  Serial.printf("Temperature:   %.1f °C\n", temp / 10.0);
  Serial.printf("Humidity:      %.1f %%\n", humi / 10.0);
  Serial.printf("HCHO (CH2O):   %d μg/m³\n", ch2o);
  Serial.printf("CO:            %d ppm\n", co);
  Serial.printf("O3:            %d ppb\n", o3);
  Serial.printf("NO2:           %d ppb\n", no2);
  Serial.println("=========================================\n");
  
  // Display on LCD (20x4 format)
  lcd.clear();
  
  // Line 1: PM2.5 and PM10
  lcd.setCursor(0, 0);
  lcd.printf("PM2.5:%3d PM10:%3d", pm2_5, pm10);
  
  // Line 2: CO2 and TVOC
  lcd.setCursor(0, 1);
  lcd.printf("CO2:%4d TVOC:%3d", co2, tvoc);
  
  // Line 3: Temperature and Humidity
  lcd.setCursor(0, 2);
  lcd.printf("T:%.1fC H:%.1f%%", temp / 10.0, humi / 10.0);
  
  // Line 4: Formaldehyde
  lcd.setCursor(0, 3);
  lcd.printf("HCHO:%d ug/m3", ch2o);
  
  // Send data to backend
  sendMeasurement(
    pm2_5,           // PM2.5
    pm10,            // PM10
    co2,             // CO2
    tvoc,            // TVOC
    temp / 10.0,     // Temperature
    humi / 10.0,     // Humidity
    ch2o / 1000.0    // HCHO in mg/m³
  );
}

void loop() {
  static unsigned long lastRequest = 0;
  
  // Request data every 2 seconds
  if (millis() - lastRequest > 2000) {
    requestSensorData();
    lastRequest = millis();
  }
  
  while (Serial2.available()) {
    uint8_t inByte = Serial2.read();
    
    // Look for start byte (0xFF)
    if (bufferIndex == 0 && inByte != 0xFF) {
      continue; // Wait for start byte
    }
    
    dataBuffer[bufferIndex++] = inByte;
    
    // When we have a complete packet (25 bytes)
    if (bufferIndex >= 25) {
      parseZPHS01B();
      bufferIndex = 0; // Reset for next packet
    }
  }
}
