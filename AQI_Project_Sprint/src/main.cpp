#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// 🌟 TINY_GSM_MODEM_SIM7000SSL is globally defined in platformio.ini
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- Configuration ---
const char apn[] = "internet.netone";

// BROKER: Private EMQX Serverless Cluster (Requires strict TLS 1.2 + SNI routing)
const char* mqtt_server = "ya4f6956.ala.eu-central-1.emqxsl.com"; 
const int mqtt_port = 8883; 
const char* mqtt_user = "harare_esp32_client"; 
const char* mqtt_pass = "Langton@emqx$#"; 
const char* mqtt_topic = "td_aqm/fixed/node/esp32_02/data"; 

// --- Pinout (LilyGo T-SIM7000G) ---
#define MODEM_TX     27
#define MODEM_RX     26
#define MODEM_PWR    4   
#define SENSOR_TX    33
#define SENSOR_RX    32
#define I2C_SDA      21  
#define I2C_SCL      22  

// --- Objects ---
HardwareSerial SerialAT(1);      
HardwareSerial SerialSensor(2);  
TinyGsm modem(SerialAT);

// Secure Client Engine
TinyGsmClientSecure cellularClient(modem);
PubSubClient mqtt(cellularClient);
LiquidCrystal_I2C lcd(0x27, 20, 4);

// --- Global Telemetry Data ---
uint16_t pm25 = 0, co2 = 0, pm10 = 0;
float temp = 0.0, hum = 0.0;

// Hardcoded coordinates for Budiriro, Harare
const float lat = -17.8700;
const float lon = 30.9000;

String netTime = "Syncing..."; 

// --- Timers ---
unsigned long lastRequest = 0;
unsigned long lastReconnectAttempt = 0; 
unsigned long lastGprsAttempt = 0;
unsigned long lastTimeSync = 0;
uint8_t dataBuf[26];

// --- Smart Adaptive Power-On Logic ---
void powerModemResilient() {
    pinMode(MODEM_PWR, OUTPUT);
    Serial.println("[Power] Interrogating modem state...");
    lcd.setCursor(0, 1); lcd.print("Modem: Checking...  ");
    
    for (int i = 0; i < 4; i++) {
        SerialAT.println("AT");
        delay(200);
        if (SerialAT.available()) {
            String response = SerialAT.readString();
            if (response.indexOf("OK") != -1) {
                Serial.println("[Power] Modem is already online! Safe-skipping toggle sequence.");
                lcd.setCursor(0, 1); lcd.print("Modem: Already ON    ");
                return;
            }
        }
    }

    Serial.println("[Power] No response. Launching Pulse Sequence A...");
    lcd.setCursor(0, 1); lcd.print("Modem: Powering A...");
    digitalWrite(MODEM_PWR, HIGH);
    delay(300);
    digitalWrite(MODEM_PWR, LOW);
    delay(4000); 

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

// --- Time Sync Logic ---
void syncNTP() {
    Serial.println("[Time] Syncing modem RTC with NTP Server...");
    modem.sendAT("+CNTP=\"pool.ntp.org\",8"); 
    modem.waitResponse();
    
    modem.sendAT("+CNTP"); 
    modem.waitResponse(10000); 
}

void updateNetworkTime() {
    modem.sendAT("+CCLK?");
    if (modem.waitResponse(2000, "+CCLK: ") == 1) {
        String res = modem.stream.readStringUntil('\n');
        res.trim();
        
        int commaIndex = res.indexOf(',');
        int tzIndex = res.indexOf('+', commaIndex);
        if (tzIndex == -1) tzIndex = res.indexOf('-', commaIndex); 
        
        if (commaIndex != -1 && tzIndex != -1) {
            netTime = res.substring(commaIndex + 1, tzIndex);
        }
    }
}

// --- EPA PM2.5 AQI Calculation ---
int calculateAQI(uint16_t pm) {
    float c = (float)pm;
    int iLow = 0, iHigh = 0;
    float cLow = 0.0, cHigh = 0.0;

    if (c <= 12.0)      { iLow = 0; iHigh = 50; cLow = 0.0; cHigh = 12.0; }
    else if (c <= 35.4) { iLow = 51; iHigh = 100; cLow = 12.1; cHigh = 35.4; }
    else if (c <= 55.4) { iLow = 101; iHigh = 150; cLow = 35.5; cHigh = 55.4; }
    else if (c <= 150.4){ iLow = 151; iHigh = 200; cLow = 55.5; cHigh = 150.4; }
    else if (c <= 250.4){ iLow = 201; iHigh = 300; cLow = 150.5; cHigh = 250.4; }
    else if (c <= 350.4){ iLow = 301; iHigh = 400; cLow = 250.5; cHigh = 350.4; }
    else                { iLow = 401; iHigh = 500; cLow = 350.5; cHigh = 500.4; }

    if (c > 500.4) return 500; 

    return round(((iHigh - iLow) / (cHigh - cLow)) * (c - cLow) + iLow);
}

// Checksum verifier for the ZPHS01B sensor
bool checkSensorChecksum(uint8_t *packet) {
    uint8_t checksum = 0;
    for (int i = 1; i < 25; i++) {
        checksum += packet[i];
    }
    checksum = (~checksum) + 1;
    return (checksum == packet[25]);
}

void setup() {
    // 1. Initialize Serial Interfaces
    Serial.begin(115200);
    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX); 
    SerialSensor.begin(9600, SERIAL_8N1, SENSOR_RX, SENSOR_TX);
    SerialSensor.setTimeout(100); 

    // 2. Local Visuals Setup
    Wire.begin(I2C_SDA, I2C_SCL);
    lcd.init(); lcd.backlight();
    lcd.print("BUDIRIRO NODE ");

    randomSeed(analogRead(0));

    // 3. Boot Cellular Core
    powerModemResilient();

    lcd.setCursor(0, 1); lcd.print("Modem: Syncing...   ");
    if (!modem.init()) { 
        Serial.println("[System] Base init failed. Dropping back to heavy hardware reset...");
        if (!modem.restart()) {
            lcd.setCursor(0, 2); lcd.print("ERROR: NO MODEM   ");
            while(true);
        }
    }

    // -------------------------------------------------------------
    // 🌟 CRITICAL: SIM7000 HARDWARE SSL OVERRIDE FOR EMQX CLOUD 🌟
    // -------------------------------------------------------------
    Serial.println("[SSL] Injecting secure SNI configuration into SIM7000...");
    
    // 1. Force the modem context 0 to TLS 1.2 (3 = TLS 1.2)
    modem.sendAT("+CSSLCFG=\"sslversion\",0,3");
    modem.waitResponse();
    
    // 2. Set