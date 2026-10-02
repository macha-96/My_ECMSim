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
