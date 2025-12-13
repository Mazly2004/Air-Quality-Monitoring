# Air Quality Monitoring System - Architecture Documentation

## 🏗️ System Overview

This is a full-stack IoT air quality monitoring system with real-time data visualization. The system consists of three main components that work together to collect, process, and display environmental sensor data.

```
┌─────────────────┐         ┌─────────────────┐         ┌─────────────────┐
│   ESP32 Device  │         │  Backend (API)  │         │  Frontend (Web) │
│   (Hardware)    │  WiFi   │   FastAPI       │WebSocket│   SvelteKit     │
│                 │ ──────> │   Python        │ ──────> │   + Tailwind    │
│  ZPHS01B Sensor │  HTTP   │   Port 8000     │         │   Port 5173     │
│  + LCD Display  │  POST   │                 │         │                 │
└─────────────────┘         └─────────────────┘         └─────────────────┘
```

---

## 📡 Component 1: ESP32 Device (Hardware)

### Location
Physical ESP32 DevKit board with attached sensors

### Technology Stack
- **Platform:** ESP32 (240MHz dual-core, WiFi enabled)
- **Language:** C++ (Arduino framework)
- **Build Tool:** PlatformIO
- **Sensors:** ZPHS01B Multi-in-One Sensor Module
- **Display:** 20x4 I2C LCD (LiquidCrystal_I2C)

### Hardware Connections
- **Serial2 (UART):** RX=GPIO16, TX=GPIO17 → ZPHS01B sensor
- **I2C:** SDA/SCL → LCD Display (0x27)
- **WiFi:** Built-in ESP32 radio

### Code Location
```
src/main.cpp
```

### Responsibilities
1. **Sensor Communication:**
   - Sends request command every 2 seconds to ZPHS01B
   - Receives 25-byte response packet (FF 86 + 22 data bytes + checksum)
   - Parses sensor data: PM1.0, PM2.5, PM10, CO2, TVOC, Temperature, Humidity, HCHO, CO, O3, NO2

2. **Local Display:**
   - Shows real-time readings on 20x4 LCD
   - 4 lines displaying: PM values, air quality, temperature/humidity, formaldehyde

3. **Network Communication:**
   - Connects to WiFi on startup
   - Sends HTTP POST to backend API every 2 seconds
   - Continues in offline mode if WiFi fails

### Data Format (HTTP POST)
```json
POST http://<backend-ip>:8000/api/measurements
Content-Type: application/json

{
  "pm25": 4.0,
  "pm10": 4.0,
  "co2": 500,
  "tvoc": 2,
  "temp": 22.5,
  "humidity": 55.3,
  "hcho": 0.05
}
```

### Configuration
Located at top of `src/main.cpp`:
```cpp
const char* WIFI_SSID = "YourNetwork";
const char* WIFI_PASSWORD = "YourPassword";
const char* API_URL = "http://192.168.1.100:8000/api/measurements";
```

---

## 🔧 Component 2: Backend API (Server)

### Location
Runs on development PC/server

### Technology Stack
- **Framework:** FastAPI (Python)
- **Server:** Uvicorn (ASGI)
- **Validation:** Pydantic
- **Communication:** HTTP REST + WebSocket
- **Port:** 8000

### Code Location
```
webapp/backend/
├── main.py           # FastAPI application
├── requirements.txt  # Python dependencies
└── venv/            # Virtual environment
```

### API Endpoints

#### 1. POST `/api/measurements`
**Purpose:** Receive sensor data from ESP32

**Request Body:**
```json
{
  "pm25": float,
  "pm10": float,
  "co2": float,
  "tvoc": float,
  "temp": float,
  "humidity": float,
  "hcho": float
}
```

**Response:**
```json
{
  "status": "ok"
}
```

**Side Effect:** Broadcasts new measurement to all WebSocket clients

#### 2. GET `/api/measurements/latest`
**Purpose:** Fetch most recent measurement

**Response:**
```json
{
  "pm25": 35.2,
  "pm10": 42.8,
  "co2": 850,
  "tvoc": 120,
  "temp": 22.5,
  "humidity": 55.3,
  "hcho": 0.05,
  "device_id": "esp32-1",
  "ts": 1733484623.456
}
```

#### 3. GET `/api/measurements`
**Purpose:** List all stored measurements (last 1000)

**Response:** Array of measurement objects

#### 4. WebSocket `/ws`
**Purpose:** Real-time push notifications to frontend

**Message Format:**
```json
{
  "type": "measurement",
  "data": {
    "pm25": 35.2,
    "pm10": 42.8,
    ...
  }
}
```

### Data Model (Pydantic)
```python
class Measurement(BaseModel):
    pm25: float = Field(..., ge=0)
    pm10: float = Field(..., ge=0)
    co2: float = Field(..., ge=0)
    tvoc: float = Field(..., ge=0)
    temp: float
    humidity: float
    hcho: float = 0
    o3: int = 0
    no2: int = 0
    device_id: str = "esp32-1"
    ts: float = Field(default_factory=lambda: time.time())
```

### Storage
- **Type:** In-memory list (not persistent)
- **Capacity:** Last 1000 measurements
- **Note:** Data is lost on server restart

### CORS Configuration
```python
allow_origins=["*"]  # Allows frontend on any origin
```

### Run Command
```bash
cd webapp/backend
python -m venv venv
.\venv\Scripts\Activate.ps1
pip install -r requirements.txt
uvicorn main:app --host 0.0.0.0 --port 8000 --reload
```

---

## 🎨 Component 3: Frontend Dashboard (Web UI)

### Location
Runs on development PC/browser

### Technology Stack
- **Framework:** SvelteKit
- **Build Tool:** Vite
- **Styling:** Tailwind CSS 3.x
- **Charts:** Chart.js 4.x
- **Language:** TypeScript + Svelte
- **Port:** 5173 (dev), 4173 (preview)

### Code Location
```
webapp/frontend-svelte/
├── src/
│   ├── routes/
│   │   ├── +layout.svelte   # Global layout & header
│   │   └── +page.svelte     # Dashboard page
│   ├── app.css              # Tailwind directives
│   └── app.html             # HTML template
├── package.json
├── tailwind.config.cjs
├── vite.config.ts
└── svelte.config.js
```

### UI Components

#### 1. Header
- Title: "Air Quality Dashboard"
- Connection status indicator (Connecting/Connected/Disconnected)

#### 2. Metric Cards (6 cards in responsive grid)
- **PM2.5:** Particulate Matter 2.5μm (µg/m³)
- **PM10:** Particulate Matter 10μm (µg/m³)
- **CO₂:** Carbon Dioxide (ppm)
- **TVOC:** Total Volatile Organic Compounds (ppb)
- **Temp:** Temperature (°C)
- **Humidity:** Relative Humidity (%)

#### 3. PM2.5 Chart
- **Type:** Line chart (Chart.js)
- **Data:** Last 50 PM2.5 readings
- **X-axis:** Time (HH:MM:SS)
- **Y-axis:** PM2.5 value (µg/m³)
- **Updates:** Real-time via WebSocket

### Data Flow

#### Initial Load
1. Component mounts (`onMount`)
2. Fetch latest measurement: `GET /api/measurements/latest`
3. Populate cards with initial values
4. Initialize Chart.js canvas
5. Connect to WebSocket: `ws://127.0.0.1:8000/ws`

#### Real-time Updates
1. WebSocket receives message:
   ```json
   {
     "type": "measurement",
     "data": { ... }
   }
   ```
2. Update `latest` reactive variable
3. Svelte auto-updates card values
4. Push new data point to chart
5. Shift chart data if > 50 points

### TypeScript Interface
```typescript
type Measurement = {
  timestamp: string;
  pm25: number;
  pm10: number;
  co2: number;
  tvoc: number;
  temp: number;
  humidity: number;
  hcho?: number;
};
```

### Styling
- **Theme:** Dark mode (custom Tailwind colors)
- **Background:** `#0b1020`
- **Cards:** `#111936` with border `#223`
- **Text:** Slate gray labels, white values
- **Responsive:** Grid auto-fits cards (min 220px)

### Run Command
```bash
cd webapp/frontend-svelte
npm install
npm run dev          # Dev server (http://localhost:5173)
npm run build        # Production build
npm run preview      # Preview production (port 4173)
```

---

## 🔄 Data Flow Diagram

### Complete Pipeline

```
┌─────────────────────────────────────────────────────────────────┐
│                     ESP32 Device (every 2s)                     │
│  1. Request sensor data from ZPHS01B                            │
│  2. Parse 25-byte response packet                               │
│  3. Display on LCD                                              │
│  4. HTTP POST to backend                                        │
└────────────────────┬────────────────────────────────────────────┘
                     │ WiFi (HTTP POST)
                     │ {"pm25":4.0,"pm10":4.0,...}
                     ▼
┌─────────────────────────────────────────────────────────────────┐
│                    Backend API (FastAPI)                        │
│  1. Receive POST at /api/measurements                           │
│  2. Validate with Pydantic                                      │
│  3. Store in memory (last 1000)                                 │
│  4. Broadcast via WebSocket to all clients                      │
└────────────────────┬────────────────────────────────────────────┘
                     │ WebSocket
                     │ {"type":"measurement","data":{...}}
                     ▼
┌─────────────────────────────────────────────────────────────────┐
│                   Frontend (SvelteKit)                          │
│  1. Receive WebSocket message                                   │
│  2. Update reactive state (latest)                              │
│  3. Svelte auto-renders new values in cards                     │
│  4. Push to Chart.js (last 50 points)                           │
└─────────────────────────────────────────────────────────────────┘
```

### Network Requirements
- **ESP32 → Backend:** Same WiFi network, backend at `<PC-IP>:8000`
- **Frontend → Backend:** Localhost OK (WebSocket: `ws://127.0.0.1:8000/ws`)
- **Firewall:** Port 8000 must be open for ESP32 HTTP access

---

## 📦 Dependencies

### ESP32 (PlatformIO)
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = 
    LiquidCrystal_I2C
    WiFi (built-in)
    HTTPClient (built-in)
```

### Backend (Python)
```txt
fastapi==0.115.2
uvicorn[standard]==0.30.6
pydantic==2.8.2
python-multipart==0.0.9
```

### Frontend (Node.js)
```json
{
  "dependencies": {
    "chart.js": "^4.4.1",
    "svelte": "^4.2.0"
  },
  "devDependencies": {
    "@sveltejs/kit": "^2.0.0",
    "@sveltejs/vite-plugin-svelte": "^3.0.0",
    "vite": "^5.0.0",
    "tailwindcss": "^3.4.15",
    "postcss": "^8.4.31",
    "autoprefixer": "^10.4.19"
  }
}
```

---

## 🚀 Deployment & Usage

### Quick Start

1. **Configure ESP32:**
   ```cpp
   // Edit src/main.cpp
   const char* WIFI_SSID = "YourNetwork";
   const char* WIFI_PASSWORD = "YourPassword";
   const char* API_URL = "http://192.168.1.100:8000/api/measurements";
   ```

2. **Upload to ESP32:**
   ```bash
   cd "Air Quality Monitoring"
   platformio run --target upload
   platformio device monitor
   ```

3. **Start Backend:**
   ```bash
   cd webapp/backend
   .\venv\Scripts\Activate.ps1
   uvicorn main:app --host 0.0.0.0 --port 8000 --reload
   ```

4. **Start Frontend:**
   ```bash
   cd webapp/frontend-svelte
   npm install
   npm run dev
   ```

5. **Open Dashboard:**
   - Browser: http://localhost:5173
   - Watch real-time updates every 2 seconds!

### Monitoring
- **ESP32 Serial:** `platformio device monitor` (115200 baud)
- **Backend Logs:** Terminal running uvicorn
- **Frontend DevTools:** Browser console (F12)

---

## 🛠️ Customization Guide for New Frontend

### Backend API Contract (must maintain)

#### Measurement Schema
```json
{
  "pm25": number,      // PM2.5 in µg/m³
  "pm10": number,      // PM10 in µg/m³
  "co2": number,       // CO2 in ppm
  "tvoc": number,      // TVOC in ppb
  "temp": number,      // Temperature in °C
  "humidity": number,  // Humidity in %
  "hcho": number,      // Formaldehyde in mg/m³
  "device_id": string, // Device identifier
  "ts": number         // Unix timestamp
}
```

#### REST Endpoints
- `GET /api/measurements/latest` → Single measurement object
- `GET /api/measurements` → Array of measurements (last 1000)
- `POST /api/measurements` → Submit new measurement (from ESP32)

#### WebSocket
- **URL:** `ws://<backend-host>:8000/ws`
- **Message Format:**
  ```json
  {
    "type": "measurement",
    "data": { <Measurement object> }
  }
  ```

### Recommended Features for New Frontend

1. **Essential:**
   - Real-time metric cards (6 main sensors)
   - WebSocket connection with auto-reconnect
   - Time-series chart (PM2.5 minimum)
   - Connection status indicator

2. **Nice-to-have:**
   - Air Quality Index (AQI) calculation & color coding
   - Multiple charts (CO2, TVOC, Temp/Humidity)
   - Historical data fetch on load
   - Export data (CSV/JSON)
   - Dark/light theme toggle
   - Mobile responsive design
   - Alerts/notifications for thresholds

3. **Advanced:**
   - Multiple device support
   - Data persistence (connect to database)
   - User authentication
   - Configurable refresh rate
   - Trend analysis & predictions
   - Geographic mapping (if multiple sensors)

### Frontend Technology Options

#### Recommended Stacks
1. **React + TypeScript + Material-UI**
2. **Vue 3 + Vuetify**
3. **Angular + Angular Material**
4. **Next.js (React with SSR)**
5. **Nuxt.js (Vue with SSR)**

#### Chart Libraries
- Chart.js (current)
- Recharts (React-friendly)
- ApexCharts
- D3.js (advanced)
- Plotly

#### State Management
- React: Zustand, Redux Toolkit
- Vue: Pinia, Vuex
- SvelteKit: Svelte stores (current)

---

## 🔒 Security Considerations

### Current Implementation (Development)
- ⚠️ No authentication
- ⚠️ CORS allows all origins
- ⚠️ Data not encrypted in transit (HTTP not HTTPS)
- ⚠️ WiFi credentials in plain text

### Production Recommendations
- Add API key authentication
- Enable HTTPS (TLS certificates)
- Restrict CORS to specific origins
- Store WiFi credentials securely (EEPROM encrypted)
- Rate limiting on API endpoints
- Input validation on all endpoints

---

## 📊 Current System Status

### Working Features ✅
- ESP32 WiFi connection
- Sensor data reading (ZPHS01B)
- LCD display
- HTTP POST to backend
- Backend REST API
- Backend WebSocket streaming
- Frontend real-time updates
- Chart visualization

### Known Issues ⚠️
1. **Temperature/Humidity values incorrect** (-1612.8°C, 1638.4%)
   - Cause: Sensor packet parsing offset error
   - Impact: Display only; PM2.5, PM10, CO2, TVOC are correct
   - Fix: Adjust byte offsets in `parseZPHS01B()`

2. **Checksum warnings**
   - Cause: Sensor packet format validation
   - Impact: Warning only; data still processes
   - Fix: Verify checksum calculation range

3. **Data not persistent**
   - Storage is in-memory only
   - Lost on backend restart
   - Solution: Add database (PostgreSQL, MongoDB, SQLite)

---

## 📞 Integration Points for GPT-5

### What GPT-5 Needs to Know

1. **Backend is fixed and ready** - don't change the API
2. **WebSocket URL:** `ws://127.0.0.1:8000/ws` (or backend host)
3. **Initial data fetch:** `GET http://127.0.0.1:8000/api/measurements/latest`
4. **Message format is strict** - use TypeScript interface above
5. **Frontend should auto-reconnect WebSocket** on disconnect
6. **Chart should limit to ~50 points** for performance
7. **Dark theme preferred** to match current design

### Files to Replace
```
webapp/frontend-svelte/   ← Entire directory can be replaced
```

### Files to Keep
```
src/main.cpp              ← ESP32 code (don't change)
webapp/backend/           ← Backend API (don't change)
platformio.ini            ← ESP32 config (don't change)
```

---

## 📖 Additional Documentation

- **ESP32 Setup:** `ESP32_SETUP.md`
- **Backend README:** `webapp/backend/README.md` (if exists)
- **Frontend README:** `webapp/frontend-svelte/README.md`

---

**Last Updated:** December 6, 2025  
**System Version:** 1.0  
**Author:** Built with GitHub Copilot
