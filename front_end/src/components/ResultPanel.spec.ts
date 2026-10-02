import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import ResultPanel from './ResultPanel.vue'
import JammerDetailTable from './JammerDetailTable.vue'
import { useSimulationStore } from '../stores/simulation'
import type { SimResult } from '../types/api'

vi.mock('element-plus', () => ({
  ElMessage: { error: vi.fn(), success: vi.fn() },
}))

const result: SimResult = {
  radar_id: 1, SINR_dB: -3.2, jsr_dB: 12.5, detect_success: false,
  total_effective_jam_power_W: 0.42, jam_success_score: 0.78, is_deception_active: true,
  jammer_details: [{ jammer_id: 2, freq_delta_Hz: 1e6, freq_match_factor: 0.87, original_power_W: 1.5, effective_power_W: 0.3 }],
}

describe('ResultPanel', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
  })

  it('无结果显示空态文案', () => {
    const w = mount(ResultPanel)
    expect(w.text()).toContain('尚未运行仿真')
  })

  it('有结果展示雷达行与检测状态', async () => {
    const sim = useSimulationStore()
    sim.results = [result]
    const w = mount(ResultPanel, { attachTo: document.body })
    await w.vm.$nextTick()
    expect(w.text()).toContain('R1')
    expect(w.text()).toContain('-3.2')
    expect(w.text()).toContain('未检出')
    w.unmount()
  })

  it('未运行时显示运行按钮（sessionId 有值时可点）', async () => {
    const { useSceneStore } = await import('../stores/scene')
    const scene = useSceneStore()
    scene.sessionId = 'sess-1'
    const w = mount(ResultPanel)
    await w.vm.$nextTick()
    const btns = w.findAll('button')
    expect(btns.some(b => b.text().includes('运行仿真'))).toBe(true)
  })

  it('running 时运行按钮 disabled', async () => {
    const { useSceneStore } = await import('../stores/scene')
    const scene = useSceneStore()
    scene.sessionId = 'sess-1'
    const sim = useSimulationStore()
    sim.running = true
    const w = mount(ResultPanel)
    await w.vm.$nextTick()
    const runBtn = w.findAll('button').find(b => b.text().includes('运行仿真'))
    expect(runBtn?.attributes('disabled')).toBeDefined()
  })
})

describe('JammerDetailTable', () => {
  it('展示干扰机行与换算值', () => {
    const w = mount(JammerDetailTable, { props: { details: result.jammer_details! } })
    const text = w.text()
    expect(text).toContain('J2')
    expect(text).toContain('1.00') // 1e6 Hz → 1.00 MHz
    expect(text).toContain('0.87') // ζ
    expect(text).toContain('1500') // 1.5 W → 1500 mW
  })

  it('空数组渲染空提示', () => {
    const w = mount(JammerDetailTable, { props: { details: [] } })
    expect(w.text()).toContain('无干扰机参与')
  })
})
