#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// --- Configuration ---
const char apn[] = "internet.netone";
const char* mqtt_server = "10.26.234.148";
const char* mqtt_topic = "sensors/indoor/esp32_02";

// --- Pinout (LilyGo T-SIM7000G) ---
#define MODEM_TX     27
#define MODEM_RX     26
#define MODEM_PWR    4   
#define SENSOR_TX    17
#define SENSOR_RX    16
#define I2C_SDA      21  // Back to standard
#define I2C_SCL      22  // Back to standard

// --- Objects ---
HardwareSerial SerialAT(1);      // SIM7000G
HardwareSerial SerialSensor(2);  // ZPHS01B
TinyGsm modem(SerialAT);
TinyGsmClient cellularClient(modem);
PubSubClient mqtt(cellularClient);
LiquidCrystal_I2C lcd(0x27, 20, 4);

// --- Global Data ---
uint16_t pm25, co2, pm10;
float temp, hum, lat = 0.0, lon = 0.0;
String gpsTime = "Searching...";
unsigned long lastRequest = 0;
uint8_t dataBuf[26];

// --- Functions ---
void powerModem() {
    pinMode(MODEM_PWR, OUTPUT);
    digitalWrite(MODEM_PWR, LOW); delay(100);
    digitalWrite(MODEM_PWR, HIGH); delay(1000); 
    digitalWrite(MODEM_PWR, LOW);
}

void updateGPS() {
    float g_lat, g_lon, g_speed, g_alt;
    int g_y, g_m, g_d, g_h, g_min, g_s;
    if (modem.getGPS(&g_lat, &g_lon, &g_speed, &g_alt, &g_y, &g_m, &g_d, &g_h, &g_min, &g_s)) {
        lat = g_lat; lon = g_lon;
        char tBuf[16];
        snprintf(tBuf, sizeof(tBuf), "%02d:%02d:%02d", g_h, g_min, g_s);
        gpsTime = String(tBuf);
    }
}

void setup() {
    Serial.begin(115200);
    SerialAT.begin(9600, SERIAL_8N1, MODEM_RX, MODEM_TX);
    SerialSensor.begin(9600, SERIAL_8N1, SENSOR_RX, SENSOR_TX);

    Wire.begin(I2C_SDA, I2C_SCL);
    lcd.init(); lcd.backlight();
    lcd.print("NETONE MOBILE IOT");

    powerModem();
    delay(2000);

    lcd.setCursor(0, 1); lcd.print("Modem: Starting...");
    if (!modem.restart()) {
        lcd.setCursor(0, 2); lcd.print("ERROR: NO MODEM");
        while(true);
    }

    lcd.setCursor(0, 2); lcd.print("GPRS: Connecting...");
    if (modem.gprsConnect(apn)) {
        lcd.setCursor(0, 3); lcd.print("STATUS: ONLINE   ");
    }

    modem.sendAT("+CGNSPWR=1"); // Enable GPS
    mqtt.setServer(mqtt_server, 1883);
}

void loop() {
    if (!modem.isGprsConnected()) modem.gprsConnect(apn);
    if (!mqtt.connected()) mqtt.connect("NetOne_Mobile_Node");
    mqtt.loop();

    // 10-second data cycle
    if (millis() - lastRequest > 10000) {
        lastRequest = millis();
        byte cmd[] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
        SerialSensor.write(cmd, 9);
        updateGPS();
    }

    // Process Sensor Serial
    if (SerialSensor.available() >= 26) {
        SerialSensor.readBytes(dataBuf, 26);
        if (dataBuf[0] == 0xFF && dataBuf[1] == 0x86) {
            // Parse Values
            pm25 = (uint16_t)dataBuf[4] << 8 | dataBuf[5];
            co2  = (uint16_t)dataBuf[8] << 8 | dataBuf[9];
            temp = ((((uint16_t)dataBuf[11] << 8) | dataBuf[12]) - 500.0f) * 0.1f;
            hum  = ((uint16_t)dataBuf[13] << 8 | dataBuf[14]);

            // --- LCD Layout ---
            // Row 0: Environment
            lcd.setCursor(0, 0);
            lcd.print("T:"); lcd.print(temp, 1); lcd.print("C H:"); lcd.print(hum, 0); lcd.print("%   ");
            
            // Row 1: Air Quality
            lcd.setCursor(0, 1);
            lcd.print("PM2.5:"); lcd.print(pm25); lcd.print(" CO2:"); lcd.print(co2); lcd.print("  ");
            
            // Row 2: Location
            lcd.setCursor(0, 2);
            lcd.print("Lat:"); lcd.print(lat, 4); lcd.print(" "); lcd.print(gpsTime);

            // Row 3: Network Status
            lcd.setCursor(0, 3);
            lcd.print(mqtt.connected() ? "MQTT:OK" : "MQTT:ERR");
            lcd.print(" Sig:"); lcd.print(modem.getSignalQuality()); lcd.print("/31 ");

            // --- MQTT Publish ---
            JsonDocument doc;
            doc["pm25"] = pm25;
            doc["co2"] = co2;
            doc["lat"] = lat;
            doc["lon"] = lon;
            char jb[128];
            serializeJson(doc, jb);
            mqtt.publish(mqtt_topic, jb);
        }
    }
}