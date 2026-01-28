# ESP32 WiFi + Backend Integration Setup

## 🎯 What I Added

Your ESP32 now:
1. ✅ Connects to WiFi on startup
2. ✅ Sends sensor measurements to your backend via HTTP POST
3. ✅ Continues to display data on LCD
4. ✅ Works in offline mode if WiFi fails

## 📝 Configuration Steps

### Step 1: Update WiFi Credentials in `src/main.cpp`

Find these lines near the top of the file and replace with YOUR values:

```cpp
// WiFi Configuration - UPDATE THESE!
const char* WIFI_SSID = "YOUR_WIFI_SSID";        // ← Your WiFi network name
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD"; // ← Your WiFi password

// Backend API Configuration - UPDATE WITH YOUR PC's IP!
const char* API_URL = "http://192.168.1.100:8000/api/measurements"; // ← Your PC's IP
```

**Your PC's IP address is: `172.20.10.3`**

So change the API_URL line to:
```cpp
const char* API_URL = "http://172.20.10.3:8000/api/measurements";
```

### Step 2: Start Your Backend

In PowerShell:
```powershell
cd "C:\Users\USER\OneDrive\Documents\PlatformIO\Projects\Air Quality Monitoring\webapp\backend"
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
& .\venv\Scripts\Activate.ps1
uvicorn main:app --host 0.0.0.0 --port 8000 --reload
```

**Important:** Use `--host 0.0.0.0` so the ESP32 can reach it over the network!

### Step 3: Upload to ESP32

```powershell
cd "C:\Users\USER\OneDrive\Documents\PlatformIO\Projects\Air Quality Monitoring"
platformio run --target upload
platformio device monitor
```

### Step 4: Open Frontend Dashboard

Open your browser to: **http://localhost:5173/**

The frontend should already be running from the earlier `npm run dev` command.

## 🔄 Data Flow

```
ESP32 (Sensor) → WiFi → Backend (FastAPI) → WebSocket → Frontend (SvelteKit)
       ↓
    LCD Display
```

Every 2 seconds:
1. ESP32 requests data from ZPHS01B sensor
2. Parses and displays on LCD
3. **Sends JSON payload via HTTP POST to backend**
4. Backend broadcasts to frontend via WebSocket
5. Dashboard updates in real-time!

## 🐛 Troubleshooting

### WiFi won't connect
- Double-check SSID and password (case-sensitive!)
- Make sure ESP32 is within WiFi range
- Check serial monitor for connection status

### Backend not receiving data
- Verify PC IP: run `ipconfig` in PowerShell
- Make sure backend is running with `--host 0.0.0.0`
- Check firewall isn't blocking port 8000
- Verify ESP32 and PC are on the same network

### Frontend shows "Disconnected"
- Confirm backend is running on port 8000
- Check browser console (F12) for WebSocket errors
- Try refreshing the page

### LCD/Sensor still working?
- Yes! The code continues to work exactly as before
- WiFi is optional - if it fails, ESP32 continues in local mode

## 📊 Expected Output

### Serial Monitor:
```
=== ESP32 Air Quality Monitor (ZPHS01B) ===
...
=== Connecting to WiFi ===
SSID: YourNetwork
...............
✓ WiFi Connected!
IP Address: 192.168.1.123
Backend API: http://172.20.10.3:8000/api/measurements
...
========== ZPHS01B Sensor Data ==========
PM2.5:         35 μg/m³
PM10:          42 μg/m³
CO2:           850 ppm
...
[HTTP] Sending to backend...
{"pm25":35.0,"pm10":42.0,"co2":850,...}
[HTTP] Response code: 200
[HTTP] ✓ Data sent successfully!
```

### Dashboard:
Real-time cards updating every 2 seconds with smooth animations and chart updates!

## 🎨 Optional: Add More Features

Want me to add:
- Retry logic if HTTP POST fails
- WiFi reconnection if connection drops
- Status LED indicators
- Data buffering for offline periods
- Multiple sensor support

Let me know!
