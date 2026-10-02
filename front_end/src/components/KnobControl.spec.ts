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
