#define TINY_GSM_MODEM_SIM7000 // IMPORTANT: Must be defined before including TinyGsmClient.h

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- Configuration ---
const char apn[] = "internet.netone";

// BROKER: EMQX Public Broker 
const char* mqtt_server = "broker.emqx.io"; 
const int mqtt_port = 1883;

const char* mqtt_topic = "netone/fixed/node/esp32_02/data"; // Unique topic for your Telegraf stack

// --- Pinout (LilyGo T-SIM7000G) ---
#define MODEM_TX     27
#define MODEM_RX     26
#define MODEM_PWR    4   
#define SENSOR_TX    33
#define SENSOR_RX    32
#define I2C_SDA      21  
#define I2C_SCL      22  

// --- Objects ---
HardwareSerial SerialAT(1);      // SIM7000G Cellular Core
HardwareSerial SerialSensor(2);  // ZPHS01B Air Quality Sensor
TinyGsm modem(SerialAT);
TinyGsmClient cellularClient(modem);
PubSubClient mqtt(cellularClient);
LiquidCrystal_I2C lcd(0x27, 20, 4);

// --- Global Telemetry Data ---
uint16_t pm25 = 0, co2 = 0, pm10 = 0;
float temp = 0.0, hum = 0.0, lat = 0.0, lon = 0.0;
String gpsTime = "Searching...";

// --- Timers ---
unsigned long lastRequest = 0;
unsigned long lastReconnectAttempt = 0; 
unsigned long lastGprsAttempt = 0;
unsigned long lastTimeSync = 0;
bool gpsLocked = false;
uint8_t dataBuf[26];

// --- Smart Adaptive Power-On Logic ---
void powerModemResilient() {
    pinMode(MODEM_PWR, OUTPUT);
    Serial.println("[Power] Interrogating modem state...");
    lcd.setCursor(0, 1); lcd.print("Modem: Checking...  ");
    
    // Test if the modem is already awake from a previous software crash/reset
    for (int i = 0; i < 4; i++) {
        SerialAT.println("AT");
        delay(200);
        if (SerialAT.available()) {
            String response = SerialAT.readString();
            if (response.indexOf("OK") != -1) {
                Serial.println("[Power] Modem is already online! Safe-skipping toggle sequence.");
                lcd.setCursor(0, 1); lcd.print("Modem: Already ON   ");
                return;
            }
        }
    }

    // Sequence A: Standard LilyGo High -> Low hardware trigger pulse
    Serial.println("[Power] No response. Launching Pulse Sequence A...");
    lcd.setCursor(0, 1); lcd.print("Modem: Powering A...");
    digitalWrite(MODEM_PWR, HIGH);
    delay(300);
    digitalWrite(MODEM_PWR, LOW);
    delay(4000); // Give hardware time to spin up cellular firmware

    for (int i = 0; i < 4; i++) {
        SerialAT.println("AT");
        delay(200);
        if (SerialAT.available()) {
            String response = SerialAT.readString();
            if (response.indexOf("OK") != -1) {
                Serial.println("[Power] Hardware initialized via Sequence A.");
                return;
            }
        }
    }

    // Sequence B: Alternative Low -> High hardware pulse (for secondary hardware revisions)
    Serial.println("[Power] Still dark. Launching Fallback Sequence B...");
    lcd.setCursor(0, 1); lcd.print("Modem: Powering B...");
    digitalWrite(MODEM_PWR, LOW);
    delay(1000);
    digitalWrite(MODEM_PWR, HIGH);
    delay(4000); 
    
    for (int i = 0; i < 4; i++) {
        SerialAT.println("AT");
        delay(200);
        if (SerialAT.available()) {
            String response = SerialAT.readString();
            if (response.indexOf("OK") != -1) {
                Serial.println("[Power] Hardware initialized via Sequence B.");
                return;
            }
        }
    }
    Serial.println("[Power] WARNING: Physical lines unresponsive. Check 18650 lipo cell.");
}

void updateGPS() {
    float g_lat = 0, g_lon = 0, g_speed = 0, g_alt = 0, g_accuracy = 0;
    int g_vsat = 0, g_usat = 0;
    int g_y = 0, g_m = 0, g_d = 0, g_h = 0, g_min = 0, g_s = 0;

    if (modem.getGPS(&g_lat, &g_lon, &g_speed, &g_alt, &g_vsat, &g_usat, &g_accuracy, &g_y, &g_m, &g_d, &g_h, &g_min, &g_s)) {
        if (g_lat != 0.0 && g_lon != 0.0) {
            lat = g_lat; 
            lon = g_lon;
            gpsLocked = true;
        }
        
        // Convert UTC to Central Africa Time (CAT) / UTC+2
        int local_h = (g_h + 2) % 24;

        char tBuf[16];
        snprintf(tBuf, sizeof(tBuf), "%02d:%02d:%02d", local_h, g_min, g