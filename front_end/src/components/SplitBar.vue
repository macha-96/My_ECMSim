<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue'

const props = withDefaults(defineProps<{
  direction: 'horizontal' | 'vertical'
  fixedPane?: 'a' | 'b'
  minA: number
  minB: number
  maxFixed?: number
  storageKey: string
  defaultValue?: number
  defaultUnit?: 'px' | 'ratio'
}>(), {
  fixedPane: 'a',
  defaultValue: 300,
  defaultUnit: 'px',
})

const HANDLE = 8

const rootEl = ref<HTMLDivElement | null>(null)
const fixedSize = ref(props.defaultValue)
const initialized = ref(false)
let dragging = false
let startPos = 0
let startSize = 0
let ro: ResizeObserver | null = null

function containerSize(): number {
  const el = rootEl.value
  if (!el) return 0
  return props.direction === 'horizontal' ? el.clientWidth : el.clientHeight
}

function clampSize(v: number): number {
  const total = containerSize()
  const minFixed = props.fixedPane === 'a' ? props.minA : props.minB
  const minOther = props.fixedPane === 'a' ? props.minB : props.minA
  let hi = total - minOther - HANDLE
  if (props.maxFixed !== undefined) hi = Math.min(hi, props.maxFixed)
  if (hi < minFixed) hi = minFixed // 容器过窄时保底 fixed 最小值
  return Math.min(Math.max(v, minFixed), hi)
}

/** 首次度量时确定尺寸：记忆 > 默认（ratio 需容器尺寸换算；持久化单位恒为 px） */
function ensureInit(): void {
  if (initialized.value || !rootEl.value) return
  initialized.value = true
  const saved = Number(localStorage.getItem(props.storageKey))
  if (saved > 0) {
    fixedSize.value = clampSize(saved)
  } else if (props.defaultUnit === 'ratio') {
    fixedSize.value = clampSize(props.defaultValue * containerSize())
  } else {
    fixedSize.value = clampSize(props.defaultValue)
  }
}

function onDown(ev: PointerEvent): void {
  ensureInit()
  dragging = true
  startPos = props.direction === 'horizontal' ? ev.clientX : ev.clientY
  startSize = fixedSize.value
  ;(ev.target as HTMLElement).setPointerCapture?.(ev.pointerId)
}

function onMove(ev: PointerEvent): void {
  if (!dragging) return
  const pos = props.direction === 'horizontal' ? ev.clientX : ev.clientY
  const delta = pos - startPos
  const sign = props.fixedPane === 'a' ? 1 : -1
  fixedSize.value = clampSize(startSize + sign * delta)
}

function endDrag(ev: PointerEvent): void {
  if (!dragging) return
  dragging = false
  ;(ev.target as HTMLElement).releasePointerCapture?.(ev.pointerId)
  localStorage.setItem(props.storageKey, String(Math.round(fixedSize.value)))
}

function onResize(): void {
  ensureInit()
  fixedSize.value = clampSize(fixedSize.value)
}

const styleA = computed(() => (props.fixedPane === 'a'
  ? { flex: `0 0 ${fixedSize.value}px` }
  : { flex: '1 1 0%' }))
const styleB = computed(() => (props.fixedPane === 'b'
  ? { flex: `0 0 ${fixedSize.value}px` }
  : { flex: '1 1 0%' }))

onMounted(() => {
  ro = new ResizeObserver(onResize)
  if (rootEl.value) ro.observe(rootEl.value)
  ensureInit()
})

onBeforeUnmount(() => {
  ro?.disconnect()
})
</script>

<template>
  <div ref="rootEl" class="split" :class="direction">
    <div class="pane pane-a" :style="styleA"><slot name="a" /></div>
    <div
      class="handle"
      @pointerdown="onDown"
      @pointermove="onMove"
      @pointerup="endDrag"
      @pointercancel="endDrag"
    />
    <div class="pane pane-b" :style="styleB"><slot name="b" /></div>
  </div>
</template>

<style scoped>
.split { display: flex; width: 100%; height: 100%; min-width: 0; min-height: 0; }
.split.horizontal { flex-direction: row; }
.split.vertical { flex-direction: column; }
.pane { min-width: 0; min-height: 0; overflow: hidden; display: flex; flex-direction: column; }
.handle { flex: 0 0 8px; touch-action: none; }
.split.horizontal > .handle { cursor: col-resize; width: 8px; background: linear-gradient(90deg, transparent 3px, rgba(255,255,255,.18) 3px, rgba(255,255,255,.18) 5px, transparent 5px); }
.split.vertical > .handle { cursor: row-resize; height: 8px; background: linear-gradient(180deg, transparent 3px, rgba(255,255,255,.18) 3px, rgba(255,255,255,.18) 5px, transparent 5px); }
.handle:hover { background-color: rgba(233,69,96,.25); }
</style>
