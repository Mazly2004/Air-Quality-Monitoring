import { latest, status, type Measurement } from '../stores/measurements';

const WS_URL = 'ws://127.0.0.1:8000/ws';
let ws: WebSocket | null = null;
let retries = 0;

function backoffDelay(n: number) {
  return Math.min(30000, 1000 * Math.pow(2, n));
}

export function connectWebSocket() {
  try {
    status.set('Connecting');
    ws = new WebSocket(WS_URL);

    ws.onopen = () => {
      retries = 0;
      status.set('Connected');
    };

    ws.onmessage = (ev) => {
      try {
        const msg = JSON.parse(ev.data);
        if (msg.type === 'measurement') {
          latest.set(msg.data as Measurement);
        }
      } catch (e) {
        // ignore
      }
    };

    ws.onclose = () => {
      status.set('Disconnected');
      retries++;
      setTimeout(connectWebSocket, backoffDelay(retries));
    };

    ws.onerror = () => {
      status.set('Error');
    };
  } catch (e) {
    status.set('Error');
  }
}

export function disconnectWebSocket() {
  if (ws) {
    ws.close();
    ws = null;
  }
}
