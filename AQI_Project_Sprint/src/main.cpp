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
    
    // 2. Set authentication mode to 0 (No server verification/one-way handshake)
    modem.sendAT("+CSSLCFG=\"authmode\",0,0");
    modem.waitResponse();
    
    // 3. Bind the SNI Server URL directly to Context 0 (Fixes MQ -2 Error)
    String sniCmd = String("+CSSLCFG=\"sni\",0,\"") + mqtt_server + "\"";
    modem.sendAT(sniCmd);
    modem.waitResponse();
    
    Serial.println("[SSL] Hardware SSL Parameters Configured Successfully.");
    // -------------------------------------------------------------

    // Force 2G/GPRS Mode
    Serial.println("[Network] Forcing Modem to 2G/GSM Mode...");
    modem.setNetworkMode(13); 
    delay(3000); 

    // 4. Attach Cellular Data Network
    lcd.setCursor(0, 2); lcd.print("GPRS: Connecting... ");
    Serial.println("[Network] Attaching to NetOne network...");
    if (modem.gprsConnect(apn)) {
        lcd.setCursor(0, 3); lcd.print("STATUS: ONLINE    ");
        Serial.println("[Network] Cellular data attached successfully.");
    }

    // Sync Cellular NTP Time
    syncNTP();
    
    // 5. MQTT Setup
    mqtt.setServer(mqtt_server, mqtt_port); 
    mqtt.setSocketTimeout(30); 
}

void loop() {
    // --- Network Guard & Self-Healing Auto-Reconnect ---
    if (!modem.isGprsConnected()) {
        if (millis() - lastGprsAttempt > 20000) {
            lastGprsAttempt = millis();
            Serial.println("[Network] Link dropped. Repairing GPRS connection...");
            modem.gprsConnect(apn);
        }
    } 
    // --- Safe, Secure MQTT Connection Sequence ---
    else if (!mqtt.connected()) {
        if (millis() - lastReconnectAttempt > 10000) {
            lastReconnectAttempt = millis();
            Serial.print("[MQTT] Connecting to secure cloud cluster... ");
            
            char clientId[32];
            snprintf(clientId, sizeof(clientId), "NetOneNode_%04lX", random(0xffff));
            
            if (mqtt.connect(clientId, mqtt_user, mqtt_pass)) {
                Serial.println("CONNECTED SUCCESSFULLY!");
            } else {
                Serial.print("FAILED, Error State rc=");
                Serial.print(mqtt.state()); 
                Serial.println(" (Check credentials or cluster Access Control settings)");
            }
        }
    } else {
        mqtt.loop();
    }

    // --- 10-Second Passive Sensor Request Pulse ---
    if (millis() - lastRequest > 10000) {
        lastRequest = millis();
        byte cmd[] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
        SerialSensor.write(cmd, 9);
        
        if (millis() - lastTimeSync > 60000) {
            updateNetworkTime();
            lastTimeSync = millis();
        }
    }

    // --- High-Performance Sensor Buffer Parser ---
    while (SerialSensor.available() > 0) {
        if (SerialSensor.peek() != 0xFF) {
            SerialSensor.read(); 
            continue;
        }

        if (SerialSensor.available() >= 26) {
            SerialSensor.readBytes(dataBuf, 26);
            if (dataBuf[1] == 0x86) {
                
                if (checkSensorChecksum(dataBuf)) {
                    pm25 = (uint16_t)dataBuf[4] << 8 | dataBuf[5];
                    co2  = (uint16_t)dataBuf[8] << 8 | dataBuf[9];
                    temp = ((((uint16_t)dataBuf[11] << 8) | dataBuf[12]) - 500.0f) * 0.1f;
                    hum  = ((uint16_t)dataBuf[13] << 8 | dataBuf[14]);

                    pm10        = (uint16_t)dataBuf[6] << 8 | dataBuf[7];
                    uint8_t tvoc_grade = dataBuf[10]; 
                    float ch2o  = ((uint16_t)dataBuf[15] << 8 | dataBuf[16]) * 0.001f; 
                    float co    = ((uint16_t)dataBuf[17] << 8 | dataBuf[18]) * 0.1f;   
                    float o3    = ((uint16_t)dataBuf[19] << 8 | dataBuf[20]) * 0.01f;  
                    float no2   = ((uint16_t)dataBuf[21] << 8 | dataBuf[22]) * 0.01f;  

                    int currentAQI = calculateAQI(pm25);

                    // --- Print Updates to Local LCD Matrix ---
                    lcd.setCursor(0, 0);
                    lcd.print("TEMP:"); lcd.print(temp, 1); lcd.print("C HUM:"); lcd.print(hum, 0); lcd.print("%   ");
                    lcd.setCursor(0, 1);
                    lcd.print("CO2:"); lcd.print(co2); lcd.print("  ");lcd.print("PM2.5:"); lcd.print(pm25);
                    lcd.setCursor(0, 2);
                    lcd.print("TIME: "); lcd.print(netTime);
                    
                    lcd.setCursor(0, 3);
                    if (mqtt.connected()) {
                        lcd.print("MQTT:OK ");
                    } else {
                        lcd.print("MQ:"); lcd.print(mqtt.state()); lcd.print("    "); 
                    }
                    lcd.print("AQI:"); lcd.print(currentAQI); lcd.print("   ");

                    // --- Compile Structured JSON Payload for Cloud Ecosystem ---
                    if (mqtt.connected()) {
                        JsonDocument doc;
                        doc["pm25"] = pm25;
                        doc["co2"]  = co2;
                        doc["temp"] = temp; 
                        doc["hum"]  = hum;  
                        doc["lat"]  = lat;
                        doc["lon"]  = lon;
                        doc["time"] = netTime; 
                        doc["aqi"]  = currentAQI; 
                        
                        doc["pm10"] = pm10;
                        doc["tvoc"] = tvoc_grade;
                        doc["ch2o"] = ch2o;
                        doc["co"]   = co;
                        doc["o3"]   = o3;
                        doc["no2"]  = no2;
                        
                        char jb[384]; 
                        serializeJson(doc, jb);
                        
                        if (mqtt.publish(mqtt_topic, jb)) {
                            Serial.println("[MQTT] Telemetry safely dispatched over TLS.");
                        } else {
                            Serial.println("[MQTT] Warning: Packet dropped at network interface.");
                        }
                    }
                } else {
                    Serial.println("[Sensor] Checksum failed. Corrupt packet discarded.");
                }
            }
        } else {
            break; 
        }
    }
}