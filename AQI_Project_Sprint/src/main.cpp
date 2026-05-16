#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ThreeWire.h>  
#include <RtcDS1302.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

// --- Network & MQTT Credentials ---
const char* ssid = "TDANGARE 9321";
const char* password = "26754|Wy";
const char* mqtt_server = "192.168.137.1";
const int mqtt_port = 1883;
const char* mqtt_topic = "telemetry/airquality";

WiFiClient espClient;
PubSubClient client(espClient);

// --- ZPHS01B Sensor Pins ---
#define RX_PIN 16
#define TX_PIN 17

// --- DS1302 RTC Pins ---
#define RTC_DAT_PIN 27
#define RTC_CLK_PIN 26
#define RTC_RST_PIN 14

// --- Nokia 5110 LCD Pins ---
#define LCD_SCLK 18
#define LCD_DIN  23
#define LCD_DC   4
#define LCD_CS   5
#define LCD_RST  2

ThreeWire myWire(RTC_DAT_PIN, RTC_CLK_PIN, RTC_RST_PIN); 
RtcDS1302<ThreeWire> Rtc(myWire);
Adafruit_PCD8544 display = Adafruit_PCD8544(LCD_SCLK, LCD_DIN, LCD_DC, LCD_CS, LCD_RST);

const byte requestCmd[] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
uint8_t dataBuffer[26];
int bufferIndex = 0;
unsigned long lastRequest = 0;
const unsigned long requestInterval = 60000;

// Parsed sensor values
uint16_t pm1_0, pm2_5, pm10, co2;
uint8_t voc;
float temp, hum, ch2o, co, o3, no2;

// --- EDGE AI & ANOMALY DETECTION VARIABLES ---
// 1. Regulatory Limit (Heaviside threshold) for PM2.5
const float LIMIT_PM25 = 15.0;   // WHO guideline e.g., 15 µg/m³

// 2. MAD Algorithm Window for PM2.5
const int WINDOW_SIZE = 10;
float pm25_history[WINDOW_SIZE];
int history_idx = 0;
int readings_count = 0;
const float MAD_THRESHOLD_MULTIPLIER = 3.0; // 'k' value

// --- HELPER FUNCTIONS FOR MATH ---

// Heaviside Step Function
int heaviside(float value, float limit) {
  return (value >= limit) ? 1 : 0;
}

// Utility to find the median of an array without altering the original
float getMedian(float data[], int size) {
  float temp[size];
  memcpy(temp, data, size * sizeof(float));
  
  // Simple bubble sort for small arrays
  for(int i=0; i<size-1; i++) {
    for(int j=i+1; j<size; j++) {
      if(temp[i] > temp[j]) {
        float t = temp[i];
        temp[i] = temp[j];
        temp[j] = t;
      }
    }
  }
  if(size % 2 == 0) return (temp[size/2 - 1] + temp[size/2]) / 2.0;
  return temp[size/2];
}

// Compute MAD anomaly flag
int calculateMadAnomaly(float newValue) {
  if (readings_count < WINDOW_SIZE) return 0; // Not enough data yet

  float median = getMedian(pm25_history, WINDOW_SIZE);
  
  float deviations[WINDOW_SIZE];
  for(int i=0; i<WINDOW_SIZE; i++) {
    deviations[i] = abs(pm25_history[i] - median);
  }
  
  float mad = getMedian(deviations, WINDOW_SIZE);
  if (mad == 0) mad = 1.0; // Prevent overly sensitive zero-floor division
  
  float current_deviation = abs(newValue - median);
  
  // Return 1 if current deviation is greater than k * MAD
  return (current_deviation > (MAD_THRESHOLD_MULTIPLIER * mad)) ? 1 : 0;
}


// --- STANDARD SETUP & CONNECTIVITY ---

void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
}

void reconnect_mqtt() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32_AirQualityNode")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  
  display.begin();
  display.setContrast(60); 
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(BLACK);
  
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  
  Rtc.Begin();
  if (!Rtc.GetIsRunning()) Rtc.SetIsRunning(true);
  
  requestSensorData();
}

void requestSensorData() {
  Serial2.write(requestCmd, 9);
}

bool parseZPHS01B() {
  if (dataBuffer[0] != 0xFF || dataBuffer[1] != 0x86) return false;

  uint8_t checksum = 0;
  for (int i = 1; i <= 24; i++) checksum += dataBuffer[i];
  checksum = (~checksum) + 1;

  if (checksum != dataBuffer[25]) return false;

  pm1_0 = (uint16_t)dataBuffer[2] << 8 | dataBuffer[3];   
  pm2_5 = (uint16_t)dataBuffer[4] << 8 | dataBuffer[5];   
  pm10  = (uint16_t)dataBuffer[6] << 8 | dataBuffer[7];   
  co2   = (uint16_t)dataBuffer[8] << 8 | dataBuffer[9];   
  voc   = dataBuffer[10];                                 
  
  uint16_t rawTempU = (uint16_t)dataBuffer[11] << 8 | dataBuffer[12];
  temp = (rawTempU - 500.0f) * 0.1f;
  hum = (float)((uint16_t)dataBuffer[13] << 8 | dataBuffer[14]);

  ch2o = ((uint16_t)dataBuffer[15] << 8 | dataBuffer[16]) * 0.001f;  
  co   = ((uint16_t)dataBuffer[17] << 8 | dataBuffer[18]) * 0.1f;    
  o3   = ((uint16_t)dataBuffer[19] << 8 | dataBuffer[20]) * 0.01f;   
  no2  = ((uint16_t)dataBuffer[21] << 8 | dataBuffer[22]) * 0.01f;   

  return true;
}

void loop() {
  if (!client.connected()) reconnect_mqtt();
  client.loop();

  if (millis() - lastRequest > requestInterval) {
    requestSensorData();
    lastRequest = millis();
  }

  while (Serial2.available()) {
    uint8_t b = Serial2.read();

    if (bufferIndex == 0 && b != 0xFF) continue;
    if (bufferIndex == 1 && b != 0x86) { bufferIndex = 0; continue; }

    dataBuffer[bufferIndex++] = b;

    if (bufferIndex == 26) {
      bufferIndex = 0;
      
      if (parseZPHS01B()) {
        
        // 1. Run Algorithms ONLY for PM2.5
        int mad_flag_pm25 = calculateMadAnomaly(pm2_5);
        int h_flag_pm25 = heaviside(pm2_5, LIMIT_PM25);

        // Update rolling window for next calculation
        pm25_history[history_idx] = pm2_5;
        history_idx = (history_idx + 1) % WINDOW_SIZE;
        if (readings_count < WINDOW_SIZE) readings_count++;

        // 2. Build JSON Payload
        StaticJsonDocument<512> doc;
        
        // All raw data
        JsonObject sensors = doc.createNestedObject("readings");
        sensors["pm1_0"] = pm1_0;
        sensors["pm2_5"] = pm2_5;
        sensors["pm10"]  = pm10;
        sensors["co2"]   = co2;
        sensors["voc_grade"] = voc;
        sensors["temp"]  = temp;
        sensors["humidity"] = hum;
        sensors["ch2o"]  = ch2o;
        sensors["co"]    = co;
        sensors["o3"]    = o3;
        sensors["no2"]   = no2;

        // Anomaly flags for PM2.5 only
        JsonObject flags = doc.createNestedObject("anomalies");
        flags["heaviside_pm25"] = h_flag_pm25;
        flags["mad_spike_pm25"] = mad_flag_pm25;

        // 3. Serialize and Publish
        char jsonBuffer[512];
        serializeJson(doc, jsonBuffer);
        client.publish(mqtt_topic, jsonBuffer);
        
        Serial.println("Published to MQTT:");
        Serial.println(jsonBuffer);

        // 4. Update LCD Display
        display.clearDisplay();
        display.setCursor(0, 0);
        
        RtcDateTime now = Rtc.GetDateTime();
        char lcdTime[15];
        snprintf(lcdTime, sizeof(lcdTime), "%02d:%02d:%02d", now.Hour(), now.Minute(), now.Second());
        display.println(lcdTime);
        
        display.print("T:"); display.print(temp, 1); display.print("C "); 
        display.print("RH:"); display.print(hum, 0); display.println("%");
        display.print("PM2.5: "); display.print(pm2_5); display.println("ug");
        
        // Show anomaly warnings on screen for PM2.5
        if(mad_flag_pm25) display.println("! SPIKE DETECTED");
        else if (h_flag_pm25) display.println("! LIMIT EXCEEDED");
        else display.println("Air Quality OK");
        
        display.display();
      }
    }
  }
}