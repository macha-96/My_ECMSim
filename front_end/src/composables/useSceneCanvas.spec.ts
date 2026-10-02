import { describe, expect, it } from 'vitest'
import { DEFAULT_VIEWPORT, computeViewport, hitTest, makeTransform } from './useSceneCanvas'

describe('computeViewport / makeTransform', () => {
  it('方形画布下视口中心在世界原点且覆盖 30000m', () => {
    const vp = computeViewport(800, 800)
    expect(vp.w).toBeCloseTo(30000)
    expect(vp.h).toBeCloseTo(30000)
    expect(vp.x + vp.w / 2).toBeCloseTo(0)
    expect(vp.y + vp.h / 2).toBeCloseTo(0)
  })

  it('宽扁画布保证 30000m 可见（视口变宽）', () => {
    const vp = computeViewport(1600, 800)
    expect(vp.w).toBeGreaterThanOrEqual(30000)
    expect(vp.h).toBeGreaterThanOrEqual(30000)
    expect(vp.x + vp.w / 2).toBeCloseTo(0)
  })

  it('worldToScreen 与 screenToWorld 互逆', () => {
    const vp = DEFAULT_VIEWPORT
    const t = makeTransform(vp, 800, 600)
    const { sx, sy } = t.toScreen(5000, -3000)
    const back = t.toWorld(sx, sy)
    expect(back.wx).toBeCloseTo(5000, 6)
    expect(back.wy).toBeCloseTo(-3000, 6)
  })

  it('世界 y 向上、屏幕 y 向下（世界原点在屏幕中心）', () => {
    const vp = computeViewport(800, 800)
    const t = makeTransform(vp, 800, 800)
    const origin = t.toScreen(0, 0)
    expect(origin.sx).toBeCloseTo(400)
    expect(origin.sy).toBeCloseTo(400)
    const above = t.toScreen(0, 15000)
    expect(above.sy).toBeLessThan(origin.sy) // 世界 y+ 在屏幕上方
  })
})

describe('hitTest', () => {
  const pts = [
    { kind: 'radar' as const, id: 1, sx: 100, sy: 100 },
    { kind: 'jammer' as const, id: 2, sx: 130, sy: 100 },
  ]

  it('命中半径内返回最近实体', () => {
    expect(hitTest(110, 100, pts)).toEqual({ kind: 'radar', id: 1 })
    expect(hitTest(128, 100, pts)).toEqual({ kind: 'jammer', id: 2 })
  })

  it('半径外返回 null', () => {
    expect(hitTest(100, 140, pts)).toBeNull()
  })

  it('空列表返回 null', () => {
    expect(hitTest(0, 0, [])).toBeNull()
  })
})
