<script lang="ts">
  import { status } from '$lib/stores/measurements';
  import { derived } from 'svelte/store';

  const pill = derived(status, ($s) => {
    let color = 'bg-[#21262d] text-[#8b949e]';
    if ($s === 'Connected') color = 'bg-[#1f6feb] text-white';
    if ($s === 'Disconnected') color = 'bg-[#21262d] text-[#8b949e]';
    if ($s === 'Connecting') color = 'bg-[#21262d] text-[#c9d1d9]';
    if ($s === 'Error') color = 'bg-red-600 text-white';
    return { text: $s, color };
  });
</script>

<span class="px-3 py-1 rounded-full text-sm {$pill.color}">
  {$pill.text}
</span>
