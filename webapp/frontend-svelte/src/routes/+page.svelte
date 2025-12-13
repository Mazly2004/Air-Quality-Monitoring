<script lang="ts">
  import { onMount } from 'svelte';
  import Chart from 'chart.js/auto';
  import MetricCard from '$lib/components/MetricCard.svelte';
  import { latest } from '$lib/stores/measurements';
  import { connectWebSocket } from '$lib/utils/websocket';

  let pm25ChartEl: HTMLCanvasElement;
  let chart: Chart | null = null;
  let current: any = null;
  let devices: any[] = [];
  const backendHttp = 'http://127.0.0.1:8000';

  // Threshold helpers
  function pm25Status(v?: number) {
    if (v == null) return 'unknown';
    if (v <= 12) return 'good';
    if (v <= 35.4) return 'moderate';
    return 'unhealthy';
  }

  function co2Status(v?: number) {
    if (v == null) return 'unknown';
    if (v <= 800) return 'good';
    if (v <= 1200) return 'moderate';
    return 'unhealthy';
  }

  function tvocStatus(v?: number) {
    if (v == null) return 'unknown';
    if (v <= 200) return 'good';
    if (v <= 600) return 'moderate';
    return 'unhealthy';
  }

  latest.subscribe((v) => {
    current = v;
    if (v) pushPoint(v);
  });

  async function bootstrap() {
    try {
      const res = await fetch(`${backendHttp}/api/measurements/latest`);
      if (res.ok) {
        current = await res.json();
      }
    } catch {}
  }

  async function fetchDevices() {
    try {
      const res = await fetch(`${backendHttp}/api/devices`);
      if (res.ok) {
        devices = await res.json();
      }
    } catch {}
  }

  function initChart() {
    chart = new Chart(pm25ChartEl, {
      type: 'line',
      data: { labels: [], datasets: [{ label: 'PM2.5 (µg/m³)', data: [], borderColor: '#58a6ff', backgroundColor: 'rgba(88,166,255,0.15)', tension: 0.2, pointRadius: 0 }] },
      options: { responsive: true, scales: { x: { ticks: { color: '#8b949e' }, grid: { color: '#30363d' } }, y: { ticks: { color: '#8b949e' }, grid: { color: '#30363d' } } }, plugins: { legend: { labels: { color: '#c9d1d9' } } } }
    });
  }

  function pushPoint(m: any) {
    if (!chart) return;
    const label = new Date((m.ts || Date.now()) * 1000).toLocaleTimeString();
    chart.data.labels!.push(label);
    (chart.data.datasets[0].data as number[]).push(m.pm25);
    if (chart.data.labels!.length > 50) {
      chart.data.labels!.shift();
      (chart.data.datasets[0].data as number[]).shift();
    }
    chart.update();
  }

  onMount(async () => {
    await bootstrap();
    await fetchDevices();
    initChart();
    connectWebSocket();
    
    // Refresh device list periodically
    setInterval(fetchDevices, 10000);
  });
</script>

<div class="space-y-6">
  <div>
    <h1 class="text-2xl font-semibold text-[#c9d1d9] mb-1">Dashboard Overview</h1>
    <p class="text-sm text-[#8b949e]">Latest readings from all monitoring stations</p>
  </div>

  <!-- Connected Devices Summary -->
  {#if devices.length > 0}
    <div class="bg-[#0d1117] border border-[#30363d] rounded-lg p-4">
      <h2 class="text-lg font-semibold text-[#c9d1d9] mb-3">Connected Devices ({devices.length})</h2>
      <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 xl:grid-cols-4 gap-3">
        {#each devices as device}
          <a 
            href="/devices/{device.device_id}" 
            class="flex items-center justify-between bg-[#161b22] rounded-lg p-3 hover:bg-[#21262d] transition-colors"
          >
            <div class="flex-1 min-w-0">
              <div class="font-medium text-[#c9d1d9] text-sm truncate">{device.device_id}</div>
              <div class="text-xs text-[#8b949e]">PM2.5: {device.latest_measurement.pm25.toFixed(1)}</div>
            </div>
            <span 
              class="ml-2 w-2 h-2 rounded-full flex-shrink-0"
              class:bg-green-500={device.status === 'online'}
              class:bg-gray-500={device.status === 'offline'}
            ></span>
          </a>
        {/each}
      </div>
      <div class="mt-3">
        <a href="/devices" class="text-sm text-[#58a6ff] hover:underline">View all devices →</a>
      </div>
    </div>
  {/if}

  <!-- Latest Metrics -->
  <div>
    <h2 class="text-lg font-semibold text-[#c9d1d9] mb-3">Latest Measurement</h2>
    <div class="grid grid-cols-[repeat(auto-fit,minmax(220px,1fr))] gap-4">
      <MetricCard label="PM2.5" value={current?.pm25 ?? '--'} unit="µg/m³" status={pm25Status(current?.pm25)} />
      <MetricCard label="PM10" value={current?.pm10 ?? '--'} unit="µg/m³" status={pm25Status(current?.pm10)} />
      <MetricCard label="CO₂" value={current?.co2 ?? '--'} unit="ppm" status={co2Status(current?.co2)} />
      <MetricCard label="TVOC" value={current?.tvoc ?? '--'} unit="ppb" status={tvocStatus(current?.tvoc)} />
      <MetricCard label="Temp" value={current?.temp ?? '--'} unit="°C" status="unknown" />
      <MetricCard label="Humidity" value={current?.humidity ?? '--'} unit="%" status="unknown" />
    </div>
  </div>

  <!-- PM2.5 Trend -->
  <div class="bg-[#21262d] border border-[#30363d] rounded-xl p-4">
    <h2 class="text-lg font-semibold text-[#c9d1d9] mb-3">PM2.5 Trend (All Devices)</h2>
    <canvas bind:this={pm25ChartEl} height="120"></canvas>
  </div>
</div>
