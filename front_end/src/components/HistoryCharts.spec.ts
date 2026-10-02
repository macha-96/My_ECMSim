import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import HistoryCharts from './HistoryCharts.vue'

const setOption = vi.fn()
// 形参加 rest 以接受 echarts.init(dom, opts?) 的转发调用（否则 vue-tsc TS2556）
const init = vi.fn((..._args: unknown[]) => ({ setOption, resize: vi.fn(), dispose: vi.fn(), on: vi.fn() }))
vi.mock('echarts', () => ({ init: (...args: unknown[]) => init(...args) }))

describe('HistoryCharts', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    // jsdom 无布局 → 手动补尺寸
    vi.spyOn(HTMLElement.prototype, 'clientWidth', 'get').mockReturnValue(600)
    vi.spyOn(HTMLElement.prototype, 'clientHeight', 'get').mockReturnValue(300)
    vi.stubGlobal('ResizeObserver', class {
      observe() {}
      unobserve() {}
      disconnect() {}
    })
  })

  it('无数据时显示占位且不初始化图表', () => {
    const w = mount(HistoryCharts)
    expect(w.text()).toContain('暂无数据')
    expect(init).not.toHaveBeenCalled()
  })

  it('有历史时懒初始化图表一次', async () => {
    const { useSimulationStore } = await import('../stores/simulation')
    const sim = useSimulationStore()
    sim.history.push({ labels: [1], sinr: [-3], zeta: [0.8] })
    const w = mount(HistoryCharts, { attachTo: document.body })
    await w.vm.$nextTick()
    expect(init).toHaveBeenCalledTimes(1)
    expect(setOption).toHaveBeenCalled()
    w.unmount()
  })

  it('追加历史触发 setOption 更新', async () => {
    const { useSimulationStore } = await import('../stores/simulation')
    const sim = useSimulationStore()
    sim.history.push({ labels: [1], sinr: [-3], zeta: [0.8] })
    const w = mount(HistoryCharts, { attachTo: document.body })
    await w.vm.$nextTick()
    setOption.mockClear()
    sim.history.push({ labels: [1], sinr: [-2], zeta: [0.6] })
    await w.vm.$nextTick()
    expect(setOption).toHaveBeenCalled()
    w.unmount()
  })

  it('卸载时 dispose', async () => {
    const { useSimulationStore } = await import('../stores/simulation')
    const sim = useSimulationStore()
    sim.history.push({ labels: [1], sinr: [0], zeta: [1] })
    const w = mount(HistoryCharts, { attachTo: document.body })
    await w.vm.$nextTick()
    const inst = init.mock.results[0].value as { dispose: ReturnType<typeof vi.fn> }
    w.unmount()
    expect(inst.dispose).toHaveBeenCalled()
  })

  it('系列色为字面 hex（zrender 不解析 var()）', async () => {
    const { useSimulationStore } = await import('../stores/simulation')
    const sim = useSimulationStore()
    sim.history.push({ labels: [1], sinr: [0], zeta: [1] })
    const w = mount(HistoryCharts, { attachTo: document.body })
    await w.vm.$nextTick()
    const opt = setOption.mock.calls[0][0] as { series: { itemStyle: { color: string } }[] }
    expect(opt.series[0].itemStyle.color).toMatch(/^#[0-9a-fA-F]{6}$/)
    expect(opt.series[0].itemStyle.color).not.toContain('var(')
    w.unmount()
  })
})
