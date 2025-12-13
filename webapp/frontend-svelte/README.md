# Air Quality Dashboard (SvelteKit + Tailwind)

A modern, responsive dashboard for live air quality readings streamed from your FastAPI backend.

## Prerequisites
- Node.js 18+
- Backend running at `http://127.0.0.1:8000` (with WebSocket at `ws://127.0.0.1:8000/ws`)

## Run (Windows PowerShell)
```powershell
# From the Svelte app folder
cd "C:\Users\USER\OneDrive\Documents\PlatformIO\Projects\Air Quality Monitoring\webapp\frontend-svelte"

# Install dependencies (includes Tailwind)
npm install

# Start dev server (http://127.0.0.1:5173)
npm run dev
```

Open http://127.0.0.1:5173 and you should see live metrics and a PM2.5 chart. If your backend runs on a different host/port, edit the URLs in `src/routes/+page.svelte` (backendHttp and backendWs).

## Build & Preview
```powershell
npm run build
npm run preview
```

## Notes
- The page pulls initial state from `/api/measurements/latest` and then streams updates over WebSocket.
- Chart shows the last ~50 points to keep the UI smooth.
