<script lang="ts">
  import ConnectionStatus from './ConnectionStatus.svelte';
</script>

<header class="sticky top-0 z-10 bg-[#0d1117] border-b border-[#30363d]">
  <div class="h-14 flex items-center justify-between px-4">
    <div class="flex items-center gap-2">
      <span class="text-[#c9d1d9] font-semibold">Air Quality Monitoring</span>
    </div>
    <div class="flex items-center gap-3">
      <ConnectionStatus />
      <button class="text-[#8b949e] hover:text-[#c9d1d9]" aria-label="Settings">
        ⋮
      </button>
    </div>
  </div>
</header>
