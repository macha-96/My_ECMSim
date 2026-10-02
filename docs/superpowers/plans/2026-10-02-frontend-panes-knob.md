# 四窗格可拖拽布局 + 旋钮 + 坐标负值 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 主区改为四窗格可拖拽分隔（画布↔参数、结果↔历史、上下抽屉），EntityCard 滑杆换为圆周旋转旋钮，坐标放开到 ±20000m。

**Architecture:** 新增通用 `SplitBar` 容器组件（三处复用：画布↔参数水平分、抽屉内结果↔历史水平分；抽屉高度沿用 BottomDrawer 现有逻辑即垂直分隔②）；新建 `KnobControl`（SVG 表盘 + 纯函数角度数学 `knobMath`）替换 EntityCard 全部数值滑杆，写入仍走 `updateField` 统一入口；BottomDrawer 去标签页改为左右双栏；App.vue 重装配。

**Tech Stack:** Vue 3.5 `<script setup>` + TS、现有 Pinia stores、Element Plus（仅 `jam_type` 下拉与 LEV 保留处）、Vitest + jsdom。

**Spec:** `docs/superpowers/specs/2026-10-02-frontend-panes-knob-design.md`（计划论据来源，执行者需同时阅读）

## Global Constraints

- 仅改 `front_end/`；C++（`src/`、`include/`）、`static/`、`protos/`、`agents/` 零改动；WS 同步/防抖语义不变
- `updateField(kind, id, prop, value)` 仍是所有参数写入的唯一入口；pending 键格式 `` `${prop}_${kind === 'jammer' ? 'j' : ''}${id}` `` 不变
- localStorage 键注册表（防碰撞）：`ecmsim.drawerH`（既有，抽屉高度 px，不变）、`ecmsim.sideW`（参数面板宽 px，新增）、`ecmsim.resultSplit`（结果窗格宽 px，新增）；`ecmsim.drawerTab` 退役（残留不迁移）
- SplitBar 持久化单位恒为 px（`defaultUnit: 'ratio'` 仅影响首次无记忆时的初始计算）；拖拽用 pointer 捕获 + `touch-action: none`；`setPointerCapture?.()` 可选链（jsdom 无此方法）
- UI 文案中文；数值读数用 `--font-mono`；玻璃拟态沿用 `--accent`/`--glass-*` 令牌
- tsconfig 严格：`noUnusedLocals`/`noUnusedParameters`/`erasableSyntaxOnly`；类型导入用 `import type`
- 每个任务通过自己的验证门（`vitest` 相关文件 → 全量 `vitest run` → `typecheck` → `build`）后 commit
- jsdom 无布局：凡依赖 `clientWidth/clientHeight/getBoundingClientRect/matchMedia` 的测试，必须像 HistoryCharts.spec 那样 stub（`vi.spyOn(HTMLElement.prototype, 'clientWidth', 'get')`、`getBoundingClientRect` mock、`matchMedia` 守卫）；pointer 事件用 `dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX, clientY }))`（VTU trigger 传 clientX 会抛，见 Task 15 经验）

## File Structure

```
front_end/src/
├── components/
│   ├── SplitBar.vue            # NEW 通用分隔容器（slots a/b + handle，px 尺寸 + 记忆）
│   ├── SplitBar.spec.ts        # NEW
│   ├── KnobControl.vue         # NEW 旋钮（SVG 表盘 + 读数键入 + 滚轮 + 键盘）
│   ├── KnobControl.spec.ts     # NEW（组件行为；纯数学另有单测）
│   ├── EntityCard.vue          # MOD 滑杆→KnobControl；x/y min → -20000；网格布局
│   ├── EntityCard.spec.ts      # MOD 用例同步（knob 交互 + 负值范围）
│   ├── BottomDrawer.vue        # MOD 去标签页→左右双栏（slots left/right + SplitBar③）
│   ├── BottomDrawer.spec.ts    # MOD 重写（双栏渲染 + 高度记忆保留）
│   └── ...（其余组件不动）
├── composables/
│   ├── knobMath.ts             # NEW 纯函数：角度↔数值↔SVG 坐标
│   └── knobMath.spec.ts        # NEW
├── App.vue                     # MOD SplitBar①装配 + 抽屉双栏装配 + 移除 drawerTab
└── ...（stores/api 不动）
```

---

### Task 1: SplitBar 通用分隔容器

**Files:**
- Create: `front_end/src/components/SplitBar.vue`
- Create: `front_end/src/components/SplitBar.spec.ts`

**Interfaces:**
- Consumes: 无（自包含；localStorage 直读写）
- Produces（Task 4 依赖，签名固定）:
  - props: `{ direction: 'horizontal' | 'vertical'; fixedPane?: 'a' | 'b'（默认 'a'）; minA: number; minB: number; maxFixed?: number; storageKey: string; defaultValue?: number（默认 300）; defaultUnit?: 'px' | 'ratio'（默认 'px'） }`
  - slots: `a`、`b`；fixed 窗格得 `flex: 0 0 Npx`，另一窗格 `flex: 1`
  - 语义：horizontal=左右分栏（dx 拖动；fixed==='a' 时 size+=dx，否则 -=dx）；vertical=上下分栏（同理用 dy）；松手/取消时 `localStorage.setItem(storageKey, round(px))`；挂载 + ResizeObserver 时重钳制；`pointercancel` 视同松手

- [ ] **Step 1: 写失败测试 `src/components/SplitBar.spec.ts`**

```ts
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import SplitBar from './SplitBar.vue'

function down(el: Element, x: number, y: number): void {
  el.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: x, clientY: y }))
}
function move(el: Element, x: number, y: number): void {
  el.dispatchEvent(new MouseEvent('pointermove', { bubbles: true, clientX: x, clientY: y }))
}
function up(el: Element, x: number, y: number): void {
  el.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientX: x, clientY: y }))
}

describe('SplitBar horizontal, fixed a', () => {
  beforeEach(() => {
    localStorage.clear()
    vi.spyOn(HTMLElement.prototype, 'clientWidth', 'get').mockReturnValue(1000)
    vi.spyOn(HTMLElement.prototype, 'clientHeight', 'get').mockReturnValue(600)
    vi.stubGlobal('ResizeObserver', class {
      observe() {}
      unobserve() {}
      disconnect() {}
    })
  })

  function mountBar(extra: Record<string, unknown> = {}) {
    return mount(SplitBar, {
      props: { direction: 'horizontal', minA: 100, minB: 100, storageKey: 'ecmsim.testSplit', defaultValue: 300, ...extra },
      slots: { a: '<div class="pa">A</div>', b: '<div class="pb">B</div>' },
      attachTo: document.body,
    })
  }

  it('首绘 fixed 窗格为默认 px', () => {
    const w = mountBar()
    expect((w.find('.pane-a').element as HTMLElement).style.flex).toContain('300px')
    w.unmount()
  })

  it('拖拽改变尺寸并持久化', () => {
    const w = mountBar()
    const handle = w.find('.handle')
    down(handle.element, 500, 0)
    move(handle.element, 600, 0)
    expect((w.find('.pane-a').element as HTMLElement).style.flex).toContain('400px')
    up(handle.element, 600, 0)
    expect(localStorage.getItem('ecmsim.testSplit')).toBe('400')
    w.unmount()
  })

  it('钳制到最小值', () => {
    const w = mountBar()
    const handle = w.find('.handle')
    down(handle.element, 500, 0)
    move(handle.element, -5000, 0)
    expect((w.find('.pane-a').element as HTMLElement).style.flex).toContain('100px')
    w.unmount()
  })

  it('恢复记忆值', () => {
    localStorage.setItem('ecmsim.testSplit', '450')
    const w = mountBar()
    expect((w.find('.pane-a').element as HTMLElement).style.flex).toContain('450px')
    w.unmount()
  })

  it('fixed b 时反向增减', () => {
    const w = mountBar({ fixedPane: 'b' })
    const handle = w.find('.handle')
    down(handle.element, 500, 0)
    move(handle.element, 600, 0) // 右拖 → b 缩小
    expect((w.find('.pane-b').element as HTMLElement).style.flex).toContain('200px')
    w.unmount()
  })
})

describe('SplitBar vertical', () => {
  beforeEach(() => {
    localStorage.clear()
    vi.spyOn(HTMLElement.prototype, 'clientWidth', 'get').mockReturnValue(1000)
    vi.spyOn(HTMLElement.prototype, 'clientHeight', 'get').mockReturnValue(600)
    vi.stubGlobal('ResizeObserver', class {
      observe() {}
      unobserve() {}
      disconnect() {}
    })
  })

  it('上下拖拽改变高度', () => {
    const w = mount(SplitBar, {
      props: { direction: 'vertical', minA: 50, minB: 50, storageKey: 'ecmsim.testV', defaultValue: 200 },
      slots: { a: '<div>A</div>', b: '<div>B</div>' },
      attachTo: document.body,
    })
    const handle = w.find('.handle')
    handle.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 0, clientY: 300 }))
    handle.element.dispatchEvent(new MouseEvent('pointermove', { bubbles: true, clientX: 0, clientY: 200 }))
    expect((w.find('.pane-a').element as HTMLElement).style.flex).toContain('100px')
    w.unmount()
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/SplitBar.spec.ts`
Expected: FAIL — `Failed to resolve import "./SplitBar.vue"`

- [ ] **Step 3: 实现 `src/components/SplitBar.vue`**

```vue
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

/** 首次度量时确定尺寸：记忆 > 默认（ratio 需容器尺寸换算） */
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
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/SplitBar.spec.ts`
Expected: PASS（6 个用例）

- [ ] **Step 5: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/components/SplitBar.vue front_end/src/components/SplitBar.spec.ts
git commit -m "front_end: 通用可拖拽分隔容器 SplitBar"
```

---

### Task 2: 旋钮数学 + KnobControl 组件

**Files:**
- Create: `front_end/src/composables/knobMath.ts`
- Create: `front_end/src/composables/knobMath.spec.ts`
- Create: `front_end/src/components/KnobControl.vue`
- Create: `front_end/src/components/KnobControl.spec.ts`

**Interfaces:**
- Consumes: 无
- Produces（Task 3 EntityCard 依赖，签名固定）:
  - `knobMath.ts`: `KNOB_MIN_ANGLE = -135`、`KNOB_SWEEP = 270`；`clampNum(v, lo, hi)`；`stepDecimals(step)`；`valueToAngle(v, min, max): number`（0=顶部、顺时针为正）；`angleToValue(a, min, max, step): number`（钳制角度域后按 step 取整）；`pointerToAngle(x, y, cx, cy): number`（`atan2(dx, -dy)` 转度后钳制）；`polar(angleDeg, r=26, c=32): {x, y}`（viewBox 64 表盘坐标）
  - `KnobControl.vue` props: `{ modelValue: number（显示值）; min: number; max: number; step: number; unit?: string }`；emit `update:modelValue`（显示值；父组件负责 toStorage 换算）
  - 行为：pointer 按下+移动按圆心角度旋转（读数/输入框区域的 pointerdown 不触发旋转）；滚轮 ±step；键盘 上/右 +step、下/左 -step、Home→min、End→max；点击读数进入键入模式（Enter/失焦提交，NaN 回退，钳制 min/max；Escape 取消）

- [ ] **Step 1: 写失败测试 `src/composables/knobMath.spec.ts`**

```ts
import { describe, expect, it } from 'vitest'
import {
  KNOB_MIN_ANGLE, KNOB_SWEEP, angleToValue, clampNum,
  pointerToAngle, polar, stepDecimals, valueToAngle,
} from './knobMath'

describe('knobMath', () => {
  it('常量：-135° 起，270° 行程', () => {
    expect(KNOB_MIN_ANGLE).toBe(-135)
    expect(KNOB_SWEEP).toBe(270)
  })

  it('valueToAngle: min→-135, max→+135, 中点→0', () => {
    expect(valueToAngle(8, 8, 18)).toBe(-135)
    expect(valueToAngle(18, 8, 18)).toBe(135)
    expect(valueToAngle(13, 8, 18)).toBeCloseTo(0)
    expect(valueToAngle(10, 8, 18)).toBeCloseTo(-81)
  })

  it('valueToAngle 越界钳制', () => {
    expect(valueToAngle(-999, 8, 18)).toBe(-135)
    expect(valueToAngle(999, 8, 18)).toBe(135)
  })

  it('angleToValue: ±135 还原 min/max，中点 0→中值', () => {
    expect(angleToValue(-135, 8, 18, 0.001)).toBe(8)
    expect(angleToValue(135, 8, 18, 0.001)).toBe(18)
    expect(angleToValue(0, 0, 100, 10)).toBe(50)
  })

  it('angleToValue 按 step 取整', () => {
    // 90° → t=(90+135)/270=0.8333 → 83.33 → step=1 取 83
    expect(angleToValue(90, 0, 100, 1)).toBe(83)
    expect(angleToValue(90, 0, 100, 10)).toBe(80)
  })

  it('pointerToAngle: 上=0, 右=90, 钳制到底部死区', () => {
    expect(pointerToAngle(32, 22, 32, 32)).toBeCloseTo(0)
    expect(pointerToAngle(42, 32, 32, 32)).toBeCloseTo(90)
    expect(pointerToAngle(32, 42, 32, 32)).toBe(135) // 正下方死区钳制到 +135
  })

  it('polar: -135° 在左下，0° 在顶部', () => {
    const p = polar(-135)
    expect(p.x).toBeCloseTo(13.6, 1)
    expect(p.y).toBeCloseTo(50.4, 1)
    const top = polar(0)
    expect(top.x).toBeCloseTo(32)
    expect(top.y).toBeCloseTo(6)
  })

  it('clampNum/stepDecimals', () => {
    expect(clampNum(5, 0, 10)).toBe(5)
    expect(clampNum(-1, 0, 10)).toBe(0)
    expect(stepDecimals(1)).toBe(0)
    expect(stepDecimals(0.001)).toBe(3)
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/composables/knobMath.spec.ts`
Expected: FAIL — `Failed to resolve import "./knobMath"`

- [ ] **Step 3: 实现 `src/composables/knobMath.ts`**

```ts
// 旋钮纯数学：表盘角度（度，0=顶部，顺时针为正）↔ 显示值 ↔ SVG 坐标
export const KNOB_MIN_ANGLE = -135
export const KNOB_SWEEP = 270

export function clampNum(v: number, lo: number, hi: number): number {
  return Math.min(Math.max(v, lo), hi)
}

export function stepDecimals(step: number): number {
  const s = String(step)
  const i = s.indexOf('.')
  return i < 0 ? 0 : s.length - i - 1
}

/** 显示值 → 表盘角度 */
export function valueToAngle(v: number, min: number, max: number): number {
  if (max <= min) return KNOB_MIN_ANGLE
  const t = clampNum(v, min, max)
  return KNOB_MIN_ANGLE + ((t - min) / (max - min)) * KNOB_SWEEP
}

/** 表盘角度 → 显示值（钳制角度域后按 step 取整） */
export function angleToValue(a: number, min: number, max: number, step: number): number {
  const hi = KNOB_MIN_ANGLE + KNOB_SWEEP
  const t = (clampNum(a, KNOB_MIN_ANGLE, hi) - KNOB_MIN_ANGLE) / KNOB_SWEEP
  const raw = min + t * (max - min)
  const snapped = Math.round(raw / step) * step
  return clampNum(Number(snapped.toFixed(stepDecimals(step))), min, max)
}

/** pointer 位置 → 表盘角度（相对圆心，钳制到底部死区两端） */
export function pointerToAngle(x: number, y: number, cx: number, cy: number): number {
  const deg = (Math.atan2(x - cx, -(y - cy)) * 180) / Math.PI
  return clampNum(deg, KNOB_MIN_ANGLE, KNOB_MIN_ANGLE + KNOB_SWEEP)
}

/** 表盘角度 → SVG 坐标（viewBox 64，中心 32，半径默认 26） */
export function polar(angleDeg: number, r = 26, c = 32): { x: number; y: number } {
  const rad = (angleDeg * Math.PI) / 180
  return { x: c + r * Math.sin(rad), y: c - r * Math.cos(rad) }
}
```

- [ ] **Step 4: 写失败测试 `src/components/KnobControl.spec.ts`**

```ts
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import KnobControl from './KnobControl.vue'

const RECT = { left: 0, top: 0, width: 64, height: 64, right: 64, bottom: 64, x: 0, y: 0, toJSON: () => ({}) }

function mountKnob(props: Record<string, unknown> = {}) {
  return mount(KnobControl, {
    props: { modelValue: 50, min: 0, max: 100, step: 1, unit: 'dBm', ...props },
    attachTo: document.body,
  })
}

function ptr(el: Element, type: string, x: number, y: number): void {
  el.dispatchEvent(new MouseEvent(type, { bubbles: true, clientX: x, clientY: y }))
}

describe('KnobControl', () => {
  beforeEach(() => {
    vi.restoreAllMocks()
  })

  it('指针指到正确角度（50 → 0°顶部）', () => {
    const w = mountKnob()
    const line = w.find('line.needle')
    expect(Number(line.attributes('x2'))).toBeCloseTo(32)
    expect(Number(line.attributes('y2'))).toBeCloseTo(6)
    w.unmount()
  })

  it('拖拽旋转发出显示值（90° → 83）', () => {
    const w = mountKnob()
    const knob = w.find('.knob').element
    vi.spyOn(knob, 'getBoundingClientRect').mockReturnValue(RECT as DOMRect)
    ptr(knob, 'pointerdown', 32 + 26, 32) // 正右方 = 90°
    ptr(knob, 'pointerup', 32 + 26, 32)
    expect(w.emitted('update:modelValue')?.[0]).toEqual([83])
    w.unmount()
  })

  it('点击读数不触发旋转，只进入键入', async () => {
    const w = mountKnob()
    const knob = w.find('.knob').element
    vi.spyOn(knob, 'getBoundingClientRect').mockReturnValue(RECT as DOMRect)
    await w.find('.readout').trigger('click')
    expect(w.emitted('update:modelValue')).toBeUndefined()
    expect(w.find('input.edit').exists()).toBe(true)
    w.unmount()
  })

  it('键入提交：合法值发出，越界钳制，非法回退', async () => {
    const w = mountKnob()
    await w.find('.readout').trigger('click')
    const input = w.find('input.edit')
    await input.setValue('75')
    await input.trigger('keyup.enter')
    expect(w.emitted('update:modelValue')?.[0]).toEqual([75])
    await w.find('.readout').trigger('click')
    const input2 = w.find('input.edit')
    await input2.setValue('9999')
    await input2.trigger('keyup.enter')
    expect(w.emitted('update:modelValue')?.[1]).toEqual([100])
    w.unmount()
  })

  it('滚轮按 step 微调', () => {
    const w = mountKnob()
    const knob = w.find('.knob').element
    knob.dispatchEvent(new WheelEvent('wheel', { bubbles: true, cancelable: true, deltaY: -100 }))
    expect(w.emitted('update:modelValue')?.[0]).toEqual([51])
    w.unmount()
  })

  it('键盘方向键调整，Home/End 到两端', async () => {
    const w = mountKnob()
    const knob = w.find('.knob')
    await knob.trigger('keydown', { key: 'ArrowUp' })
    expect(w.emitted('update:modelValue')?.[0]).toEqual([51])
    await knob.trigger('keydown', { key: 'End' })
    expect(w.emitted('update:modelValue')?.[1]).toEqual([100])
    w.unmount()
  })
})
```

- [ ] **Step 5: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/KnobControl.spec.ts`
Expected: FAIL — `Failed to resolve import "./KnobControl.vue"`

- [ ] **Step 6: 实现 `src/components/KnobControl.vue`**

```vue
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
```

**实现注意**：`id="knob-grad"` 在多实例下重复——SVG 同文档 id 引用解析到第一个同名 gradient，视觉一致故可接受（report 中声明即可）；若审查介意，加 `useId()` 后缀（Vue 3.5 有 `useId`），保持行为不变。

- [ ] **Step 7: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/composables/knobMath.spec.ts src/components/KnobControl.spec.ts`
Expected: PASS（8 + 6 个用例）

- [ ] **Step 8: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/composables/knobMath.ts front_end/src/composables/knobMath.spec.ts front_end/src/components/KnobControl.vue front_end/src/components/KnobControl.spec.ts
git commit -m "front_end: 旋钮数学与 KnobControl 组件"
```

---

### Task 3: EntityCard 换旋钮 + 坐标 ±20000

**Files:**
- Modify: `front_end/src/components/EntityCard.vue`（滑杆块→KnobControl；x/y min→-20000；字段网格布局）
- Modify: `front_end/src/components/EntityCard.spec.ts`（用例同步：knob 交互 + 负值范围）

**Interfaces:**
- Consumes: `KnobControl`（Task 2：`modelValue/min/max/step/unit` + `update:modelValue`，均为显示值空间）、`pendingKey/updateField/setFocusedField`（不变）
- Produces: EntityCard 行为不变（`updateField` 收存储值、`~` 徽标、焦点保护、一步删除确认、pulse 高亮均保留）；x/y 字段 `min: -20000`

- [ ] **Step 1: 改写测试 `src/components/EntityCard.spec.ts`（knob 交互版）**

原 4 用例保留语义，定位方式改为 knob（`.field[data-prop]` 属性已存在，沿用）：

```ts
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import EntityCard from './EntityCard.vue'
import KnobControl from './KnobControl.vue'
import { useSceneStore } from '../stores/scene'
import type { Radar } from '../types/api'

vi.mock('element-plus', () => ({
  ElMessage: { error: vi.fn(), success: vi.fn() },
  ElMessageBox: { confirm: vi.fn().mockResolvedValue(undefined) },
}))

const radar = (id: number): Radar => ({ id, x: 5000, y: 5000, Pt_dBm: 20, G_dB: 25, freq: 1e10, bandwidth: 1e6, sigma: 5, thresh_db: -50 })

describe('EntityCard (radar, knob)', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
  })

  function setup() {
    const scene = useSceneStore()
    scene.radars.push(radar(1))
    const w = mount(EntityCard, { props: { kind: 'radar', id: 1 }, global: { stubs: { teleport: true } } })
    return { w, scene }
  }

  function knobFor(w: ReturnType<typeof setup>['w'], prop: string) {
    const field = w.find(`.field[data-prop="${prop}"]`)
    expect(field.exists()).toBe(true)
    return field.findComponent(KnobControl)
  }

  it('展示实体标题 R1', () => {
    const { w } = setup()
    expect(w.text()).toContain('R1')
    expect(w.text()).toContain('雷达')
  })

  it('旋钮旋转调用 updateField 且标 pending（存储值）', async () => {
    const { w, scene } = setup()
    const knob = knobFor(w, 'Pt_dBm')
    // Pt_dBm=20 在范围 -20..60：显示值直接发出，存储值相同
    knob.vm.$emit('update:modelValue', 42)
    await w.vm.$nextTick()
    expect(scene.radars[0].Pt_dBm).toBe(42)
    expect(scene.pendingEdits.has('Pt_dBm_1')).toBe(true)
  })

  it('freq 旋钮显示为 GHz（10 GHz 对应 1e10 Hz）', () => {
    const { w } = setup()
    const knob = knobFor(w, 'freq')
    expect(knob.props('modelValue')).toBeCloseTo(10)
    expect(knob.props('min')).toBe(8)
    expect(knob.props('max')).toBe(18)
  })

  it('x/y 坐标允许负值（min -20000）', () => {
    const { w } = setup()
    expect(knobFor(w, 'x').props('min')).toBe(-20000)
    expect(knobFor(w, 'x').props('max')).toBe(20000)
    expect(knobFor(w, 'y').props('min')).toBe(-20000)
  })

  it('focus/blur 切换 focusedKey', async () => {
    const { w, scene } = setup()
    const knob = w.find('.field[data-prop="Pt_dBm"] .knob')
    await knob.trigger('focus')
    expect(scene.focusedKey).toBe('Pt_dBm_1')
    await knob.trigger('blur')
    expect(scene.focusedKey).toBeNull()
  })

  it('频率键入：显示值换算回存储 Hz', async () => {
    const { w, scene } = setup()
    const knob = knobFor(w, 'freq')
    knob.vm.$emit('update:modelValue', 10.5) // GHz 显示值
    await w.vm.$nextTick()
    expect(scene.radars[0].freq).toBeCloseTo(10.5e9)
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/EntityCard.spec.ts`
Expected: FAIL（旧测试定位的 `input`/滑杆结构已不存在；新文件则 `findComponent(KnobControl)` 为空——以实际失败为准，语义是"旧结构对不上"）

- [ ] **Step 3: 修改 `src/components/EntityCard.vue`**

改动限定四处（其余：标题、删除、jam_type 下拉、pulse、pending 徽标、focus/blur 捕获逻辑全部保留）：
1. `import KnobControl from './KnobControl.vue'`（注意 `noUnusedLocals`：el-slider 若曾显式导入则删掉；全局注册的无需动）。
2. x/y 字段：`{ prop: 'x', label: 'X', min: -20000, max: 20000, step: 100, unit: 'm' }`，y 同理（雷达与干扰机两表都要改）。
3. 字段循环模板的滑杆块替换为旋钮（`displayVal`/`onInput`/`isPending`/`keyOf` 复用不变——换算仍在显示值空间）：

```vue
<div class="fields-grid">
  <div v-for="f in fields" :key="f.prop" class="field" :data-prop="f.prop" :class="{ pending: isPending(f.prop) }">
    <label>
      {{ f.label }}
      <span v-if="isPending(f.prop)" class="tilde">~</span>
    </label>
    <KnobControl
      :model-value="displayVal(f)"
      :min="f.min"
      :max="f.max"
      :step="f.step"
      :unit="f.unit"
      @update:model-value="onInput(f, $event)"
    />
  </div>
</div>
```

（原模板若有 `.val` 读数 span 则删除——读数已进旋钮中央；`@focus.capture/@blur.capture` 保留在卡片根或 `.field` 上不动，knob 的 tabindex 使 focus 事件照常冒泡。）

4. 样式追加（scoped）：

```css
.fields-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(96px, 1fr)); gap: 8px; }
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/EntityCard.spec.ts && npx vitest run && npm run typecheck && npm run build`
Expected: PASS（EntityCard 6 用例；全量绿；typecheck 零错误；build 成功）

- [ ] **Step 5: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/components/EntityCard.vue front_end/src/components/EntityCard.spec.ts
git commit -m "front_end: 参数卡片滑杆换旋钮，坐标放开到 ±20000"
```

---

### Task 4: BottomDrawer 双栏 + App.vue 四窗格装配

**Files:**
- Modify: `front_end/src/components/BottomDrawer.vue`（去标签页→左右双栏 + SplitBar③）
- Modify: `front_end/src/components/BottomDrawer.spec.ts`（重写：双栏渲染 + 高度记忆保留）
- Modify: `front_end/src/App.vue`（SplitBar①装配 + 抽屉双栏装配 + 移除 drawerTab/drawerTabs；WS 接线与 canvasHeight 测量保留）

**Interfaces:**
- Consumes: `SplitBar`（Task 1）、`SceneCanvas/ControlPanel/ResultPanel/HistoryCharts`（props 不变）
- Produces（最终布局，Task 5 验收对象）:
  - BottomDrawer：无 props（移除 `tabs/modelValue`）；保留 `drawerH`/把手拖拽/`ecmsim.drawerH` 记忆/140~60% 钳制；`.body` 内 SplitBar③（`direction horizontal, fixedPane a, minA/minB 280, storageKey ecmsim.resultSplit, defaultValue 0.5, defaultUnit ratio`）包裹 `left`/`right` 插槽
  - App.vue：`.main` 内 SplitBar①（`direction horizontal, fixedPane b, minA 320, minB 240, maxFixed（maxB 语义） 480, storageKey ecmsim.sideW, defaultValue 300`）包裹 `.stage`（SceneCanvas）/`.side`（ControlPanel）；抽屉 `<template #left>ResultPanel / #right>HistoryCharts`；删除 `drawerTab/drawerTabs` 与 `v-model`

- [ ] **Step 1: 重写测试 `src/components/BottomDrawer.spec.ts`**

```ts
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import BottomDrawer from './BottomDrawer.vue'

describe('BottomDrawer（双栏）', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.spyOn(HTMLElement.prototype, 'clientWidth', 'get').mockReturnValue(1200)
    vi.spyOn(HTMLElement.prototype, 'clientHeight', 'get').mockReturnValue(600)
    vi.stubGlobal('ResizeObserver', class {
      observe() {}
      unobserve() {}
      disconnect() {}
    })
  })

  function mountDrawer() {
    return mount(BottomDrawer, {
      slots: { left: '<div>结果内容</div>', right: '<div>图表内容</div>' },
      attachTo: document.body,
    })
  }

  it('左右两栏同时渲染', () => {
    const w = mountDrawer()
    expect(w.text()).toContain('结果内容')
    expect(w.text()).toContain('图表内容')
    w.unmount()
  })

  it('无标签页切换器', () => {
    const w = mountDrawer()
    expect(w.find('.tabbar').exists()).toBe(false)
    w.unmount()
  })

  it('高度拖拽持久化到 localStorage', async () => {
    const w = mountDrawer()
    const handle = w.find('.drawer > .handle')
    handle.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientY: 500 }))
    handle.element.dispatchEvent(new MouseEvent('pointermove', { bubbles: true, clientY: 300 }))
    handle.element.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientY: 300 }))
    expect(Number(localStorage.getItem('ecmsim.drawerH') ?? '0')).toBeGreaterThan(260)
    w.unmount()
  })

  it('再次挂载恢复已存高度', () => {
    localStorage.setItem('ecmsim.drawerH', '400')
    const w = mountDrawer()
    expect((w.find('.drawer').element as HTMLElement).style.height).toBe('400px')
    w.unmount()
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/BottomDrawer.spec.ts`
Expected: FAIL（旧 `tabs/modelValue` props 已传参变化或 `.tabbar` 仍存在——以实际为准；若旧测试恰好通过，说明实现未动，需先确认模板已改）

- [ ] **Step 3: 改写 `src/components/BottomDrawer.vue`**

保留：`drawerH`/`MIN_H`/`DEFAULT_H`/`clamp`/`onDown/onMove/onUp`/setup 期高度恢复/把手模板与样式；删除：`tabs/modelValue` props、`update:modelValue` emit、`select`、`ecmsim.drawerTab` 读写、tabbar 模板与样式。`.body` 改为：

```vue
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
```

并 `import SplitBar from './SplitBar.vue'`。注意：`.drawer > .handle` 选择器要求抽屉把手是 `.drawer` 的直接子元素且类名 `handle` 不变（测试依赖）。

- [ ] **Step 4: 改写 `src/App.vue`**

`<script setup>` 改动：删除 `drawerTab`/`drawerTabs`；其余（stores、WS 控制器、`connectWs`+`sessionId` watch、`canvasHeight`+`measureCanvas`+resize 监听、错误处理）逐字保留。模板：

```vue
<template>
  <div class="app-shell">
    <AppHeader :ws-status="wsStatus" :session-id="scene.sessionId" />
    <main class="main">
      <SplitBar
        direction="horizontal"
        fixed-pane="b"
        :min-a="320"
        :min-b="240"
        :max-fixed="480"
        storage-key="ecmsim.sideW"
        :default-value="300"
      >
        <template #a>
          <section class="stage glass-panel">
            <SceneCanvas :height="canvasHeight" />
          </section>
        </template>
        <template #b>
          <aside class="side glass-panel">
            <ControlPanel />
          </aside>
        </template>
      </SplitBar>
    </main>
    <BottomDrawer>
      <template #left><ResultPanel /></template>
      <template #right><HistoryCharts /></template>
    </BottomDrawer>
    <StatusBar />
  </div>
</template>
```

并 `import SplitBar from './components/SplitBar.vue'`。`<style scoped>`：删除 `.side { width: 300px; ... }` 中的 `width`（SplitBar 设 flex-basis），保留 `flex-shrink: 0; overflow: hidden; display: flex; flex-direction: column`；`.main` 保持 `display:flex`（SplitBar 根占满：确认 `.main` 有确定高度——沿用现有 flex 布局，抽屉高度固定后 `.main` 自动填充剩余）。

- [ ] **Step 5: 运行测试 + 类型 + 构建**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/BottomDrawer.spec.ts && npm run test && npm run typecheck && npm run build`
Expected: 全部 PASS——抽屉 4 用例；全量（旧 AppHeader/SceneCanvas/EntityCard/HistoryCharts/ResultPanel 用例保持绿）；`vue-tsc` 零错误；`vite build` 成功

- [ ] **Step 6: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/components/BottomDrawer.vue front_end/src/components/BottomDrawer.spec.ts front_end/src/App.vue
git commit -m "front_end: 抽屉双栏与四窗格可拖拽装配"
```

---

### Task 5: 集成验收与提交

**Files:**
- Verify: 无新功能；对照 spec §5 逐条勾验；提交所有变更

**Interfaces:**
- Consumes: Task 1–4 全部产出
- Produces: 满足 DoD 的可交付变更

- [ ] **Step 1: 全量自动化验证**

```bash
cd /home/ky/workspace/ECMSim/front_end
npm run test        # vitest 全绿
npm run typecheck   # vue-tsc 零错误
npm run build       # vite build 成功
```

Expected: 三者全 PASS。任何失败 → 回到对应任务修复，不得带病提交。

- [ ] **Step 2: 回归范围检查**

Run: `cd /home/ky/workspace/ECMSim && git status --porcelain && git diff --stat 0c120f9..HEAD -- src static include protos agents`
Expected: 第二条命令零输出（C++ / static / protos / agents 无改动）；变更仅 `front_end/` + `docs/superpowers/`。

- [ ] **Step 3: 手动冒烟清单（需浏览器 + C++ 服务器，人执行——执行者只输出清单，不执行）**

向报告中记录以下清单供用户执行（`build` 下二进制 + `npm run dev`，vite 默认 `http://localhost:5173`）：

| # | 检查项 | 预期 |
|---|--------|------|
| 1 | 三条分隔线拖拽 | 画布↔参数、结果↔历史、上下抽屉均跟手，有最小尺寸钳制 |
| 2 | 位置记忆 | 刷新后三处尺寸恢复 |
| 3 | 旋钮旋转 | -135°~+135° 全行程数值正确（到底/到顶） |
| 4 | 旋钮键入 | 点击读数键入精确值；越界钳制；非法输入回退 |
| 5 | 旋钮滚轮/键盘 | 滚轮 ±step；方向键/Home/End 正常 |
| 6 | 负坐标全链路 | 雷达拖到负坐标区 → 卡片读数为负 → 300ms pending 消失 → 仿真正常 |
| 7 | 同步保护 | 键入时（focus）WS 广播不覆盖正在编辑的字段 |
| 8 | 窄窗 ~900px | 四栏仍可用，无重叠 |
| 9 | 旧功能回归 | 会话/增删实体/仿真/自动仿真/重置/历史图表均正常 |

- [ ] **Step 4: 终验 + Commit**

```bash
cd /home/ky/workspace/ECMSim/front_end && npm run test && npm run typecheck && npm run build
cd /home/ky/workspace/ECMSim
git add -A front_end docs/superpowers/plans/2026-10-02-frontend-panes-knob.md
git commit -m "front_end: 四窗格拖拽+旋钮+坐标负值集成验收"
```

**DoD checklist（全部勾选才可宣告完成）：**

- [ ] `npm run test` 全绿（含 SplitBar/KnobControl/knobMath/EntityCard/BottomDrawer 新用例）
- [ ] `npm run typecheck` 零错误
- [ ] `npm run build` 成功
- [ ] 回归范围检查：C++ / static / protos / agents 零改动
- [ ] 冒烟清单已写入报告待人工执行

