from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse, FileResponse
from pydantic import BaseModel, Field
from typing import List, Dict, Any
from collections import defaultdict
import time
import asyncio

app = FastAPI(title="Air Quality Monitoring API")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

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

# In-memory store grouped by device_id
MAX_ITEMS_PER_DEVICE = 500
device_measurements: Dict[str, List[Measurement]] = defaultdict(list)
device_last_seen: Dict[str, float] = {}

# WebSocket connections
class ConnectionManager:
    def __init__(self):
        self.active: List[WebSocket] = []
    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active.append(websocket)
    def disconnect(self, websocket: WebSocket):
        if websocket in self.active:
            self.active.remove(websocket)
    async def broadcast(self, message: Dict[str, Any]):
        to_remove = []
        for ws in self.active:
            try:
                await ws.send_json(message)
            except WebSocketDisconnect:
                to_remove.append(ws)
        for ws in to_remove:
            self.disconnect(ws)

manager = ConnectionManager()

@app.get("/")
async def root():
    return {"status": "ok", "message": "Air Quality Monitoring API"}

@app.post("/api/measurements")
async def post_measurement(m: Measurement):
    device_id = m.device_id
    device_measurements[device_id].append(m)
    device_last_seen[device_id] = m.ts
    
    # Trim old measurements
    if len(device_measurements[device_id]) > MAX_ITEMS_PER_DEVICE:
        device_measurements[device_id] = device_measurements[device_id][-MAX_ITEMS_PER_DEVICE:]
    
    await manager.broadcast({"type": "measurement", "data": m.model_dump()})
    return {"status": "ok"}

@app.get("/api/devices")
async def list_devices():
    """List all devices with their last measurement and status"""
    devices = []
    now = time.time()
    for device_id, measurements in device_measurements.items():
        if measurements:
            latest = measurements[-1]
            last_seen = device_last_seen.get(device_id, latest.ts)
            devices.append({
                "device_id": device_id,
                "last_seen": last_seen,
                "status": "online" if (now - last_seen) < 60 else "offline",
                "latest_measurement": latest.model_dump()
            })
    return devices

@app.get("/api/devices/{device_id}/measurements/latest")
async def get_device_latest(device_id: str):
    """Get latest measurement for a specific device"""
    if device_id in device_measurements and device_measurements[device_id]:
        return device_measurements[device_id][-1]
    return {"status": "empty"}

@app.get("/api/devices/{device_id}/measurements")
async def list_device_measurements(device_id: str, limit: int = 100):
    """Get measurements for a specific device"""
    if device_id in device_measurements:
        return device_measurements[device_id][-limit:]
    return []

@app.get("/api/measurements/latest")
async def get_latest():
    """Get latest measurement across all devices"""
    all_measurements = []
    for measurements in device_measurements.values():
        if measurements:
            all_measurements.extend(measurements)
    if all_measurements:
        all_measurements.sort(key=lambda m: m.ts)
        return all_measurements[-1]
    return {"status": "empty"}

@app.get("/api/measurements")
async def list_measurements():
    """Get all measurements across all devices"""
    all_measurements = []
    for measurements in device_measurements.values():
        all_measurements.extend(measurements)
    all_measurements.sort(key=lambda m: m.ts)
    return all_measurements[-500:]

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        # Send all recent measurements on connect
        all_recent = []
        for measurements in device_measurements.values():
            if measurements:
                all_recent.extend([m.model_dump() for m in measurements[-50:]])
        if all_recent:
            await websocket.send_json({"type": "bootstrap", "data": all_recent})
        
        while True:
            # Keep alive
            await asyncio.sleep(30)
            try:
                await websocket.send_json({"type": "ping", "ts": time.time()})
            except Exception:
                break
    except WebSocketDisconnect:
        pass
    finally:
        manager.disconnect(websocket)

# Static frontend serving (optional)
from fastapi.staticfiles import StaticFiles
app.mount("/static", StaticFiles(directory="../frontend"), name="static")

@app.get("/app", response_class=HTMLResponse)
async def serve_app():
    return FileResponse("../frontend/index.html")
