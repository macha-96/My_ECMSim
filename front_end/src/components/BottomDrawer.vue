<script setup lang="ts">
import { onMounted, ref } from 'vue'

const props = defineProps<{ tabs: { key: string; label: string }[]; modelValue: string }>()
const emit = defineEmits<{ 'update:modelValue': [key: string] }>()

const MIN_H = 140
const DEFAULT_H = 260

const drawerH = ref(DEFAULT_H)

function clamp(h: number): number {
  const max = Math.round(window.innerHeight * 0.6)
  return Math.min(Math.max(h, MIN_H), max)
}

// 首次渲染前同步恢复高度：onMounted 内的改动要等异步刷新，首帧会停留在默认值
const savedH = Number(localStorage.getItem('ecmsim.drawerH'))
if (savedH) drawerH.value = clamp(savedH)

let dragging = false
let startY = 0
let startH = DEFAULT_H

onMounted(() => {
  const tab = localStorage.getItem('ecmsim.drawerTab')
  if (tab && props.tabs.some(t => t.key === tab)) emit('update:modelValue', tab)
})

function onDown(ev: PointerEvent): void {
  dragging = true
  startY = ev.clientY
  startH = drawerH.value
  // jsdom 未实现 Pointer Capture，可选链兜底
  ;(ev.target as HTMLElement).setPointerCapture?.(ev.pointerId)
}

function onMove(ev: PointerEvent): void {
  if (!dragging) return
  // 把手向上拖（clientY 减小）→ 高度增大
  drawerH.value = clamp(startH + (startY - ev.clientY))
}

function onUp(ev: PointerEvent): void {
  if (!dragging) return
  dragging = false
  ;(ev.target as HTMLElement).releasePointerCapture?.(ev.pointerId)
  localStorage.setItem('ecmsim.drawerH', String(drawerH.value))
}

function select(key: string): void {
  emit('update:modelValue', key)
  localStorage.setItem('ecmsim.drawerTab', key)
}
</script>

<template>
  <div class="drawer glass-panel" :style="{ height: drawerH + 'px' }">
    <div class="handle" @pointerdown="onDown" @pointermove="onMove" @pointerup="onUp" />
    <div class="tabbar">
      <button
        v-for="t in tabs"
        :key="t.key"
        class="tab"
        :class="{ active: modelValue === t.key }"
        @click="select(t.key)"
      >{{ t.label }}</button>
    </div>
    <div class="body">
      <slot :name="modelValue" />
    </div>
  </div>
</template>

<style scoped>
.drawer {
  margin: 0 8px 8px;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  flex-shrink: 0;
}
.handle {
  height: 10px;
  cursor: ns-resize;
  background:
    radial-gradient(circle, rgba(255,255,255,.4) 1.5px, transparent 2px) center / 12px 6px no-repeat;
  touch-action: none;
}
.tabbar { display: flex; gap: 6px; padding: 0 10px 6px; }
.tab {
  background: transparent;
  border: 1px solid var(--glass-border);
  color: var(--text-2);
  font-size: 12px;
  border-radius: 999px;
  padding: 3px 14px;
  cursor: pointer;
}
.tab.active { color: var(--text-1); border-color: var(--accent); background: rgba(233,69,96,.15); }
.body { flex: 1; min-height: 0; overflow: auto; }
</style>
