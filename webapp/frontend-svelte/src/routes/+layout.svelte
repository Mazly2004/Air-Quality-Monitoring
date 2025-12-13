<script lang="ts">
  import '../app.css';
  import Navbar from '$lib/components/Navbar.svelte';
  import Sidebar from '$lib/components/Sidebar.svelte';
</script>

<div class="min-h-screen bg-[#0d1117] text-[#c9d1d9]">
  <Navbar />
  <div class="flex">
    <Sidebar />
    <main class="flex-1 p-6">
      <slot />
    </main>
  </div>
</div>

