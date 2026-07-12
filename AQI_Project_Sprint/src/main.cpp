#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>
#include <TinyGsmClient.h>

// Bumped to 768 to comfortably fit 512-byte JSON strings + MQTT protocol/topic overhead
#define MQTT_MAX_PACKET_SIZE 768
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- Configuration ---
const char apn[] = "econet.net";

// BROKER: Private EMQX Serverless Cluster 
const char* mqtt_server = "ya4f6956.ala.eu-central-1.emqxsl.com"; 
const int mqtt_port = 8883; // MUST be 8883 for EMQX Serverless
const char* mqtt_user = "harare_esp32_client"; 
const char* mqtt_pass = "Langton@emqx$#"; 
const char* mqtt_topic = "td_aqm/fixed/node/esp32_02/data"; 

// --- ThingSpeak Configuration ---
const char* ts_server = "api.thingspeak.com";
const char* ts_api_key = "FND4XOKB1LXD9C4N"; // Your Write API Key

// --- Pinout (LilyGo T-SIM7000G) ---
#define MODEM_TX     27
#define MODEM_RX     26
#define MODEM_PWR    4   
#define SENSOR_TX    33
#define SENSOR_RX    32
#define I2C_SDA      21  
#define I2C_SCL      22  

#define SPI_SCK      14
#define SPI_MISO     2
#define SPI_MOSI     15
#define SD_CS        13

// --- Objects ---
HardwareSerial SerialAT(1);      
HardwareSerial SerialSensor(2);  
TinyGsm modem(SerialAT);

TinyGsmClientSecure cellularClient(modem); // Primary Secure MQTT (EMQX)
TinyGsmClient tsClient(modem);             // Secondary HTTP (ThingSpeak - Port 80)
PubSubClient mqtt(cellularClient);
LiquidCrystal_I2C lcd(0x27, 20, 4);

// --- Global Telemetry Data ---
uint16_t pm25 = 0, co2 = 0, pm10 = 0;
float temp = 0.0, hum = 0.0;
const float lat = -17.8700;
const float lon = 30.9000;
char netTime[24] = "Syncing..."; 
uint32_t msgIndex = 1;
bool sdCardReady = false; 

// --- EDGE AI & ANOMALY DETECTION ---
const float LIMIT_PM25 = 15.0;  
const int WINDOW_SIZE = 10;
float pm25_history[WINDOW_SIZE];
int history_idx = 0;
int readings_count = 0;
const float MAD_THRESHOLD_MULTIPLIER = 3.0; 

// --- Timers & Safe Watchdogs ---
unsigned long lastRequest = 0;
unsigned long lastReconnectAttempt = 0; 
unsigned long lastGprsAttempt = 0;
unsigned long lastTimeSync = 0;
uint8_t dataBuf[26];

uint8_t mqttTimeoutCounter = 0;
const uint8_t MAX_MQTT_THRESH = 3; 
const unsigned long SEND_INTERVAL = 300000UL; // 5 Minutes

// --- ALGORITHMS ---

int heaviside(float value, float limit) {
    return (value >= limit) ? 1 : 0;
}

float getMedian(float data[], int size) {
    float tempArray[size];
    memcpy(tempArray, data, size * sizeof(float));
    for(int i = 0; i < size - 1; i++) {
        for(int j = i + 1; j < size; j++) {
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
    for(int i = 0; i < WINDOW_SIZE; i++) {
        deviations[i] = abs(pm25_history[i] - median);
    }
    float mad = getMedian(deviations, WINDOW_SIZE);
    if (mad == 0) mad = 1.0; 
    float current_deviation = abs(newValue - median);
    return (current_deviation > (MAD_THRESHOLD_MULTIPLIER * mad)) ? 1 : 0;
}

bool isModemAwake() {
    for (int i = 0; i < 4; i++) {
        if (modem.testAT(500)) return true;
        delay(100);
    }
    return false;
}

void powerModemResilient() {
    pinMode(MODEM_PWR, OUTPUT);
    Serial.println("[Power] Interrogating modem state...");
    lcd.setCursor(0, 1); lcd.print("Modem: Checking...  ");
    
    if (isModemAwake()) {
        Serial.println("[Power] Modem is already online! Safe-skipping toggle sequence.");
        lcd.setCursor(0, 1); lcd.print("Modem: Already ON    ");
        return;
    }

    Serial.println("[Power] No response. Launching Pulse Sequence A...");
    lcd.setCursor(0, 1); lcd.print("Modem: Powering A...");
    digitalWrite(MODEM_PWR, HIGH);
    delay(300);
    digitalWrite(MODEM_PWR, LOW);
    delay(4000); 

    if (isModemAwake()) {
        Serial.println("[Power] Hardware initialized via Sequence A.");
        return;
    }

    Serial.println("[Power] Still dark. Launching Fallback Sequence B...");
    lcd.setCursor(0, 1); lcd.print("Modem: Powering B...");
    digitalWrite(MODEM_PWR, LOW);
    delay(1000);
    digitalWrite(MODEM_PWR, HIGH);
    delay(4000); 
    
    if (isModemAwake()) {
        Serial.println("[Power] Hardware initialized via Sequence B.");
        return;
    }
    Serial.println("[Power] WARNING: Physical lines unresponsive.");
}

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
        char res[64];
        size_t len = modem.stream.readBytesUntil('\n', res, sizeof(res) - 1);
        res[len] = '\0'; 
        
        char* start = strchr(res, '"'); 
        if (start) {
            start++; 
        } else {
            start = res; 
        }
        
        int year, month, day, hour, minute, second;
        
        if (sscanf(start, "%d/%d/%d,%d:%d:%d", &year, &month, &day, &hour, &minute, &second) == 6) {
            snprintf(netTime, sizeof(netTime), "20%02d-%02d-%02d %02d:%02d:%02d", 
                     year, month, day, hour, minute, second);
        } else {
            char* tzIndex = strchr(start, '+'); 
            if (!tzIndex) tzIndex = strchr(start, '-'); 
            if (tzIndex != nullptr) *tzIndex = '\0'; 
            
            char* commaIndex = strchr(start, ',');
            if (commaIndex != nullptr) *commaIndex = ' '; 
            
            char* endQuote = strchr(start, '"');
            if (endQuote != nullptr) *endQuote = '\0';
            
            strlcpy(netTime, start, sizeof(netTime)); 
        }
    }
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
    lcd.print("Budiriro Node");  

    Serial.print("[System] Initializing SD Card...");
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SD_CS);
    
    if (!SD.begin(SD_CS, SPI)) {
        Serial.println(" FAILED!");
        lcd.setCursor(0, 1); lcd.print("SD Card FAILED!     ");
        sdCardReady = false;
        delay(2000);
    } else {
        Serial.println(" OK.");
        sdCardReady = true;
        File dataFile = SD.open("/datalog.csv", FILE_APPEND);
        if (dataFile) {
            if (dataFile.size() == 0) {
                dataFile.println("Index,Timestamp,AQI,PM2.5,PM10,CO2,TVOC_Grade,CH2O,CO,O3,NO2,Temp,Hum,MAD_Spike,WHO_Limit");
            }
            dataFile.close();
        }
    }

    randomSeed(analogRead(0));
    powerModemResilient();

    lcd.setCursor(0, 1); lcd.print("Modem: Syncing...   ");
    if (!modem.init()) { 
        Serial.println("[System] Base init failed.");
        if (!modem.restart()) {
            lcd.setCursor(0, 2); lcd.print("ERROR: NO MODEM   ");
            while(true);
        }
    }

    Serial.println("[Network] Forcing Modem to 2G/GSM Mode...");
    modem.setNetworkMode(13); 
    delay(3000); 

    lcd.setCursor(0, 2); lcd.print("GPRS: Connecting... ");
    Serial.println("[Network] Attaching to Econet network...");
    
    if (modem.gprsConnect(apn)) {
        lcd.setCursor(0, 3); lcd.print("STATUS: ONLINE    ");
        Serial.println("[Network] Cellular data attached successfully.");
        syncNTP(); 
    } else {
        Serial.println("[Network] Initial GPRS attachment failed. Will retry dynamically in loop.");
    }

    mqtt.setServer(mqtt_server, mqtt_port); 
    mqtt.setSocketTimeout(30); 
}

void loop() {
    if (!modem.isGprsConnected()) {
        if (millis() - lastGprsAttempt > 20000) {
            lastGprsAttempt = millis();
            Serial.println("[Network] Link dropped. Repairing GPRS...");
            if (modem.gprsConnect(apn)) {
                syncNTP(); 
            }
        }
    } 
    else if (!mqtt.connected()) {
        if (millis() - lastReconnectAttempt > 10000) {
            lastReconnectAttempt = millis();
            Serial.print("[MQTT] Connecting to secure cloud cluster... ");
            
            char clientId[32];
            snprintf(clientId, sizeof(clientId), "Budiriro_%04lX", random(0xffff));
            
            if (mqtt.connect(clientId, mqtt_user, mqtt_pass)) {
                Serial.println("CONNECTED SUCCESSFULLY!");
                mqttTimeoutCounter = 0; 
            } else {
                int8_t errState = mqtt.state();
                Serial.print("FAILED, rc=");
                Serial.println(errState); 
                
                if (errState == -4 || errState == -2) { 
                    mqttTimeoutCounter++;
                    Serial.print("[System] Watchdog: Consecutive -4/-2 failures = ");
                    Serial.println(mqttTimeoutCounter);
                    
                    if (mqttTimeoutCounter >= MAX_MQTT_THRESH) {
                        Serial.println("[CRITICAL] EMQX TLS rejected. Bypassing EMQX so ThingSpeak can run...");
                        lcd.clear();
                        lcd.setCursor(0, 0); lcd.print("EMQX TLS REJECTED");
                        lcd.setCursor(0, 1); lcd.print("Bypassing for 10m");
                        delay(2000); // Give user a moment to read it before sensor updates LCD
                        
                        // Bypass EMQX for 10 minutes so ThingSpeak gets priority
                        lastReconnectAttempt = millis() + 600000; 
                        mqttTimeoutCounter = 0; 
                    }
                } else {
                    mqttTimeoutCounter = 0;
                }
            }
        }
    } else {
        mqtt.loop();
    }

    // Timer logic to fetch sensor data
    if (millis() - lastRequest > SEND_INTERVAL) {
        lastRequest = millis();
        
        while (SerialSensor.available()) {
            SerialSensor.read();
        }

        byte cmd[] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
        SerialSensor.write(cmd, 9);
        
        if (millis() - lastTimeSync > 60000) {
            updateNetworkTime();
            lastTimeSync = millis();
        }
    }

    // Read and parse sensor data
    while (SerialSensor.available() > 0) {
        if (SerialSensor.peek() != 0xFF) {
            SerialSensor.read(); 
            continue;
        }

        if (SerialSensor.available() >= 26) {
            SerialSensor.readBytes(dataBuf, 26);
            if (dataBuf[1] == 0x86) {
                if (checkSensorChecksum(dataBuf)) {
                    // 1. Map Data
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
                    int mad_flag_pm25 = calculateMadAnomaly((float)pm25);
                    int h_flag_pm25 = heaviside((float)pm25, LIMIT_PM25);

                    pm25_history[history_idx] = (float)pm25;
                    history_idx = (history_idx + 1) % WINDOW_SIZE;
                    if (readings_count < WINDOW_SIZE) readings_count++;

                    // 2. LCD Display
                    lcd.setCursor(0, 0);
                    lcd.print("T:"); lcd.print(temp, 1); lcd.print("C H:"); lcd.print(hum, 0); lcd.print("%   ");
                    lcd.setCursor(0, 1);
                    lcd.print("CO2:"); lcd.print(co2); lcd.print("  PM2.5:"); lcd.print(pm25);
                    lcd.setCursor(0, 2);
                    lcd.print("T: "); lcd.print(netTime);
                    
                    lcd.setCursor(0, 3);
                    if (mqtt.connected()) {
                        lcd.print("MQ:OK ");
                    } else {
                        lcd.print("MQ:"); lcd.print(mqtt.state()); lcd.print(" "); 
                    }
                    lcd.print("AQI:"); lcd.print(currentAQI); 
                    lcd.print(" #"); lcd.print(msgIndex);

                    // 3. SD Card Log
                    if (sdCardReady) {
                        File dataFile = SD.open("/datalog.csv", FILE_APPEND);
                        if (dataFile) {
                            dataFile.print(msgIndex); dataFile.print(",");
                            dataFile.print(netTime); dataFile.print(",");
                            dataFile.print(currentAQI); dataFile.print(",");
                            dataFile.print(pm25); dataFile.print(",");
                            dataFile.print(pm10); dataFile.print(",");
                            dataFile.print(co2); dataFile.print(",");
                            dataFile.print(tvoc_grade); dataFile.print(",");
                            dataFile.print(ch2o, 3); dataFile.print(",");
                            dataFile.print(co, 1); dataFile.print(",");
                            dataFile.print(o3, 2); dataFile.print(",");
                            dataFile.print(no2, 2); dataFile.print(",");
                            dataFile.print(temp, 1); dataFile.print(",");
                            dataFile.print(hum, 0); dataFile.print(",");
                            dataFile.print(mad_flag_pm25); dataFile.print(",");
                            dataFile.println(h_flag_pm25);
                            dataFile.close();
                            Serial.println("[SD] Row appended to datalog.csv");
                        }
                    }

                    // 4. EMQX Publish (If Connected)
                    if (mqtt.connected()) {
                        JsonDocument doc;
                        doc["msg_idx"] = msgIndex; 
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

                        char jb[512]; 
                        serializeJson(doc, jb);
                        
                        if (mqtt.publish(mqtt_topic, jb)) {
                            Serial.println("[MQTT] EMQX Telemetry Dispatched.");
                        }
                    } else {
                        Serial.println("[MQTT] Device Offline. Local save evaluated.");
                    }

                    // 5. THINGSPEAK DISPATCH (ALWAYS ATTEMPTED)
                    Serial.print("[ThingSpeak] Connecting... ");
                    if (tsClient.connect(ts_server, 80)) {
                        char tsRequest[384];
                        snprintf(tsRequest, sizeof(tsRequest), 
                                 "GET /update?api_key=%s&field1=%d&field2=%d&field3=%d&field4=%.1f&field5=%.0f&field6=%d HTTP/1.1\r\n"
                                 "Host: %s\r\n"
                                 "Connection: close\r\n\r\n", 
                                 ts_api_key, pm25, pm10, co2, temp, hum, currentAQI, ts_server);
                                 
                        tsClient.print(tsRequest);
                        delay(10); 
                        tsClient.stop();
                        Serial.println("OK.");
                    } else {
                        Serial.println("FAILED.");
                    }

                    // Increment universal message counter
                    msgIndex++; 
                }
            }
        } else {
            break; 
        }
    }
}