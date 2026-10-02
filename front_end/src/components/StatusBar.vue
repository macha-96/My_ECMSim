<script setup lang="ts">
import { useSceneStore } from '../stores/scene'
import { useSimulationStore } from '../stores/simulation'

const scene = useSceneStore()
const sim = useSimulationStore()
</script>

<template>
  <footer class="status-bar mono">
    <span>{{ sim.statusMsg }}</span>
    <span v-if="scene.dirtyCount > 0" class="warn">待同步 {{ scene.dirtyCount }}</span>
    <span v-else-if="scene.syncing">同步中…</span>
    <span class="dim">步骤 {{ sim.history.length }}</span>
    <span class="dim">雷达 {{ scene.radars.length }} · 干扰机 {{ scene.jammers.length }}</span>
  </footer>
</template>

<style scoped>
.status-bar {
  display: flex;
  gap: 18px;
  align-items: center;
  padding: 6px 16px;
  font-size: 12px;
  color: var(--text-2);
}
.warn { color: var(--accent); }
.dim { opacity: 0.7; }
</style>
