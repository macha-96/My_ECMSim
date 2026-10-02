import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import EntityCard from './EntityCard.vue'
import { useSceneStore } from '../stores/scene'
import type { Radar } from '../types/api'

vi.mock('element-plus', () => ({
  ElMessage: { error: vi.fn(), success: vi.fn() },
  ElMessageBox: { confirm: vi.fn().mockResolvedValue(undefined) },
}))

const radar = (id: number): Radar => ({ id, x: 5000, y: 5000, Pt_dBm: 20, G_dB: 25, freq: 1e10, bandwidth: 1e6, sigma: 5, thresh_db: -50 })

describe('EntityCard (radar)', () => {
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

  it('展示实体标题 R1', () => {
    const { w } = setup()
    expect(w.text()).toContain('R1')
    expect(w.text()).toContain('雷达')
  })

  it('修改功率滑杆调用 updateField 且标 pending', async () => {
    const { w, scene } = setup()
    // 找到 Pt_dBm 对应的 input（el-slider show-input 生成 input[type=number]）
    const inputs = w.findAll('input')
    expect(inputs.length).toBeGreaterThan(0)
    // 输入定位适配（sanctioned）：末位 input 属于 thresh_db —— 按 data-prop 精确取功率输入
    const powerInput = w.find('.field[data-prop="Pt_dBm"] input')
    expect(powerInput.exists()).toBe(true)
    await powerInput.setValue('42')
    await powerInput.trigger('change')
    expect(scene.pendingEdits.size).toBeGreaterThan(0)
    expect(scene.pendingEdits.has('Pt_dBm_1')).toBe(true)
  })

  it('freq 显示为 GHz（10 GHz 对应 1e10 Hz）', () => {
    const { w } = setup()
    const sliderInputs = w.findAll('input').map(i => i.attributes('value') ?? (i.element as HTMLInputElement).value)
    expect(sliderInputs.some(v => v === '10' || v.startsWith('10'))).toBe(true)
  })

  it('focus/blur 切换 focusedKey', async () => {
    const { w, scene } = setup()
    const input = w.find('input')
    await input.trigger('focus')
    expect(scene.focusedKey).not.toBeNull()
    await input.trigger('blur')
    expect(scene.focusedKey).toBeNull()
  })
})
