import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import SceneCanvas from './SceneCanvas.vue'
import { useSceneStore } from '../stores/scene'
import { computeViewport, makeTransform } from '../composables/useSceneCanvas'
import type { Radar } from '../types/api'

// jsdom 无真实 canvas — stub getContext 返回可记录的 mock
function stubCanvas() {
  const draw: string[] = []
  const ctx = {
    clearRect: () => draw.push('clear'),
    fillText: () => draw.push('text'),
    beginPath: () => draw.push('begin'),
    arc: () => draw.push('arc'),
    stroke: () => draw.push('stroke'),
    fill: () => draw.push('fill'),
    moveTo: () => undefined,
    lineTo: () => undefined,
    setLineDash: () => undefined,
    strokeRect: () => undefined,
    save: () => undefined,
    restore: () => undefined,
    fillRect: () => draw.push('fillRect'),
  }
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(ctx as unknown as CanvasRenderingContext2D)
  return draw
}

// jsdom 无布局：clientWidth/clientHeight 恒为 0 — 显式给画布尺寸使坐标变换确定（800x600）
function stubCanvasSize(canvas: HTMLCanvasElement, w = 800, h = 600): void {
  Object.defineProperty(canvas, 'clientWidth', { value: w, configurable: true })
  Object.defineProperty(canvas, 'clientHeight', { value: h, configurable: true })
}

describe('SceneCanvas', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.restoreAllMocks()
  })

  function setup() {
    const draw = stubCanvas()
    const wrapper = mount(SceneCanvas, { props: { height: 600 }, attachTo: document.body })
    const scene = useSceneStore()
    const radar: Radar = { id: 1, x: 5000, y: 5000, Pt_dBm: 20, G_dB: 25, freq: 1e10, bandwidth: 1e6, sigma: 5, thresh_db: -50 }
    scene.radars.push(radar)
    return { wrapper, scene, draw, radar }
  }

  it('渲染 canvas 元素且高度正确', () => {
    const { wrapper } = setup()
    const canvas = wrapper.find('canvas')
    expect(canvas.exists()).toBe(true)
    expect(canvas.attributes('height')).toBe('600')
  })

  it('pointerdown 命中雷达后 move 更新 x/y 并标记 pending', async () => {
    const { wrapper, scene, radar } = setup()
    const canvas = wrapper.find('canvas').element as HTMLCanvasElement
    stubCanvasSize(canvas)
    // 冲刷一帧 rAF：让 draw() 以 800x600 实测尺寸完成首绘，视口/命中几何此后确定
    await new Promise<void>(resolve => requestAnimationFrame(() => resolve()))
    // 画布 800x600：原点居中 (400,300)，scale=0.02 → 世界 (5000,5000) 屏幕 (500,200)
    // 由纯函数计算而非硬编码，避免 jsdom 几何变化导致断言漂移
    const t = makeTransform(computeViewport(800, 600), 800, 600)
    const p = t.toScreen(radar.x, radar.y)
    canvas.dispatchEvent(new PointerEvent('pointerdown', { clientX: p.sx, clientY: p.sy, pointerId: 1, bubbles: true }))
    canvas.dispatchEvent(new PointerEvent('pointermove', { clientX: p.sx + 20, clientY: p.sy, pointerId: 1, bubbles: true }))
    canvas.dispatchEvent(new PointerEvent('pointerup', { clientX: p.sx + 20, clientY: p.sy, pointerId: 1, bubbles: true }))
    expect(scene.pendingEdits.size).toBeGreaterThan(0)
    expect(scene.radars[0].x).not.toBe(5000) // 已被拖动更新
  })

  it('pointerdown 未命中可平移视口，平移在重绘后保持', async () => {
    const { wrapper, scene, radar } = setup()
    const canvas = wrapper.find('canvas').element as HTMLCanvasElement
    stubCanvasSize(canvas)
    await new Promise<void>(resolve => requestAnimationFrame(() => resolve()))
    const t = makeTransform(computeViewport(800, 600), 800, 600)
    const p = t.toScreen(radar.x, radar.y)
    // 空白处按下 → 右拖 100px 平移视口（内容右移 100px）
    canvas.dispatchEvent(new PointerEvent('pointerdown', { clientX: 400, clientY: 300, pointerId: 1, bubbles: true }))
    canvas.dispatchEvent(new PointerEvent('pointermove', { clientX: 500, clientY: 300, pointerId: 1, bubbles: true }))
    canvas.dispatchEvent(new PointerEvent('pointerup', { clientX: 500, clientY: 300, pointerId: 1, bubbles: true }))
    // 冲刷 rAF：平移触发的重绘不得把视口重置回默认（否则平移一帧即回弹）
    await new Promise<void>(resolve => requestAnimationFrame(() => resolve()))
    // 雷达屏幕坐标应随视口右移 100px → 在新位置命中并拖动
    canvas.dispatchEvent(new PointerEvent('pointerdown', { clientX: p.sx + 100, clientY: p.sy, pointerId: 1, bubbles: true }))
    canvas.dispatchEvent(new PointerEvent('pointermove', { clientX: p.sx + 120, clientY: p.sy, pointerId: 1, bubbles: true }))
    canvas.dispatchEvent(new PointerEvent('pointerup', { clientX: p.sx + 120, clientY: p.sy, pointerId: 1, bubbles: true }))
    expect(scene.pendingEdits.size).toBeGreaterThan(0)
    expect(scene.radars[0].x).not.toBe(5000)
  })

  it('首绘前的命中几何以世界原点为中心', () => {
    const { wrapper, scene, radar } = setup()
    const canvas = wrapper.find('canvas').element as HTMLCanvasElement
    stubCanvasSize(canvas)
    // 不冲刷 rAF：直接在首绘前派发 — 初始视口必须与 draw() 同为原点居中
    const t = makeTransform(computeViewport(800, 600), 800, 600)
    const p = t.toScreen(radar.x, radar.y)
    canvas.dispatchEvent(new PointerEvent('pointerdown', { clientX: p.sx, clientY: p.sy, pointerId: 1, bubbles: true }))
    canvas.dispatchEvent(new PointerEvent('pointermove', { clientX: p.sx + 20, clientY: p.sy, pointerId: 1, bubbles: true }))
    canvas.dispatchEvent(new PointerEvent('pointerup', { clientX: p.sx + 20, clientY: p.sy, pointerId: 1, bubbles: true }))
    expect(scene.pendingEdits.size).toBeGreaterThan(0)
    expect(scene.radars[0].x).not.toBe(5000)
  })

  it('首绘后位图尺寸同步为实测画布尺寸', async () => {
    stubCanvas()
    const wrapper = mount(SceneCanvas, { props: { height: 600 }, attachTo: document.body })
    const canvas = wrapper.find('canvas').element as HTMLCanvasElement
    // jsdom 无布局 → stub clientWidth=800；draw 须把位图从 HTML 默认 300 同步为 800
    stubCanvasSize(canvas)
    await new Promise<void>(resolve => requestAnimationFrame(() => resolve()))
    expect(canvas.width).toBe(800)
    expect(canvas.height).toBe(600)
    expect(canvas.width).toBeGreaterThan(0)
  })

  it('空场景 pointerdown 不产生 pending', async () => {
    stubCanvas()
    const wrapper = mount(SceneCanvas, { props: { height: 600 }, attachTo: document.body })
    const canvas = wrapper.find('canvas').element as HTMLCanvasElement
    canvas.dispatchEvent(new PointerEvent('pointerdown', { clientX: 10, clientY: 10, pointerId: 1, bubbles: true }))
    canvas.dispatchEvent(new PointerEvent('pointerup', { clientX: 10, clientY: 10, pointerId: 1, bubbles: true }))
    const { useSceneStore } = await import('../stores/scene')
    expect(useSceneStore().pendingEdits.size).toBe(0)
  })
})
