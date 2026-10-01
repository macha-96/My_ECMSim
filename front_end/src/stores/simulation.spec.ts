import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { createPinia, setActivePinia } from 'pinia'
import type { SimResult, SimulateRes } from '../types/api'
import * as api from '../api/scene'
import { MAX_HISTORY, useSimulationStore } from './simulation'
import { useSceneStore } from './scene'

vi.mock('element-plus', () => ({ ElMessage: { error: vi.fn(), success: vi.fn() } }))
vi.mock('../api/scene', () => ({
  createSession: vi.fn(), fetchSceneConfig: vi.fn(), deleteScene: vi.fn(),
  addRadar: vi.fn(), updateRadar: vi.fn(), deleteRadar: vi.fn(),
  addJammer: vi.fn(), updateJammer: vi.fn(), deleteJammer: vi.fn(), simulate: vi.fn(),
}))

const result = (radarId: number, sinr: number, zeta: number): SimResult => ({
  radar_id: radarId, SINR_dB: sinr, jsr_dB: 10, detect_success: true,
  jammer_details: [{ jammer_id: 1, freq_delta_Hz: 1e6, freq_match_factor: zeta, original_power_W: 1, effective_power_W: 0.5 }],
})

describe('simulation store', () => {
  beforeEach(async () => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    vi.mocked(api.createSession).mockResolvedValue({ success: true, session_id: 'sess-1' })
    vi.mocked(api.fetchSceneConfig).mockResolvedValue({ success: true, radars: [], jammers: [] })
    vi.mocked(api.deleteScene).mockResolvedValue({ success: true })
    vi.mocked(api.simulate).mockResolvedValue({ success: true, sim_results: [result(1, -3.2, 0.8)] })
    const scene = useSceneStore()
    await scene.initSession()
  })

  afterEach(() => {
    vi.useRealTimers()
  })

  it('run 成功：存结果、追加历史、running 复位', async () => {
    const sim = useSimulationStore()
    const ok = await sim.run()
    expect(ok).toBe(true)
    expect(sim.results).toHaveLength(1)
    expect(sim.history).toHaveLength(1)
    expect(sim.history[0]).toEqual({ labels: [1], sinr: [-3.2], zeta: [0.8] })
    expect(sim.running).toBe(false)
    expect(sim.statusMsg).toBe('步骤 1')
  })

  it('run 失败：返回 false、running 复位、状态为仿真失败', async () => {
    vi.mocked(api.simulate).mockRejectedValue(new Error('down'))
    const sim = useSimulationStore()
    const ok = await sim.run()
    expect(ok).toBe(false)
    expect(sim.running).toBe(false)
    expect(sim.statusMsg).toBe('仿真失败')
    expect(sim.results).toBeNull()
  })

  it(`历史封顶 ${MAX_HISTORY} 步`, async () => {
    const sim = useSimulationStore()
    for (let i = 0; i < MAX_HISTORY + 5; i++) await sim.run()
    expect(sim.history).toHaveLength(MAX_HISTORY)
  })

  it('无会话时 run 直接返回 false', async () => {
    const scene = useSceneStore()
    scene.sessionId = null
    const sim = useSimulationStore()
    expect(await sim.run()).toBe(false)
    expect(api.simulate).not.toHaveBeenCalled()
  })

  it('startAuto 按间隔触发 run，stopAuto 停止', async () => {
    vi.useFakeTimers()
    vi.mocked(api.simulate).mockResolvedValue({ success: true, sim_results: [result(1, 1, 1)] })
    const sim = useSimulationStore()
    sim.auto.interval = 2
    sim.startAuto()
    expect(sim.auto.enabled).toBe(true)
    expect(sim.auto.remaining).toBe(2)

    await vi.advanceTimersByTimeAsync(1000)
    expect(sim.auto.remaining).toBe(1)
    expect(api.simulate).not.toHaveBeenCalled()

    await vi.advanceTimersByTimeAsync(1000)
    expect(api.simulate).toHaveBeenCalledTimes(1)
    expect(sim.auto.remaining).toBe(2) // 回到最新 interval

    sim.stopAuto()
    expect(sim.auto.enabled).toBe(false)
    await vi.advanceTimersByTimeAsync(5000)
    expect(api.simulate).toHaveBeenCalledTimes(1)
  })

  it('run 进行中不会重复触发（running 互斥）', async () => {
    let release!: (v: SimulateRes) => void
    vi.mocked(api.simulate).mockReturnValue(new Promise(res => { release = res }))
    const sim = useSimulationStore()
    const p1 = sim.run()
    const p2 = sim.run()
    release({ success: true, sim_results: [result(1, 1, 1)] })
    expect(await p1).toBe(true)
    expect(await p2).toBe(false) // 第二次因 running 互斥被拒
    expect(api.simulate).toHaveBeenCalledTimes(1)
  })

  it('reset 停止自动仿真并清空结果', async () => {
    vi.useFakeTimers()
    const sim = useSimulationStore()
    await sim.run()
    sim.startAuto()
    await sim.reset()
    expect(sim.auto.enabled).toBe(false)
    expect(sim.results).toBeNull()
    expect(sim.history).toHaveLength(0)
    expect(api.deleteScene).toHaveBeenCalledWith('sess-1')
    await vi.advanceTimersByTimeAsync(5000)
    // 仅保留测试开头手动 run() 的 1 次调用；推进 5s 无新增 → 自动仿真已停
    expect(api.simulate).toHaveBeenCalledTimes(1)
  })
})
