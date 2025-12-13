<script lang="ts">
  import { onMount } from 'svelte';
  import Chart from 'chart.js/auto';
  import { latest } from '$lib/stores/measurements';

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

  const chartConfig = {
    responsive: true,
    maintainAspectRatio: false,
    scales: {
      x: { 
        ticks: { color: '#8b949e', maxTicksLimit: 8 }, 
        grid: { color: '#30363d' } 
      },
      y: { 
        ticks: { color: '#8b949e' }, 
        grid: { color: '#30363d' } 
      }
    },
    plugins: {
      legend: { labels: { color: '#c9d1d9' } }
    }
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
          pointRadius: 2,
          pointHoverRadius: 4
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
    
    // Keep last 100 points
    if (chart.data.labels!.length > 100) {
      chart.data.labels!.shift();
      (chart.data.datasets[0].data as number[]).shift();
    }
    chart.update('none');
  }

  latest.subscribe((m) => {
    if (!m) return;
    const ts = m.ts || Date.now() / 1000;
    pushData(pm25Chart, m.pm25, ts);
    pushData(pm10Chart, m.pm10, ts);
    pushData(co2Chart, m.co2, ts);
    pushData(tvocChart, m.tvoc, ts);
    pushData(tempChart, m.temp, ts);
    pushData(humidityChart, m.humidity, ts);
  });

  onMount(() => {
    pm25Chart = createChart(pm25El, 'PM2.5 (µg/m³)', '#58a6ff');
    pm10Chart = createChart(pm10El, 'PM10 (µg/m³)', '#79c0ff');
    co2Chart = createChart(co2El, 'CO₂ (ppm)', '#a5d6ff');
    tvocChart = createChart(tvocEl, 'TVOC (ppb)', '#f778ba');
    tempChart = createChart(tempEl, 'Temperature (°C)', '#ffa657');
    humidityChart = createChart(humidityEl, 'Humidity (%)', '#7ee787');
  });
</script>

<div class="space-y-4">
  <div>
    <h1 class="text-2xl font-semibold text-[#c9d1d9] mb-1">Analytics</h1>
    <p class="text-sm text-[#8b949e]">Real-time air quality metrics</p>
  </div>

  <div class="grid grid-cols-1 lg:grid-cols-2 gap-4">
    <!-- PM2.5 -->
    <div class="bg-[#0d1117] border border-[#30363d] rounded-lg p-4">
      <h3 class="text-sm font-medium text-[#c9d1d9] mb-3">PM2.5 Particulate Matter</h3>
      <div class="h-64">
        <canvas bind:this={pm25El}></canvas>
      </div>
    </div>

    <!-- PM10 -->
    <div class="bg-[#0d1117] border border-[#30363d] rounded-lg p-4">
      <h3 class="text-sm font-medium text-[#c9d1d9] mb-3">PM10 Particulate Matter</h3>
      <div class="h-64">
        <canvas bind:this={pm10El}></canvas>
      </div>
    </div>

    <!-- CO2 -->
    <div class="bg-[#0d1117] border border-[#30363d] rounded-lg p-4">
      <h3 class="text-sm font-medium text-[#c9d1d9] mb-3">Carbon Dioxide</h3>
      <div class="h-64">
        <canvas bind:this={co2El}></canvas>
      </div>
    </div>

    <!-- TVOC -->
    <div class="bg-[#0d1117] border border-[#30363d] rounded-lg p-4">
      <h3 class="text-sm font-medium text-[#c9d1d9] mb-3">Total Volatile Organic Compounds</h3>
      <div class="h-64">
        <canvas bind:this={tvocEl}></canvas>
      </div>
    </div>

    <!-- Temperature -->
    <div class="bg-[#0d1117] border border-[#30363d] rounded-lg p-4">
      <h3 class="text-sm font-medium text-[#c9d1d9] mb-3">Temperature</h3>
      <div class="h-64">
        <canvas bind:this={tempEl}></canvas>
      </div>
    </div>

    <!-- Humidity -->
    <div class="bg-[#0d1117] border border-[#30363d] rounded-lg p-4">
      <h3 class="text-sm font-medium text-[#c9d1d9] mb-3">Humidity</h3>
      <div class="h-64">
        <canvas bind:this={humidityEl}></canvas>
      </div>
    </div>
  </div>
</div>

