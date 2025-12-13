import { writable } from 'svelte/store';

export type Measurement = {
  pm25: number;
  pm10: number;
  co2: number;
  tvoc: number;
  temp: number;
  humidity: number;
  hcho: number;
  device_id: string;
  ts: number;
};

export const latest = writable<Measurement | null>(null);
export const status = writable<'Connecting' | 'Connected' | 'Disconnected' | 'Error'>('Connecting');
