<script lang="ts">
  import { onMount } from 'svelte';
  
  interface Device {
    device_id: string;
    last_seen: number;
    status: 'online' | 'offline';
    latest_measurement: {
      pm25: number;
      pm10: number;
      co2: number;
      tvoc: number;
      temp: number;
      humidity: number;
    };
  }
  
  let devices: Device[] = [];
  let loading = true;
  const backendHttp = 'http://127.0.0.1:8000';
  
  async function fetchDevices() {
    try {
      const res = await fetch(`${backendHttp}/api/devices`);
      if (res.ok) {
        devices = await res.json();
      }
    } catch (err) {
      console.error('Failed to fetch devices:', err);
    } finally {
      loading = false;
    }
  }
  
  function formatLastSeen(ts: number): string {
    const seconds = Math.floor(Date.now() / 1000 - ts);
    if (seconds < 60) return `${seconds}s ago`;
    if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`;
    if (seconds < 86400) return `${Math.floor(seconds / 3600)}h ago`;
    return `${Math.floor(seconds / 86400)}d ago`;
  }
  
  onMount(() => {
    fetchDevices();
    // Refresh device list every 10 seconds
    const interval = setInterval(fetchDevices, 10000);
    return () => clearInterval(interval);
  });
</script>

<div class="space-y-4">
  <div>
    <h1 class="text-2xl font-semibold text-[#c9d1d9] mb-1">Connected Devices</h1>
    <p class="text-sm text-[#8b949e]">Monitor all air quality sensors across your school</p>
  </div>

  {#if loading}
    <div class="text-[#8b949e]">Loading devices...</div>
  {:else if devices.length === 0}
    <div class="bg-[#0d1117] border border-[#30363d] rounded-lg p-8 text-center">
      <div class="text-4xl mb-3">📡</div>
      <h3 class="text-lg font-medium text-[#c9d1d9] mb-2">No devices connected</h3>
      <p class="text-sm text-[#8b949e]">Waiting for ESP32 units to start sending data...</p>
    </div>
  {:else}
    <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
      {#each devices as device}
        <a 
          href="/devices/{device.device_id}" 
          class="block bg-[#0d1117] border border-[#30363d] rounded-lg p-5 hover:border-[#58a6ff] transition-colors"
        >
          <!-- Header -->
          <div class="flex items-start justify-between mb-4">
            <div>
              <h3 class="text-base font-semibold text-[#c9d1d9]">{device.device_id}</h3>
              <p class="text-xs text-[#8b949e] mt-1">{formatLastSeen(device.last_seen)}</p>
            </div>
            <span 
              class="px-2 py-1 rounded-full text-xs font-medium"
              class:bg-green-600={device.status === 'online'}
              class:text-white={device.status === 'online'}
              class:bg-gray-600={device.status === 'offline'}
              class:text-gray-300={device.status === 'offline'}
            >
              {device.status}
            </span>
          </div>

          <!-- Quick Stats Grid -->
          <div class="grid grid-cols-2 gap-3">
            <div class="bg-[#161b22] rounded p-2">
              <div class="text-xs text-[#8b949e]">PM2.5</div>
              <div class="text-sm font-medium text-[#c9d1d9]">{device.latest_measurement.pm25.toFixed(1)} µg/m³</div>
            </div>
            <div class="bg-[#161b22] rounded p-2">
              <div class="text-xs text-[#8b949e]">CO₂</div>
              <div class="text-sm font-medium text-[#c9d1d9]">{device.latest_measurement.co2.toFixed(0)} ppm</div>
            </div>
            <div class="bg-[#161b22] rounded p-2">
              <div class="text-xs text-[#8b949e]">Temp</div>
              <div class="text-sm font-medium text-[#c9d1d9]">{device.latest_measurement.temp.toFixed(1)}°C</div>
            </div>
            <div class="bg-[#161b22] rounded p-2">
              <div class="text-xs text-[#8b949e]">TVOC</div>
              <div class="text-sm font-medium text-[#c9d1d9]">{device.latest_measurement.tvoc.toFixed(0)} ppb</div>
            </div>
          </div>

          <!-- View Details Indicator -->
          <div class="mt-4 text-xs text-[#58a6ff] flex items-center gap-1">
            View details <span>→</span>
          </div>
        </a>
      {/each}
    </div>
  {/if}
</div>
