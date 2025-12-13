# Air Quality Web App

This adds a FastAPI backend and a lightweight dashboard to view your ESP32 ZPHS01B readings in real-time.

## Structure
- `backend/` FastAPI app (REST + WebSocket)
- `frontend/` Static dashboard (served at `/app`)

## Run backend

```powershell
# From the backend folder
cd "c:\Users\USER\OneDrive\Documents\PlatformIO\Projects\Air Quality Monitoring\webapp\backend"
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
uvicorn main:app --reload --host 0.0.0.0 --port 8000
```

Open http://localhost:8000/app in your browser.

## ESP32 → Backend (HTTP POST)

Add WiFi + POST code to your ESP32 sketch to push readings:

```cpp
#include <WiFi.h>
#include <HTTPClient.h>

const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";
const char* API_URL   = "http://192.168.1.100:8000/api/measurements"; // change to your PC IP

void wifiSetup(){
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while(WiFi.status()!=WL_CONNECTED){ delay(500); Serial.print("."); }
  Serial.println("\nWiFi connected");
}

void sendMeasurement(int pm25,int pm10,int co2,int tvoc,float temp,float humidity,int hcho,int o3,int no2){
  if(WiFi.status()==WL_CONNECTED){
    HTTPClient http;
    http.begin(API_URL);
    http.addHeader("Content-Type","application/json");
    String payload = String("{")+
      "\"pm25\":"+pm25+","+
      "\"pm10\":"+pm10+","+
      "\"co2\":"+co2+","+
      "\"tvoc\":"+tvoc+","+
      "\"temp_c\":"+String(temp,1)+","+
      "\"humidity\":"+String(humidity,1)+","+
      "\"hcho\":"+hcho+","+
      "\"o3\":"+o3+","+
      "\"no2\":"+no2+","+
      "\"device_id\":\"esp32-1\"}";
    int code = http.POST(payload);
    Serial.printf("POST /api/measurements -> %d\n", code);
    http.end();
  }
}
```

Call `wifiSetup()` once in `setup()`. Then whenever you parse a packet, call `sendMeasurement(pm2_5, pm10, co2, tvoc, temp/10.0, humi/10.0, ch2o, o3, no2);`

## Notes
- Use your PC IP in `API_URL`. Find it with `ipconfig`.
- The dashboard uses WebSocket for live updates and shows last 60 PM2.5 values.
- For multi-device, include unique `device_id`.
