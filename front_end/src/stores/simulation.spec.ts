import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { createPinia, setActivePinia } from 'pinia'
import { nextTick } from 'vue'
import { ElMessage } from 'element-plus'
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

  it('场景实体 ID 签名变化 → 自动清空结果与历史', async () => {
    const sim = useSimulationStore()
    const scene = useSceneStore()
    await sim.run()
    expect(sim.results).toHaveLength(1)
    expect(sim.history).toHaveLength(1)

    scene.radars.push({
      id: 9, x: 100, y: 100, Pt_dBm: 20, G_dB: 25,
      freq: 1e10, bandwidth: 1e6, sigma: 5, thresh_db: -50,
    })
    await nextTick() // watch flush: 'pre' → nextTick 后回调已执行

    expect(sim.results).toBeNull()
    expect(sim.history).toHaveLength(0)
  })

  it('reset 删除会话失败：不抛异常、提示错误、仍清空结果', async () => {
    vi.mocked(api.deleteScene).mockRejectedValue(new Error('gone'))
    const sim = useSimulationStore()
    await sim.run()
    expect(sim.results).not.toBeNull()

    await expect(sim.reset()).resolves.toBeUndefined()

    expect(api.deleteScene).toHaveBeenCalledWith('sess-1')
    expect(vi.mocked(ElMessage.error)).toHaveBeenCalledWith('gone')
    expect(sim.results).toBeNull() // 结果清空是契约，必须钉死
    expect(sim.history).toHaveLength(0)
    expect(sim.statusMsg).toBe('已重置')
  })

  it('startAuto 重复调用只启动一个定时器', async () => {
    vi.useFakeTimers()
    vi.mocked(api.simulate).mockResolvedValue({ success: true, sim_results: [result(1, 1, 1)] })
    const sim = useSimulationStore()
    sim.auto.interval = 2
    sim.startAuto()
    sim.startAuto() // enabled 守卫：不应再建第二个 interval
    expect(sim.auto.enabled).toBe(true)

    await vi.advanceTimersByTimeAsync(1000)
    expect(sim.auto.remaining).toBe(1) // 只被单个定时器递减一次（双定时器会减到 0 并提前触发）

    await vi.advanceTimersByTimeAsync(1000)
    expect(api.simulate).toHaveBeenCalledTimes(1)

    sim.stopAuto()
  })

  it('结果无 jammer_details 时 zeta 记 0', async () => {
    const sim = useSimulationStore()
    vi.mocked(api.simulate).mockResolvedValue({
      success: true,
      sim_results: [{ radar_id: 7, SINR_dB: -1.5, jsr_dB: 10, detect_success: false }],
    })
    expect(await sim.run()).toBe(true)
    expect(sim.history).toHaveLength(1)
    expect(sim.history[0]).toEqual({ labels: [7], sinr: [-1.5], zeta: [0] })
  })

  it('run 进行中 statusMsg 为 仿真中...', async () => {
    let release!: (v: SimulateRes) => void
    vi.mocked(api.simulate).mockReturnValue(new Promise(res => { release = res }))
    const sim = useSimulationStore()
    const p = sim.run()
    expect(sim.statusMsg).toBe('仿真中...') // 同步置位，先于任何 await
    expect(sim.running).toBe(true)
    release({ success: true, sim_results: [result(1, 1, 1)] })
    expect(await p).toBe(true)
    expect(sim.statusMsg).toBe('步骤 1')
  })
})
