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
