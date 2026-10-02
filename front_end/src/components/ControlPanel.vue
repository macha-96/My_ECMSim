<script setup lang="ts">
import { computed } from 'vue'
import { useSceneStore } from '../stores/scene'
import EntityCard from './EntityCard.vue'

const scene = useSceneStore()

const tabs = [
  { key: 'all', label: '全部' },
  { key: 'radar', label: '雷达' },
  { key: 'jammer', label: '干扰机' },
] as const

const visible = computed(() => {
  const radars = scene.radars.map(r => ({ kind: 'radar' as const, id: r.id }))
  const jammers = scene.jammers.map(j => ({ kind: 'jammer' as const, id: j.id }))
  const all = [...radars, ...jammers]
  if (scene.filter === 'radar') return radars
  if (scene.filter === 'jammer') return jammers
  return all
})
</script>

<template>
  <div class="control-panel">
    <div class="toolbar">
      <div class="tabs">
        <button
          v-for="t in tabs"
          :key="t.key"
          class="tab"
          :class="{ active: scene.filter === t.key }"
          @click="scene.filter = t.key"
        >{{ t.label }}</button>
      </div>
      <div class="actions">
        <button class="btn" @click="scene.addRadar()">+ 雷达</button>
        <button class="btn" @click="scene.addJammer()">+ 干扰机</button>
      </div>
    </div>
    <div v-if="visible.length === 0" class="empty">暂无实体</div>
    <EntityCard v-for="e in visible" :key="e.kind + e.id" :kind="e.kind" :id="e.id" />
  </div>
</template>

<style scoped>
.control-panel { display: flex; flex-direction: column; gap: 8px; overflow: auto; padding: 8px; flex: 1; min-height: 0; scrollbar-width: thin; scrollbar-color: rgba(255,255,255,.25) transparent; }
.control-panel::-webkit-scrollbar { width: 8px; }
.control-panel::-webkit-scrollbar-track { background: transparent; }
.control-panel::-webkit-scrollbar-thumb { background: rgba(255,255,255,.22); border-radius: 4px; }
.control-panel::-webkit-scrollbar-thumb:hover { background: rgba(255,255,255,.35); }
.toolbar { display: flex; justify-content: space-between; align-items: center; }
.tabs { display: flex; gap: 4px; }
.tab {
  background: transparent;
  border: 1px solid var(--glass-border);
  color: var(--text-2);
  border-radius: 999px;
  padding: 3px 12px;
  font-size: 12px;
  cursor: pointer;
}
.tab.active { color: var(--text-1); border-color: var(--accent); }
.actions { display: flex; gap: 6px; }
.btn {
  background: linear-gradient(90deg, var(--accent), var(--accent-2));
  border: none;
  color: #fff;
  font-size: 12px;
  padding: 5px 12px;
  border-radius: 6px;
  cursor: pointer;
}
.empty { color: var(--text-2); font-size: 13px; text-align: center; padding: 24px 0; }
</style>
