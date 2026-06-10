#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// 🌟 TINY_GSM_MODEM_SIM7000 is globally defined in platformio.ini
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- Configuration ---
const char apn[] = "internet.netone";

// BROKER: Private EMQX Serverless Cluster
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
HardwareSerial SerialAT(1);      // SIM7000G Cellular Core
HardwareSerial SerialSensor(2);  // ZPHS01B Air Quality Sensor
TinyGsm modem(SerialAT);

TinyGsmClient plainClient(modem);+-
TinyGsmClientSecure cellularClient(modem);
PubSubClient mqtt(cellularClient);
LiquidCrystal_I2C lcd(0x27, 20, 4);

// --- Global Telemetry Data ---
uint16_t pm25 = 0, co2 = 0, pm10 = 0;
float temp = 0.0, hum = 0.0;

const float lat = -17.8700;
const float lon = 30.9000;

String netTime = "Syncing..."; 

// --- Timers & Buffers ---
unsigned long lastRequest = 0;
unsigned long lastReconnectAttempt = 0; 
unsigned long lastGprsAttempt = 0;
unsigned long lastTimeSync = 0;
uint8_t dataBuf[26];
int dataIndex = 0; // Global tracker for single-byte parser alignment

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
                Serial.println("[Power] Modem is already online!");
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
    Serial.println("[Power] WARNING: Physical lines unresponsive.");
}

void updateNetworkTime() {
    modem.sendAT("+CCLK?");
    if (modem.waitResponse(2000, "+CCLK: \"") == 1) {
        String res = modem.stream.readStringUntil('"');
        res.trim();
        
        int commaIndex = res.indexOf(',');
        int tzIndex = res.indexOf('+', commaIndex);
        if (tzIndex == -1) tzIndex = res.indexOf('-', commaIndex); 
        
        if (commaIndex != -1 && tzIndex != -1) {
            netTime = res.substring(commaIndex + 1, tzIndex);
        } else {
            netTime = res; 
        }
    }
}

bool syncTimeHTTP() {
    Serial.println("[HTTP Time] Launching unencrypted TCP endpoint request...");
    if (!plainClient.connect("worldtimeapi.org", 80)) {
        return false;
    }
    
    plainClient.print("GET /api/timezone/Africa/Harare HTTP/1.1\r\n");
    plainClient.print("Host: worldtimeapi.org\r\n");
    plainClient.print("Connection: close\r\n\r\n");
    
    unsigned long timeout = millis();
    bool headerEnded = false;
    while (plainClient.connected() && millis() - timeout < 5000) {
        String line = plainClient.readStringUntil('\n');
        if (line == "\r" || line == "") {
            headerEnded = true;
            break;
        }
    }
    
    if (!headerEnded) {
        plainClient.stop();
        return false;
    }
    
    String body = plainClient.readString();
    plainClient.stop();
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    if (error) return false;
    
    const char* datetime = doc["datetime"]; 
    if (!datetime || strlen(datetime) < 19) return false;
    
    String dtStr = String(datetime);
    String s_year  = dtStr.substring(2, 4);   
    String s_month = dtStr.substring(5, 7);  
    String s_day   = dtStr.substring(8, 10);  
    String s_time  = dtStr.substring(11, 19); 
    
    String cclk_cmd = "+CCLK=\"" + s_year + "/" + s_month + "/" + s_day + "," + s_time + "+08\"";
    modem.sendAT(cclk_cmd);
    return (modem.waitResponse(2000) == 1);
}

void syncNTP() {
    modem.sendAT("+CNTP=\"216.239.35.0\",8"); 
    if (modem.waitResponse(3000) == 1) {
        modem.sendAT("+CNTP"); 
        if (modem.waitResponse(2000) == 1) { 
            if (modem.waitResponse(8000, "+CNTP: 0") == 1) {
                updateNetworkTime();
                return;
            }
        }
    }
    
    if (syncTimeHTTP()) {
        updateNetworkTime();
        return;
    }
    
    // Emergency Fallback Injection (Valid Baseline for EMQX TLS verification)
    modem.sendAT("+CCLK=\"26/06/10,01:15:00+08\""); 
    modem.waitResponse(2000);
    updateNetworkTime(); 
}

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

bool checkSensorChecksum(uint8_t *packet) {
    uint8_t checksum = 0;
    for (int i = 1; i < 25; i++) {
        checksum += packet[i];
    }
    checksum = (~checksum) + 1;
    return (checksum == packet[25]);
}

void setup() {
    Serial.begin(115200);
    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX); 
    SerialSensor.begin(9600, SERIAL_8N1, SENSOR_RX, SENSOR_TX);
    SerialSensor.setTimeout(100); 

    Wire.begin(I2C_SDA, I2C_SCL);
    lcd.init(); lcd.backlight();
    lcd.print("BUDIRIRO NODE ");

    randomSeed(analogRead(0));
    powerModemResilient();

    if (!modem.init()) { 
        if (!modem.restart()) {
            while(true);
        }
    }

    modem.setNetworkMode(13); 
    delay(3000); 

    if (modem.gprsConnect(apn)) {
        Serial.println("[Network] Cellular data attached successfully.");
    }

    // TLS configuration overrides
    modem.sendAT("+CSSLCFG=\"authmode\",0,0"); modem.waitResponse();
    modem.sendAT("+CSSLCFG=\"authmode\",1,0"); modem.waitResponse();
    modem.sendAT("+CSSLCFG=\"sslversion\",0,3"); modem.waitResponse();
    modem.sendAT("+CSSLCFG=\"sslversion\",1,3"); modem.waitResponse();
    modem.sendAT("+CSSLCFG=\"sni\",0,\"" + String(mqtt_server) + "\""); modem.waitResponse();
    modem.sendAT("+CSSLCFG=\"sni\",1,\"" + String(mqtt_server) + "\""); modem.waitResponse();

    syncNTP();
    mqtt.setServer(mqtt_server, mqtt_port); 
    mqtt.setSocketTimeout(30); 
}

void loop() {
    // --- Network Guard & Auto-Reconnect ---
    if (!modem.isGprsConnected()) {
        if (millis() - lastGprsAttempt > 20000) {
            lastGprsAttempt = millis();
            modem.gprsDisconnect(); 
            delay(2000);
            modem.gprsConnect(apn);
        }
    } 
    else if (!mqtt.connected()) {
        if (millis() - lastReconnectAttempt > 10000) {
            lastReconnectAttempt = millis();
            char clientId[32];
            snprintf(clientId, sizeof(clientId), "NetOneNode_%04lX", random(0xffff));
            mqtt.connect(clientId, mqtt_user, mqtt_pass);
        }
    } else {
        mqtt.loop();
    }

    // --- 10-Second Sensor Request Pulse ---
    if (millis() - lastRequest > 10000) {
        lastRequest = millis();
        byte cmd[] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
        SerialSensor.write(cmd, 9);
        
        if (millis() - lastTimeSync > 60000) {
            updateNetworkTime();
            lastTimeSync = millis();
        }
    }

    // --- Industrial Single-Byte State Machine Parser ---
    while (SerialSensor.available() > 0) {
        uint8_t c = SerialSensor.read();
        
        // Step 1: Align frame start marker
        if (dataIndex == 0 && c != 0xFF) {
            continue; 
        }
        
        dataBuf[dataIndex++] = c;
        
        // Step 2: Validate command byte header alignment
        if (dataIndex == 2 && dataBuf[1] != 0x86) {
            dataIndex = 0; // Drop frame instantly and look for next 0xFF
            continue;
        }
        
        // Step 3: Complete 26-byte frame parsing
        if (dataIndex >= 26) {
            dataIndex = 0; // Reset state machine instantly
            
            if (checkSensorChecksum(dataBuf)) {
                // Extract metrics safely without frame shifting
                pm25 = (uint16_t)dataBuf[4] << 8 | dataBuf[5];
                pm10 = (uint16_t)dataBuf[6] << 8 | dataBuf[7];
                co2  = (uint16_t)dataBuf[8] << 8 | dataBuf[9];
                temp = ((((uint16_t)dataBuf[11] << 8) | dataBuf[12]) - 500.0f) * 0.1f;
                hum  = ((uint16_t)dataBuf[13] << 8 | dataBuf[14]);

                uint8_t tvoc_grade = dataBuf[10]; 
                float ch2o  = ((uint16_t)dataBuf[15] << 8 | dataBuf[16]) * 0.001f; 
                float co    = ((uint16_t)dataBuf[17] << 8 | dataBuf[18]) * 0.1f;   
                float o3    = ((uint16_t)dataBuf[19] << 8 | dataBuf[20]) * 0.01f;  
                float no2   = ((uint16_t)dataBuf[21] << 8 | dataBuf[22]) * 0.01f;  

                int currentAQI = calculateAQI(pm25);

                // --- Production Fixed-Width LCD Driver Engine ---
                char lcdLine[21]; // Buffer for exactly 20 characters + null terminator

                // Line 0: Temperature and Humidity
                snprintf(lcdLine, sizeof(lcdLine), "Temp:%4.1fC Hum:%3.0f%%", temp, hum);
                lcd.setCursor(0, 0); lcd.print(lcdLine);

                // Line 1: CO2 and PM2.5 (Fixed width avoids truncation)
                snprintf(lcdLine, sizeof(lcdLine), "CO2:%4d  PM25:%3d  ", co2, pm25);
                lcd.setCursor(0, 1); lcd.print(lcdLine);

                // Line 2: Time Sync
                snprintf(lcdLine, sizeof(lcdLine), "TIME: %-13s", netTime.substring(0, 13).c_str());
                lcd.setCursor(0, 2); lcd.print(lcdLine);

                // Line 3: MQTT Status Engine and AQI
                char mqttStatus[8];
                if (mqtt.connected()) {
                    strcpy(mqttStatus, "OK");
                } else {
                    snprintf(mqttStatus, sizeof(mqttStatus), "MQ:%d", mqtt.state());
                }
                snprintf(lcdLine, sizeof(lcdLine), "MQTT:%-5s  AQI:%-3d ", mqttStatus, currentAQI);
                lcd.setCursor(0, 3); lcd.print(lcdLine);

                // --- JSON Dispatch Payload Engine ---
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
                    mqtt.publish(mqtt_topic, jb);
                }
            } else {
                Serial.println("[Sensor] Frame Corrupted / Checksum Failed");
            }
        }
    }
}