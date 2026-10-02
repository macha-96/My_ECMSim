<script setup lang="ts">
import { computed, nextTick, ref } from 'vue'
import {
  KNOB_MIN_ANGLE, KNOB_SWEEP, angleToValue, clampNum,
  pointerToAngle, polar, stepDecimals, valueToAngle,
} from '../composables/knobMath'

const props = defineProps<{
  modelValue: number
  min: number
  max: number
  step: number
  unit?: string
}>()
const emit = defineEmits<{ 'update:modelValue': [v: number] }>()

const knobEl = ref<HTMLDivElement | null>(null)
const inputEl = ref<HTMLInputElement | null>(null)
const editing = ref(false)
const draft = ref('')
let dragging = false

const angle = computed(() => valueToAngle(props.modelValue, props.min, props.max))

function arcPath(fromDeg: number, toDeg: number): string {
  const s = polar(fromDeg)
  const e = polar(toDeg)
  const large = toDeg - fromDeg > 180 ? 1 : 0
  return `M ${s.x.toFixed(2)} ${s.y.toFixed(2)} A 26 26 0 ${large} 1 ${e.x.toFixed(2)} ${e.y.toFixed(2)}`
}

const track = computed(() => arcPath(KNOB_MIN_ANGLE, KNOB_MIN_ANGLE + KNOB_SWEEP))
const valueArc = computed(() => arcPath(KNOB_MIN_ANGLE, angle.value))
const needle = computed(() => polar(angle.value))

const displayText = computed(() => {
  const dec = stepDecimals(props.step)
  return String(Number(props.modelValue.toFixed(dec)))
})

function inInteractiveZone(target: EventTarget | null): boolean {
  return !!(target as HTMLElement | null)?.closest?.('.readout, .edit')
}

function setFromEvent(ev: PointerEvent): void {
  const box = knobEl.value?.getBoundingClientRect()
  if (!box) return
  const a = pointerToAngle(ev.clientX, ev.clientY, box.left + box.width / 2, box.top + box.height / 2)
  emit('update:modelValue', angleToValue(a, props.min, props.max, props.step))
}

function onDown(ev: PointerEvent): void {
  if (inInteractiveZone(ev.target) || editing.value) return
  dragging = true
  setFromEvent(ev)
  ;(ev.target as HTMLElement).setPointerCapture?.(ev.pointerId)
}

function onMove(ev: PointerEvent): void {
  if (!dragging) return
  setFromEvent(ev)
}

function onUp(ev: PointerEvent): void {
  if (!dragging) return
  dragging = false
  ;(ev.target as HTMLElement).releasePointerCapture?.(ev.pointerId)
}

function nudge(dir: 1 | -1): void {
  const dec = stepDecimals(props.step)
  const raw = props.modelValue + dir * props.step
  emit('update:modelValue', clampNum(Number(raw.toFixed(dec)), props.min, props.max))
}

function onWheel(ev: WheelEvent): void {
  nudge(ev.deltaY < 0 ? 1 : -1)
}

function onKey(ev: KeyboardEvent): void {
  if (ev.key === 'ArrowUp' || ev.key === 'ArrowRight') nudge(1)
  else if (ev.key === 'ArrowDown' || ev.key === 'ArrowLeft') nudge(-1)
  else if (ev.key === 'Home') emit('update:modelValue', props.min)
  else if (ev.key === 'End') emit('update:modelValue', props.max)
  else return
  ev.preventDefault()
}

async function startEdit(): Promise<void> {
  draft.value = displayText.value
  editing.value = true
  await nextTick()
  inputEl.value?.focus()
  inputEl.value?.select()
}

function commit(): void {
  if (!editing.value) return
  editing.value = false
  const v = parseFloat(draft.value)
  if (Number.isNaN(v)) return
  emit('update:modelValue', clampNum(v, props.min, props.max))
}
</script>

<template>
  <div
    ref="knobEl"
    class="knob"
    tabindex="0"
    @pointerdown="onDown"
    @pointermove="onMove"
    @pointerup="onUp"
    @pointercancel="onUp"
    @wheel.prevent="onWheel"
    @keydown="onKey"
  >
    <svg viewBox="0 0 64 64" class="dial">
      <defs>
        <linearGradient id="knob-grad" x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stop-color="#e94560" />
          <stop offset="1" stop-color="#ff7a59" />
        </linearGradient>
      </defs>
      <path :d="track" class="track" />
      <path :d="valueArc" class="value" />
      <line x1="32" y1="32" :x2="needle.x" :y2="needle.y" class="needle" />
      <circle cx="32" cy="32" r="3.5" class="hub" />
    </svg>
    <div v-if="!editing" class="readout mono" title="点击键入精确值" @click.stop="startEdit">
      {{ displayText }}<span v-if="unit" class="unit"> {{ unit }}</span>
    </div>
    <input
      v-else
      ref="inputEl"
      v-model="draft"
      class="edit mono"
      @keyup.enter="commit"
      @keyup.escape="editing = false"
      @blur="commit"
    />
  </div>
</template>

<style scoped>
.knob { width: 76px; touch-action: none; outline: none; display: flex; flex-direction: column; align-items: center; gap: 2px; }
.knob:focus-visible { filter: drop-shadow(0 0 3px var(--cyan)); }
.dial { width: 64px; height: 64px; }
.track { fill: none; stroke: rgba(255,255,255,.15); stroke-width: 5; stroke-linecap: round; }
.value { fill: none; stroke: url(#knob-grad); stroke-width: 5; stroke-linecap: round; }
.needle { stroke: var(--text-1); stroke-width: 2.5; stroke-linecap: round; }
.hub { fill: var(--text-1); }
.readout { font-size: 11px; cursor: text; color: var(--text-1); white-space: nowrap; }
.readout .unit { color: var(--text-2); }
.edit { width: 64px; font-size: 11px; text-align: center; background: rgba(0,0,0,.35); border: 1px solid var(--cyan); border-radius: 4px; color: var(--text-1); padding: 1px 2px; }
</style>
