// #define TINY_GSM_MODEM_SIM7000 // Commented out to prevent PlatformIO redefinition warning

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- Configuration ---
const char apn[] = "internet.netone";

// BROKER: EMQX Public Broker (Consider migrating to private for production)
const char* mqtt_server = "broker.emqx.io"; 
const int mqtt_port = 1883;
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
TinyGsmClient cellularClient(modem);
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

// --- Time Sync Logic ---
void syncNTP() {
    Serial.println("[Time] Syncing modem RTC with NTP Server...");
    // 8 quarters of an hour = UTC+2 (Central Africa Time)
    modem.sendAT("+CNTP=\"pool.ntp.org\",8"); 
    modem.waitResponse();
    
    modem.sendAT("+CNTP"); // Execute the network time sync
    modem.waitResponse(10000); // Give it up to 10 seconds to fetch the time
}

void updateNetworkTime() {
    modem.sendAT("+CCLK?");
    if (modem.waitResponse(2000, "+CCLK: ") == 1) {
        String res = modem.stream.readStringUntil('\n');
        res.trim();
        
        // The modem returns time in format: "YY/MM/DD,HH:MM:SS+TZ"
        // We want to extract just the "HH:MM:SS" part
        int commaIndex = res.indexOf(',');
        int tzIndex = res.indexOf('+', commaIndex);
        if (tzIndex == -1) tzIndex = res.indexOf('-', commaIndex); 
        
        if (commaIndex != -1 && tzIndex != -1) {
            netTime = res.substring(commaIndex + 1, tzIndex);
        }
    }
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
    SerialSensor.setTimeout(100); // 100ms timeout for sensor reading

    // 2. Local Visuals Setup
    Wire.begin(I2C_SDA, I2C_SCL);
    lcd.init(); lcd.backlight();
    lcd.print("NETONE MOBILE IOT");

    // Seed randomness for distinct MQTT identification hashes
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

    // Force 2G/GPRS Mode
    Serial.println("[Network] Forcing Modem to 2G/GSM Mode...");
    modem.setNetworkMode(13); // 13 = GSM/2G Only. 38 = LTE-M/NB-IoT. 2 = Auto.
    delay(3000); 

    // 4. Attach Cellular Data Network
    lcd.setCursor(0, 2); lcd.print("GPRS: Connecting... ");
    Serial.println("[Network] Attaching to NetOne network...");
    if (modem.gprsConnect(apn)) {
        lcd.setCursor(0, 3); lcd.print("STATUS: ONLINE   ");
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
    // --- Safe, Collision-Proof MQTT Connection Sequence ---
    else if (!mqtt.connected()) {
        if (millis() - lastReconnectAttempt > 10000) {
            lastReconnectAttempt = millis();
            Serial.print("[MQTT] Routing packets to public cloud broker... ");
            
            char clientId[32];
            snprintf(clientId, sizeof(clientId), "NetOneNode_%04lX", random(0xffff));
            
            if (mqtt.connect(clientId)) {
                Serial.println("CONNECTED SUCCESSFULLY!");
            } else {
                Serial.print("FAILED, Error State rc=");
                Serial.print(mqtt.state()); 
                Serial.println(" (Check airtime credit or telco tower data blocking)");
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
        
        // Fetch the cellular time every 60 seconds
        if (millis() - lastTimeSync > 60000) {
            updateNetworkTime();
            lastTimeSync = millis();
        }
    }

    // --- High-Performance Sensor Buffer Parser ---
    while (SerialSensor.available() > 0) {
        if (SerialSensor.peek() != 0xFF) {
            SerialSensor.read(); // Drop stray asynchronous framing debris
            continue;
        }

        if (SerialSensor.available() >= 26) {
            SerialSensor.readBytes(dataBuf, 26);
            if (dataBuf[1] == 0x86) {
                
                // Verify checksum before updating data
                if (checkSensorChecksum(dataBuf)) {
                    // Read and assemble binary registers
                    pm25 = (uint16_t)dataBuf[4] << 8 | dataBuf[5];
                    co2  = (uint16_t)dataBuf[8] << 8 | dataBuf[9];
                    temp = ((((uint16_t)dataBuf[11] << 8) | dataBuf[12]) - 500.0f) * 0.1f;
                    hum  = ((uint16_t)dataBuf[13] << 8 | dataBuf[14]);

                    // --- Print Updates to Local LCD Matrix ---
                    lcd.setCursor(0, 0);
                    lcd.print("T:"); lcd.print(temp, 1); lcd.print("C H:"); lcd.print(hum, 0); lcd.print("%   ");
                    lcd.setCursor(0, 1);
                    lcd.print(" CO2:"); lcd.print(co2); lcd.print("  ");lcd.print("PM2.5:"); lcd.print(pm25);
                    lcd.setCursor(0, 2);
                    lcd.print("TIME:  "); lcd.print(netTime);
                    
                    // Smart Diagnostic LCD Output
                    lcd.setCursor(0, 3);
                    if (mqtt.connected()) {
                        lcd.print("MQTT:OK ");
                    } else {
                        lcd.print("MQ:"); lcd.print(mqtt.state()); lcd.print("   "); 
                    }
                    lcd.print("Sig:"); lcd.print(modem.getSignalQuality()); lcd.print("  ");

                    // --- Compile Structured JSON Payload for Telegraf & Grafana ---
                    if (mqtt.connected()) {
                        JsonDocument doc;
                        doc["pm25"] = pm25;
                        doc["co2"]  = co2;
                        doc["temp"] = temp; 
                        doc["hum"]  = hum;  
                        doc["lat"]  = lat;
                        doc["lon"]  = lon;
                        doc["time"] = netTime; 
                        
                        char jb[128]; 
                        serializeJson(doc, jb);
                        
                        if (mqtt.publish(mqtt_topic, jb)) {
                            Serial.println("[MQTT] Payload safely dispatched to cloud bridge.");
                        } else {
                            Serial.println("[MQTT] Warning: Packet dropped at transmission interface.");
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