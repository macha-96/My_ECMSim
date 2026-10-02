<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useSceneStore } from '../stores/scene'
import { useSimulationStore } from '../stores/simulation'
import { computeViewport, hitTest, makeTransform, type Viewport } from '../composables/useSceneCanvas'
import type { EntityKind } from '../types/api'

const props = defineProps<{ height: number }>()

const scene = useSceneStore()
const sim = useSimulationStore()
const canvasRef = ref<HTMLCanvasElement | null>(null)
const hover = ref<{ kind: EntityKind; id: number; x: number; y: number; sx: number; sy: number } | null>(null)

let viewport: Viewport = { x: -1500, y: -1500, w: 30000, h: 30000 }
// 画布尺寸以 clientWidth/clientHeight 为准（draw 内更新），props.height 仅作初始/回退值
let canvasW = 800
let canvasH = props.height
let rafId = 0
let dragging: { kind: EntityKind; id: number } | null = null
let panning: { startX: number; startY: number; vpX: number; vpY: number } | null = null

function scheduleDraw(): void {
  if (rafId) return
  rafId = requestAnimationFrame(() => {
    rafId = 0
    draw()
  })
}

function draw(): void {
  const canvas = canvasRef.value
  if (!canvas) return
  const ctx = canvas.getContext('2d')
  if (!ctx) return
  canvasW = canvas.clientWidth || canvas.width
  canvasH = canvas.clientHeight || props.height
  viewport = computeViewport(canvasW, canvasH)
  const t = makeTransform(viewport, canvasW, canvasH)

  ctx.clearRect(0, 0, canvasW, canvasH)

  // 网格（每 5km）
  ctx.strokeStyle = 'rgba(255,255,255,0.06)'
  ctx.lineWidth = 1
  const step = 5000 * t.scale
  const origin = t.toScreen(0, 0)
  for (let x = origin.sx % step; x < canvasW; x += step) {
    ctx.beginPath()
    ctx.moveTo(x, 0)
    ctx.lineTo(x, canvasH)
    ctx.stroke()
  }
  for (let y = origin.sy % step; y < canvasH; y += step) {
    ctx.beginPath()
    ctx.moveTo(0, y)
    ctx.lineTo(canvasW, y)
    ctx.stroke()
  }
  // 坐标轴
  ctx.strokeStyle = 'rgba(0,229,255,0.35)'
  ctx.beginPath()
  ctx.moveTo(0, origin.sy)
  ctx.lineTo(canvasW, origin.sy)
  ctx.moveTo(origin.sx, 0)
  ctx.lineTo(origin.sx, canvasH)
  ctx.stroke()

  ctx.font = '11px JetBrains Mono, monospace'
  ctx.textBaseline = 'middle'

  const resById = new Map((sim.results ?? []).map(r => [r.radar_id, r]))

  for (const r of scene.radars) {
    const { sx, sy } = t.toScreen(r.x, r.y)
    const res = resById.get(r.id)
    const color = res ? (res.detect_success ? getVar('--ok') : getVar('--danger')) : getVar('--radar-color')
    ctx.strokeStyle = color
    ctx.fillStyle = color
    ctx.beginPath()
    ctx.arc(sx, sy, 6, 0, Math.PI * 2)
    ctx.fill()
    // 未检出：虚线红圈警示
    if (res && !res.detect_success) {
      ctx.setLineDash([4, 3])
      ctx.strokeStyle = getVar('--danger')
      ctx.beginPath()
      ctx.arc(sx, sy, 11, 0, Math.PI * 2)
      ctx.stroke()
      ctx.setLineDash([])
    }
    ctx.fillText(`R${r.id}`, sx + 9, sy)
  }

  for (const j of scene.jammers) {
    const { sx, sy } = t.toScreen(j.x, j.y)
    ctx.fillStyle = getVar('--accent')
    ctx.beginPath()
    ctx.arc(sx, sy, 6, 0, Math.PI * 2)
    ctx.fill()
    ctx.fillText(`J${j.id}`, sx + 9, sy)
  }
}

function getVar(name: string): string {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || '#fff'
}

function localPos(ev: PointerEvent): { x: number; y: number } {
  const rect = canvasRef.value?.getBoundingClientRect()
  return { x: ev.clientX - (rect?.left ?? 0), y: ev.clientY - (rect?.top ?? 0) }
}

function hitPoints() {
  const t = makeTransform(viewport, canvasW, canvasH)
  return [
    ...scene.radars.map(r => ({ kind: 'radar' as const, id: r.id, ...t.toScreen(r.x, r.y) })),
    ...scene.jammers.map(j => ({ kind: 'jammer' as const, id: j.id, ...t.toScreen(j.x, j.y) })),
  ]
}

function onPointerDown(ev: PointerEvent): void {
  const { x, y } = localPos(ev)
  const hit = hitTest(x, y, hitPoints())
  const canvas = canvasRef.value
  // jsdom 未实现 setPointerCapture — 可选调用兜底
  canvas?.setPointerCapture?.(ev.pointerId)
  if (hit) {
    dragging = hit
    if (canvas) canvas.style.cursor = 'grabbing'
  } else {
    panning = { startX: x, startY: y, vpX: viewport.x, vpY: viewport.y }
  }
}

function onPointerMove(ev: PointerEvent): void {
  const { x, y } = localPos(ev)
  if (dragging) {
    const t = makeTransform(viewport, canvasW, canvasH)
    const world = t.toWorld(x, y)
    scene.updateField(dragging.kind, dragging.id, 'x', Math.round(world.wx))
    scene.updateField(dragging.kind, dragging.id, 'y', Math.round(world.wy))
  } else if (panning) {
    const t = makeTransform(viewport, canvasW, canvasH)
    viewport = {
      ...viewport,
      x: panning.vpX - (x - panning.startX) / t.scale,
      y: panning.vpY + (y - panning.startY) / t.scale,
    }
  } else {
    const hit = hitTest(x, y, hitPoints())
    const canvas = canvasRef.value
    if (hit) {
      const ent = hit.kind === 'radar'
        ? scene.radars.find(r => r.id === hit.id)
        : scene.jammers.find(j => j.id === hit.id)
      if (ent) {
        const t = makeTransform(viewport, canvasW, canvasH)
        const s = t.toScreen(ent.x, ent.y)
        hover.value = { ...hit, x: ent.x, y: ent.y, sx: s.sx, sy: s.sy }
      }
      if (canvas) canvas.style.cursor = 'grab'
    } else {
      hover.value = null
      if (canvas) canvas.style.cursor = 'crosshair'
    }
  }
  scheduleDraw()
}

function onPointerUp(ev: PointerEvent): void {
  if (dragging) scene.pulseEntity(dragging.kind, dragging.id)
  dragging = null
  panning = null
  canvasRef.value?.releasePointerCapture?.(ev.pointerId)
  scheduleDraw()
}

function onPointerLeave(): void {
  hover.value = null
  scheduleDraw()
}

function onResize(): void {
  scheduleDraw()
}

watch(() => [scene.radars, scene.jammers, sim.results, props.height], scheduleDraw, { deep: true })

onMounted(() => {
  const canvas = canvasRef.value
  canvas?.addEventListener('pointerdown', onPointerDown)
  canvas?.addEventListener('pointermove', onPointerMove)
  canvas?.addEventListener('pointerup', onPointerUp)
  canvas?.addEventListener('pointerleave', onPointerLeave)
  window.addEventListener('resize', onResize)
  scheduleDraw()
})

onBeforeUnmount(() => {
  const canvas = canvasRef.value
  canvas?.removeEventListener('pointerdown', onPointerDown)
  canvas?.removeEventListener('pointermove', onPointerMove)
  canvas?.removeEventListener('pointerup', onPointerUp)
  canvas?.removeEventListener('pointerleave', onPointerLeave)
  window.removeEventListener('resize', onResize)
  if (rafId) cancelAnimationFrame(rafId)
})
</script>

<template>
  <div class="scene-canvas">
    <canvas ref="canvasRef" class="canvas" :height="height" />
    <div
      v-if="hover"
      class="tip glass-panel mono"
      :style="{ left: hover.sx + 14 + 'px', top: hover.sy - 10 + 'px' }"
    >
      #{{ hover.id }} {{ hover.kind === 'radar' ? '雷达' : '干扰机' }} · ({{ Math.round(hover.x) }}, {{ Math.round(hover.y) }}) m
    </div>
  </div>
</template>

<style scoped>
.scene-canvas {
  position: relative;
  width: 100%;
  overflow: hidden;
}
.canvas {
  display: block;
  width: 100%;
  cursor: crosshair;
  touch-action: none;
}
.tip {
  position: absolute;
  pointer-events: none;
  padding: 4px 8px;
  font-size: 11px;
  white-space: nowrap;
  color: var(--text-1);
}
</style>
