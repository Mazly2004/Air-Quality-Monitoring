<script lang="ts">
  export let label: string;
  export let value: string | number;
  export let unit: string = '';
  export let status: 'good' | 'moderate' | 'unhealthy' | 'unknown' = 'unknown';

  const colors: Record<string, string> = {
    good: 'text-emerald-400',
    moderate: 'text-amber-400',
    unhealthy: 'text-red-500',
    unknown: 'text-[#c9d1d9]'
  };
</script>

<div class="bg-[#21262d] border border-[#30363d] rounded-xl p-4 hover:border-[#58a6ff] transition">
  <div class="flex items-center justify-between">
    <div class="text-sm text-[#8b949e]">{label}</div>
    {#if status !== 'unknown'}
      <span class="px-2 py-0.5 text-xs rounded-full border border-[#30363d] text-[#8b949e]">
        {status}
      </span>
    {/if}
  </div>
  <div class="text-3xl font-bold mt-1 {colors[status]}">{value} <span class="text-base text-[#8b949e]">{unit}</span></div>
</div>
