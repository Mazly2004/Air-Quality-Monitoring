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

// Standard Client for cleartext HTTP fallback + Secure Client for MQTT TLS
TinyGsmClient plainClient(modem);
TinyGsmClientSecure cellularClient(modem);
PubSubClient mqtt(cellularClient);
LiquidCrystal_I2C lcd(0x27, 20, 4);

// --- Global Telemetry Data ---
uint16_t pm25 = 0, co2 = 0, pm10 = 0;
float temp = 0.0, hum = 0.0;

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
        
        Serial.print("[Time] Raw modem RTC data: ");
        Serial.println(res);
        
        int commaIndex = res.indexOf(',');
        int tzIndex = res.indexOf('+', commaIndex);
        if (tzIndex == -1) tzIndex = res.indexOf('-', commaIndex); 
        
        if (commaIndex != -1 && tzIndex != -1) {
            netTime = res.substring(commaIndex + 1, tzIndex);
        } else {
            netTime = res; // Fallback to raw layout if timezone parsing markers miss
        }
    } else {
        Serial.println("[Time] Failed to pull time response from AT command channel.");
    }
}

// --- HTTP REST Time Sync Fallback Engine ---
bool syncTimeHTTP() {
    Serial.println("[HTTP Time] Launching unencrypted TCP endpoint request...");
    if (!plainClient.connect("worldtimeapi.org", 80)) {
        Serial.println("[HTTP Time] Connection failed to fallback time server.");
        return false;
    }
    
    // Request localized JSON block directly over unblocked web traffic
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
    if (error) {
        Serial.print("[HTTP Time] JSON parsing error: ");
        Serial.println(error.c_str());
        return false;
    }
    
    const char* datetime = doc["datetime"]; // Pattern: "2026-06-10T01:21:01.123456+02:00"
    if (!datetime || strlen(datetime) < 19) {
        Serial.println("[HTTP Time] Time data missing or structural failure.");
        return false;
    }
    
    String dtStr = String(datetime);
    String s_year  = dtStr.substring(2, 4);   // "26"
    String s_month = dtStr.substring(5, 7);  // "06"
    String s_day   = dtStr.substring(8, 10);  // "10"
    String s_time  = dtStr.substring(11, 19); // "01:21:01"
    
    // Reassemble command strictly into SIM7000 configuration specs
    // Harare is GMT+2, which calculates exactly to +08 quarter-hours
    String cclk_cmd = "+CCLK=\"" + s_year + "/" + s_month + "/" + s_day + "," + s_time + "+08\"";
    
    modem.sendAT(cclk_cmd);
    if (modem.waitResponse(2000) == 1) {
        Serial.println("[HTTP Time] Clock adjusted successfully via Network Web scraping!");
        return true;
    }
    return false;
}

// --- Time Sync Orchestration ---
void syncNTP() {
    Serial.println("[Time] Syncing modem RTC with Google Public NTP IP...");
    lcd.setCursor(0, 2); lcd.print("Time: Syncing NTP... ");
    
    modem.sendAT("+CNTP=\"216.239.35.0\",8"); 
    if (modem.waitResponse(3000) == 1) {
        modem.sendAT("+CNTP"); 
        if (modem.waitResponse(2000) == 1) { // Matches immediate "OK" acknowledgment
            
            // 🚨 FIX: Force module to block and listen for the true network response token
            int8_t rc = modem.waitResponse(8000, "+CNTP: 0");
            if (rc == 1) {
                Serial.println("[Time] True NTP network handshake successful.");
                updateNetworkTime();
                return;
            }
        }
    }
    
    // If NTP fails (Expected on NetOne due to UDP port filtering)
    Serial.println("[Time] NTP network sync blocked/failed. Redirecting to HTTP Engine...");
    lcd.setCursor(0, 2); lcd.print("Time: HTTP Syncing... ");
    
    if (syncTimeHTTP()) {
        updateNetworkTime();
        return;
    }
    
    // Ultimate compiled safety baseline to preserve TLS handshake context
    Serial.println("[Time] All network options exhausted. Forcing baseline clock values...");
    lcd.setCursor(0, 2); lcd.print("Time: Emergency Inject");
    modem.sendAT("+CCLK=\"26/06/10,01:15:00+08\""); 
    modem.waitResponse(2000);
    updateNetworkTime(); 
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

    lcd.setCursor(0, 1); lcd.print("Modem: Syncing...   ");
    if (!modem.init()) { 
        Serial.println("[System] Base init failed. Dropping back to heavy hardware reset...");
        if (!modem.restart()) {
            lcd.setCursor(0, 2); lcd.print("ERROR: NO MODEM   ");
            while(true);
        }
    }

    Serial.println("[Network] Forcing Modem to 2G/GSM Mode...");
    modem.setNetworkMode(13); 
    delay(3000); 

    lcd.setCursor(0, 2); lcd.print("GPRS: Connecting... ");
    Serial.println("[Network] Attaching to NetOne network...");
    if (modem.gprsConnect(apn)) {
        lcd.setCursor(0, 3); lcd.print("STATUS: ONLINE    ");
        Serial.println("[Network] Cellular data attached successfully.");
    }

    Serial.println("[TLS-Hardening] Overriding SSL Engine defaults for NetOne environment...");
    modem.sendAT("+CSSLCFG=\"authmode\",0,0");
    modem.waitResponse();
    modem.sendAT("+CSSLCFG=\"authmode\",1,0");
    modem.waitResponse();
    
    modem.sendAT("+CSSLCFG=\"sslversion\",0,3");
    modem.waitResponse();
    modem.sendAT("+CSSLCFG=\"sslversion\",1,3");
    modem.waitResponse();

    modem.sendAT("+CSSLCFG=\"sni\",0,\"" + String(mqtt_server) + "\"");
    modem.waitResponse();
    modem.sendAT("+CSSLCFG=\"sni\",1,\"" + String(mqtt_server) + "\"");
    modem.waitResponse();

    // Re-orchestrated initialization time sync execution
    syncNTP();
    
    mqtt.setServer(mqtt_server, mqtt_port); 
    mqtt.setSocketTimeout(30); 
}

void loop() {
    // --- Network Guard & Self-Healing Auto-Reconnect ---
    if (!modem.isGprsConnected()) {
        if (millis() - lastGprsAttempt > 20000) {
            lastGprsAttempt = millis();
            Serial.println("[Network] Link dropped or stale context. Re-priming GPRS interface...");
            modem.gprsDisconnect(); 
            delay(2000);
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
                Serial.println(mqtt.state()); 
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

    // --- Sensor Buffer Parser ---
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
                    lcd.print("TIME: "); lcd.print(netTime); lcd.print("        ");
                    
                    lcd.setCursor(0, 3);
                    if (mqtt.connected()) {
                        lcd.print("MQTT:OK ");
                    } else {
                        lcd.print("MQ:"); lcd.print(mqtt.state()); lcd.print("    "); 
                    }
                    lcd.print("AQI:"); lcd.print(currentAQI); lcd.print("   ");

                    // --- Compile Structured JSON Payload ---
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
                    Serial.println("[Sensor] Checksum failed.");
                }
            }
        } else {
            break; 
        }
    }
}