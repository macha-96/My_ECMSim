// 画布坐标纯函数 — 不依赖 DOM/Canvas，组件只负责调用
import type { EntityKind } from '../types/api'

export interface Viewport {
  x: number
  y: number
  w: number
  h: number
}

export const DEFAULT_VIEWPORT: Viewport = { x: -1500, y: -1500, w: 30000, h: 30000 }

/** 以世界原点为中心，保证 30000m 视野在画布内完整可见 */
export function computeViewport(canvasW: number, canvasH: number): Viewport {
  const aspect = canvasW / Math.max(1, canvasH)
  let w = DEFAULT_VIEWPORT.w
  let h = DEFAULT_VIEWPORT.h
  if (aspect > w / h) {
    w = h * aspect
  } else {
    h = w / aspect
  }
  return { x: -w / 2, y: -h / 2, w, h }
}

export interface Transform {
  scale: number
  offsetX: number
  offsetY: number
  toScreen(wx: number, wy: number): { sx: number; sy: number }
  toWorld(sx: number, sy: number): { wx: number; wy: number }
}

export function makeTransform(vp: Viewport, canvasW: number, canvasH: number): Transform {
  const scale = Math.min(canvasW / vp.w, canvasH / vp.h)
  // 世界中心映射到画布中心
  const offsetX = canvasW / 2 - (vp.x + vp.w / 2) * scale
  const offsetY = canvasH / 2 + (vp.y + vp.h / 2) * scale
  return {
    scale,
    offsetX,
    offsetY,
    toScreen(wx, wy) {
      return { sx: wx * scale + offsetX, sy: -wy * scale + offsetY }
    },
    toWorld(sx, sy) {
      return { wx: (sx - offsetX) / scale, wy: -(sy - offsetY) / scale }
    },
  }
}

export interface HitPoint {
  kind: EntityKind
  id: number
  sx: number
  sy: number
}

/** 命中测试：半径内取距点击点最近者；默认 12px */
export function hitTest(
  sx: number,
  sy: number,
  points: HitPoint[],
  radius = 12,
): { kind: EntityKind; id: number } | null {
  let best: { kind: EntityKind; id: number } | null = null
  let bestDist = radius
  for (const p of points) {
    const d = Math.hypot(p.sx - sx, p.sy - sy)
    if (d <= bestDist) {
      bestDist = d
      best = { kind: p.kind, id: p.id }
    }
  }
  return best
}
