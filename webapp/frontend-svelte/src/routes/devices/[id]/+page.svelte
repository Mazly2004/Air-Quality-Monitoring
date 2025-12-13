<script lang="ts">
  import { page } from '$app/stores';
  import { onMount, onDestroy } from 'svelte';
  import Chart from 'chart.js/auto';
  import MetricCard from '$lib/components/MetricCard.svelte';
  
  const deviceId = $page.params.id;
  const backendHttp = 'http://127.0.0.1:8000';
  
  let current: any = null;
  let ws: WebSocket | null = null;
  let reconnectTimer: any = null;
  
  // Charts
  let pm25Chart: Chart | null = null;
  let pm10Chart: Chart | null = null;
  let co2Chart: Chart | null = null;
  let tvocChart: Chart | null = null;
  let tempChart: Chart | null = null;
  let humidityChart: Chart | null = null;
  
  let pm25El: HTMLCanvasElement;
  let pm10El: HTMLCanvasElement;
  let co2El: HTMLCanvasElement;
  let tvocEl: HTMLCanvasElement;
  let tempEl: HTMLCanvasElement;
  let humidityEl: HTMLCanvasElement;
  
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
  
  const chartConfig = {
    responsive: true,
    maintainAspectRatio: false,
    scales: {
      x: { ticks: { color: '#8b949e', maxTicksLimit: 8 }, grid: { color: '#30363d' } },
      y: { ticks: { color: '#8b949e' }, grid: { color: '#30363d' } }
    },
    plugins: { legend: { labels: { color: '#c9d1d9' } } }
  };
  
  function createChart(canvas: HTMLCanvasElement, label: string, color: string) {
    return new Chart(canvas, {
      type: 'line',
      data: {
        labels: [],
        datasets: [{
          label,
          data: [],
          borderColor: color,
          backgroundColor: color.replace('rgb', 'rgba').replace(')', ', 0.15)'),
          tension: 0.3,
          fill: true,
          pointRadius: 2
        }]
      },
      options: chartConfig as any
    });
  }
  
  function pushData(chart: Chart | null, value: number, timestamp: number) {
    if (!chart) return;
    const label = new Date(timestamp * 1000).toLocaleTimeString();
    chart.data.labels!.push(label);
    (chart.data.datasets[0].data as number[]).push(value);
    if (chart.data.labels!.length > 100) {
      chart.data.labels!.shift();
      (chart.data.datasets[0].data as number[]).shift();
    }
    chart.update('none');
  }
  
  async function fetchLatest() {
    try {
      const res = await fetch(`${backendHttp}/api/devices/${deviceId}/measurements/latest`);
      if (res.ok) {
        current = await res.json();
      }
    } catch (err) {
      console.error('Failed to fetch latest:', err);
    }
  }
  
  async function fetchHistory() {
    try {
      const res = await fetch(`${backendHttp}/api/devices/${deviceId}/measurements?limit=100`);
      if (res.ok) {
        const measurements = await res.json();
        measurements.forEach((m: any) => {
          const ts = m.ts || Date.now() / 1000;
          pushData(pm25Chart, m.pm25, ts);
          pushData(pm10Chart, m.pm10, ts);
          pushData(co2Chart, m.co2, ts);
          pushData(tvocChart, m.tvoc, ts);
          pushData(tempChart, m.temp, ts);
          pushData(humidityChart, m.humidity, ts);
        });
      }
    } catch (err) {
      console.error('Failed to fetch history:', err);
    }
  }
  
  function connectWebSocket() {
    ws = new WebSocket('ws://127.0.0.1:8000/ws');
    
    ws.onopen = () => {
      console.log('WebSocket connected');
    };
    
    ws.onmessage = (event) => {
      const msg = JSON.parse(event.data);
      if (msg.type === 'measurement' && msg.data.device_id === deviceId) {
        current = msg.data;
        const ts = msg.data.ts || Date.now() / 1000;
        pushData(pm25Chart, msg.data.pm25, ts);
        pushData(pm10Chart, msg.data.pm10, ts);
        pushData(co2Chart, msg.data.co2, ts);
        pushData(tvocChart, msg.data.tvoc, ts);
        pushData(tempChart, msg.data.temp, ts);
        pushData(humidityChart, msg.data.humidity, ts);
      }
    };
    
    ws.onclose = () => {
      console.log('WebSocket disconnected, reconnecting...');
      reconnectTimer = setTimeout(connectWebSocket, 3000);
    };
  }
  
  onMount(async () => {
    await fetchLatest();
    
    pm25Chart = createChart(pm25El, 'PM2.5 (µg/m³)', '#58a6ff');
    pm10Chart = createChart(pm10El, 'PM10 (µg/m³)', '#79c0ff');
    co2Chart = createChart(co2El, 'CO₂ (ppm)', '#a5d6ff');
    tvocChart = createChart(tvocEl, 'TVOC (ppb)', '#f778ba');
    tempChart = createChart(tempEl, 'Temperature (°C)', '#ffa657');
    humidityChart = createChart(humidityEl, 'Humidity (%)', '#7ee787');
    
    await fetchHistory();
    connectWebSocket();
  });
  
  onDestroy(() => {
    if (ws) ws.close();
    if (reconnectTimer) clearTimeout(reconnectTimer);
  });
</script>

<div class="space-y-4">
  <!-- Header -->
  <div class="flex items-center gap-3">
    <a href="/devices" class="text-[#58a6ff] hover:underline">← Back to devices</a>
  </div>
  
  <div>
    <h1 class="text-2xl font-semibold text-[#c9d1d9] mb-1">{deviceId}</h1>
    <p class="text-sm text-[#8b949e]">Real-time monitoring and analytics</p>
  </div>

  <!-- Metric Cards -->
  <div class="grid grid-cols-[repeat(auto-fit,minmax(220px,1fr))] gap-4">
    <MetricCard label="PM2.5" value={current?.pm25 ?? '--'} unit="µg/m³" status={pm25Status(current?.pm25)} />
    <MetricCard label="PM10" value={current?.pm10 ?? '--'} unit="µg/m³" status={pm25Status(current?.pm10)} />
    <MetricCard label="CO₂" value={current?.co2 ?? '--'} unit="ppm" status={co2Status(current?.co2)} />
    <MetricCard label="TVOC" value={current?.tvoc ?? '--'} unit="ppb" status={tvocStatus(current?.tvoc)} />
    <MetricCard label="Temp" value={current?.temp ?? '--'} unit="°C" status="unknown" />
    <MetricCard label="Humidity" value={current?.humidity ?? '--'} unit="%" status="unknown" />
  </div>

  <!-- Charts Grid -->
  <div class="grid grid-cols-1 lg:grid-cols-2 gap-4">
    <div class="bg-[#0d1117] border border-[#30363d] rounded-lg p-4">
      <h3 class="text-sm font-medium text-[#c9d1d9] mb-3">PM2.5 Particulate Matter</h3>
      <div class="h-64"><canvas bind:this={pm25El}></canvas></div>
    </div>
    
    <div class="bg-[#0d1117] border border-[#30363d] rounded-lg p-4">
      <h3 class="text-sm font-medium text-[#c9d1d9] mb-3">PM10 Particulate Matter</h3>
      <div class="h-64"><canvas bind:this={pm10El}></canvas></div>
    </div>
    
    <div class="bg-[#0d1117] border border-[#30363d] rounded-lg p-4">
      <h3 class="text-sm font-medium text-[#c9d1d9] mb-3">Carbon Dioxide</h3>
      <div class="h-64"><canvas bind:this={co2El}></canvas></div>
    </div>
    
    <div class="bg-[#0d1117] border border-[#30363d] rounded-lg p-4">
      <h3 class="text-sm font-medium text-[#c9d1d9] mb-3">Total Volatile Organic Compounds</h3>
      <div class="h-64"><canvas bind:this={tvocEl}></canvas></div>
    </div>
    
    <div class="bg-[#0d1117] border border-[#30363d] rounded-lg p-4">
      <h3 class="text-sm font-medium text-[#c9d1d9] mb-3">Temperature</h3>
      <div class="h-64"><canvas bind:this={tempEl}></canvas></div>
    </div>
    
    <div class="bg-[#0d1117] border border-[#30363d] rounded-lg p-4">
      <h3 class="text-sm font-medium text-[#c9d1d9] mb-3">Humidity</h3>
      <div class="h-64"><canvas bind:this={humidityEl}></canvas></div>
    </div>
  </div>
</div>
