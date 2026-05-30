#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ThreeWire.h>  
#include <RtcDS1302.h>
#include <TFT_eSPI.h> 

// --- Network & MQTT Credentials ---
const char* ssid = "DT01";
const char* password = "edwinatonde";
const char* mqtt_server = "10.26.234.148"; 
const int mqtt_port = 1883;
const char* mqtt_topic = "sensors/indoor/esp32_02";

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMqttRetry = 0;
const unsigned long mqttRetryInterval = 5000; // Retry every 5s, don't block

// --- Hardware Setup --
TFT_eSPI tft = TFT_eSPI(); 

#define RX_PIN 16
#define TX_PIN 17
#define RTC_DAT_PIN 27
#define RTC_CLK_PIN 26
#define RTC_RST_PIN 14

ThreeWire myWire(RTC_DAT_PIN, RTC_CLK_PIN, RTC_RST_PIN); 
RtcDS1302<ThreeWire> Rtc(myWire);

// --- Sensor Variables ---
const byte requestCmd[] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
uint8_t dataBuffer[26];
int bufferIndex = 0;
unsigned long lastRequest = 0;
const unsigned long requestInterval = 10000;

uint16_t pm1_0, pm2_5, pm10, co2;
uint8_t voc;
float temp, hum, ch2o, co, o3, no2;

// --- EDGE AI & ANOMALY DETECTION ---
const int WINDOW_SIZE = 20; // Increased to 20
float pm25_history[WINDOW_SIZE];
int history_idx = 0;
int readings_count = 0;
const float MAD_THRESHOLD_MULTIPLIER = 3.0; 

// --- ALGORITHMS ---

int calculate_AQI_PM25(float pm) {
    if (pm < 0) return 0;
    if (pm <= 12.0) return round(((50.0 - 0.0) / (12.0 - 0.0)) * (pm - 0.0) + 0.0);
    if (pm <= 35.4) return round(((100.0 - 51.0) / (35.4 - 12.1)) * (pm - 12.1) + 51.0);
    if (pm <= 55.4) return round(((150.0 - 101.0) / (55.4 - 35.5)) * (pm - 35.5) + 101.0);
    if (pm <= 150.4) return round(((200.0 - 151.0) / (150.4 - 55.5)) * (pm - 55.5) + 151.0);
    if (pm <= 250.4) return round(((300.0 - 201.0) / (250.4 - 150.5)) * (pm - 150.5) + 201.0);
    if (pm <= 350.4) return round(((400.0 - 301.0) / (350.4 - 250.5)) * (pm - 250.5) + 301.0);
    if (pm <= 500.4) return round(((500.0 - 401.0) / (500.4 - 350.5)) * (pm - 350.5) + 401.0);
    return 500; 
}

uint16_t getAQIColor(int aqi) {
    if (aqi <= 50)  return TFT_GREEN;
    if (aqi <= 100) return TFT_YELLOW;
    if (aqi <= 150) return TFT_ORANGE;
    if (aqi <= 200) return TFT_RED;
    if (aqi <= 300) return TFT_MAGENTA;
    return TFT_MAROON;
}

float getMedian(float data[], int size) {
    float tempArray[size];
    memcpy(tempArray, data, size * sizeof(float));
    for(int i=0; i<size-1; i++) {
        for(int j=i+1; j<size; j++) {
            if(tempArray[i] > tempArray[j]) {
                float t = tempArray[i];
                tempArray[i] = tempArray[j];
                tempArray[j] = t;
            }
        }
    }
    if(size % 2 == 0) return (tempArray[size/2 - 1] + tempArray[size/2]) / 2.0;
    return tempArray[size/2];
}

int calculateMadAnomaly(float newValue) {
    if (readings_count < WINDOW_SIZE) return 0;
    float median = getMedian(pm25_history, WINDOW_SIZE);
    float deviations[WINDOW_SIZE];
    for(int i=0; i<WINDOW_SIZE; i++) {
        deviations[i] = abs(pm25_history[i] - median);
    }
    float mad = getMedian(deviations, WINDOW_SIZE);
    if (mad == 0) mad = 1.0; 
    float current_deviation = abs(newValue - median);
    return (current_deviation > (MAD_THRESHOLD_MULTIPLIER * mad)) ? 1 : 0;
}

// --- COMMS & UI HELPERS ---

void setup_wifi() {
    tft.setCursor(10, 300);
    tft.print("WiFi Connecting...");
    WiFi.begin(ssid, password);
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
        delay(500);
        Serial.print(".");
    }
    tft.fillRect(10, 300, 200, 20, TFT_BLACK);
    if(WiFi.status() == WL_CONNECTED) tft.drawString("WiFi Online", 10, 300, 2);
    else tft.drawString("WiFi Offline", 10, 300, 2);
}

void try_reconnect_mqtt() {
    if (millis() - lastMqttRetry > mqttRetryInterval) {
        lastMqttRetry = millis();
        if (WiFi.status() == WL_CONNECTED) {
            if (client.connect("ESP32_AirQualityNode_02")) {
                Serial.println("MQTT Connected");
            }
        }
    }
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

void setup() {
    Serial.begin(115200);
    Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
    
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextPadding(240); // Prevents text overlap flickering
    
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("INDOOR AIR QUALITY", 80, 5, 4);
    tft.drawFastHLine(0, 35, 480, TFT_WHITE);
    
    tft.drawString("TEMP:", 20, 60, 4);
    tft.drawString("AQI:", 20, 110, 4);
    tft.drawString("PM2.5:", 20, 160, 4);
    tft.drawString("CO2:", 20, 210, 4);
    
    setup_wifi();
    client.setServer(mqtt_server, mqtt_port);
    Rtc.Begin();
    requestSensorData();
}

void loop() {
    // Non-blocking MQTT check
    if (!client.connected()) {
        try_reconnect_mqtt();
    } else {
        client.loop();
    }

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
                int mad_flag_pm25 = calculateMadAnomaly(pm2_5);
                int aqi_pm25 = calculate_AQI_PM25(pm2_5); 

                pm25_history[history_idx] = pm2_5;
                history_idx = (history_idx + 1) % WINDOW_SIZE;
                if (readings_count < WINDOW_SIZE) readings_count++;

                // MQTT Publish (Only if connected)
                if (client.connected()) {
                    JsonDocument doc;
                    doc["pm25"] = pm2_5;
                    doc["co2"]  = co2;
                    doc["temp"] = temp;
                    doc["pm25_aqi"] = aqi_pm25;
                    doc["mad_spike"] = mad_flag_pm25;
                    char jsonBuffer[512];
                    serializeJson(doc, jsonBuffer);
                    client.publish(mqtt_topic, jsonBuffer);
                }

                // --- UI Updates ---
                char valBuf[20];
                tft.setTextColor(TFT_CYAN, TFT_BLACK);
                snprintf(valBuf, sizeof(valBuf), "%.1f C", temp);
                tft.drawString(valBuf, 180, 60, 4);
                
                tft.setTextColor(getAQIColor(aqi_pm25), TFT_BLACK);
                snprintf(valBuf, sizeof(valBuf), "%d", aqi_pm25); 
                tft.drawString(valBuf, 180, 110, 4);
                
                tft.setTextColor(TFT_CYAN, TFT_BLACK);
                snprintf(valBuf, sizeof(valBuf), "%d ug/m3", pm2_5);
                tft.drawString(valBuf, 180, 160, 4);
                
                snprintf(valBuf, sizeof(valBuf), "%d ppm", co2);
                tft.drawString(valBuf, 180, 210, 4);

                // Time update
                RtcDateTime now = Rtc.GetDateTime();
                char timeBuf[15];
                snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", now.Hour(), now.Minute(), now.Second());
                tft.setTextColor(TFT_WHITE, TFT_BLACK);
                tft.drawString(timeBuf, 340, 300, 2);

                // --- Improved AQI Warning Logic ---
                tft.fillRect(10, 255, 460, 40, TFT_BLACK);
                if (aqi_pm25 > 150) {
                    tft.setTextColor(TFT_RED, TFT_BLACK);
                    tft.drawString("DANGER: MASK UP!", 20, 260, 4);
                } else if (aqi_pm25 > 100) {
                    tft.setTextColor(TFT_ORANGE, TFT_BLACK);
                    tft.drawString("CAUTION: SENSITIVE GROUPS", 20, 260, 4);
                } else if (mad_flag_pm25) {
                    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
                    tft.drawString("ALERT: SUDDEN SPIKE!", 20, 260, 4);
                } else {
                    tft.setTextColor(TFT_GREEN, TFT_BLACK);
                    tft.drawString("AIR QUALITY: STABLE", 20, 260, 4);
                }
            }
        }
    }
}