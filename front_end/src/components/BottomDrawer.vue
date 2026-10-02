<script setup lang="ts">
import { ref } from 'vue'
import SplitBar from './SplitBar.vue'

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
</script>

<template>
  <div class="drawer glass-panel" :style="{ height: drawerH + 'px' }">
    <div class="handle" @pointerdown="onDown" @pointermove="onMove" @pointerup="onUp" />
    <div class="body">
      <SplitBar
        direction="horizontal"
        fixed-pane="a"
        :min-a="280"
        :min-b="280"
        storage-key="ecmsim.resultSplit"
        :default-value="0.5"
        default-unit="ratio"
      >
        <template #a><slot name="left" /></template>
        <template #b><slot name="right" /></template>
      </SplitBar>
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
.body { flex: 1; min-height: 0; overflow: hidden; }
</style>
