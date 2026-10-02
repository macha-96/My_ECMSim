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
