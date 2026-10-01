import { beforeEach, describe, expect, it, vi } from 'vitest'
import { createPinia, setActivePinia } from 'pinia'
import type { Jammer, Radar } from '../types/api'
import * as api from '../api/scene'
import { pendingKey, useSceneStore } from './scene'

vi.mock('element-plus', () => ({ ElMessage: { error: vi.fn(), success: vi.fn() } }))
vi.mock('../api/scene', () => ({
  createSession: vi.fn(), fetchSceneConfig: vi.fn(), deleteScene: vi.fn(),
  addRadar: vi.fn(), updateRadar: vi.fn(), deleteRadar: vi.fn(),
  addJammer: vi.fn(), updateJammer: vi.fn(), deleteJammer: vi.fn(), simulate: vi.fn(),
}))

const radar = (id: number): Radar => ({ id, x: 100, y: 200, Pt_dBm: 20, G_dB: 25, freq: 1e10, bandwidth: 1e6, sigma: 5, thresh_db: -50 })
const jammer = (id: number): Jammer => ({ id, x: 300, y: 400, Pj_dBm: 40, Gj_dB: 20, jam_freq: 1e10, jam_type: 'NOISE_JAM' })

describe('scene store: CRUD 与 updateField', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    vi.mocked(api.createSession).mockResolvedValue({ success: true, session_id: 'sess-9' })
    vi.mocked(api.fetchSceneConfig).mockResolvedValue({ success: true, radars: [], jammers: [] })
    vi.mocked(api.addRadar).mockResolvedValue({ success: true })
    vi.mocked(api.addJammer).mockResolvedValue({ success: true })
    vi.mocked(api.deleteRadar).mockResolvedValue({ success: true })
    vi.mocked(api.deleteJammer).mockResolvedValue({ success: true })
  })

  it('pendingKey: 雷达无 j 前缀，干扰机带 j 前缀', () => {
    expect(pendingKey('radar', 'Pt_dBm', 1)).toBe('Pt_dBm_1')
    expect(pendingKey('jammer', 'x', 2)).toBe('x_j2')
    expect(pendingKey('jammer', 'jam_type', 3)).toBe('jam_type_j3')
  })

  it('initSession 保存 session_id', async () => {
    const s = useSceneStore()
    await s.initSession()
    expect(s.sessionId).toBe('sess-9')
  })

  it('initSession 失败时抛错', async () => {
    vi.mocked(api.createSession).mockResolvedValue({ success: false, error: 'boom' })
    const s = useSceneStore()
    await expect(s.initSession()).rejects.toThrow('boom')
  })

  it('fetchScene 覆盖 radars/jammers', async () => {
    vi.mocked(api.fetchSceneConfig).mockResolvedValue({ success: true, radars: [radar(1)], jammers: [jammer(1)] })
    const s = useSceneStore()
    await s.initSession()
    await s.fetchScene()
    expect(s.radars).toHaveLength(1)
    expect(s.jammers).toHaveLength(1)
  })

  it('addRadar 取 max(id)+1 并入列，调用 API', async () => {
    const s = useSceneStore()
    await s.initSession()
    s.radars.push(radar(3))
    await s.addRadar()
    expect(s.radars).toHaveLength(2)
    expect(s.radars[1].id).toBe(4)
    expect(api.addRadar).toHaveBeenCalledWith('sess-9', expect.objectContaining({ id: 4, x: 5000, freq: 1e10 }))
  })

  it('addRadar 失败不入列', async () => {
    vi.mocked(api.addRadar).mockResolvedValue({ success: false, error: 'dup' })
    const s = useSceneStore()
    await s.initSession()
    await s.addRadar()
    expect(s.radars).toHaveLength(0)
  })

  it('removeRadar 删除并调用 API', async () => {
    const s = useSceneStore()
    await s.initSession()
    s.radars.push(radar(1), radar(2))
    await s.removeRadar(1)
    expect(s.radars.map(r => r.id)).toEqual([2])
    expect(api.deleteRadar).toHaveBeenCalledWith('sess-9', 1)
  })

  it('addJammer / removeJammer 对称工作', async () => {
    const s = useSceneStore()
    await s.initSession()
    await s.addJammer()
    expect(s.jammers[0]).toMatchObject({ id: 1, x: 6000, jam_type: 'NOISE_JAM' })
    await s.removeJammer(1)
    expect(s.jammers).toHaveLength(0)
    expect(api.deleteJammer).toHaveBeenCalledWith('sess-9', 1)
  })

  it('updateField 写本地值 + 标 pending + 记 lastEdited', () => {
    const s = useSceneStore()
    s.radars.push(radar(1))
    s.updateField('radar', 1, 'Pt_dBm', 42)
    expect(s.radars[0].Pt_dBm).toBe(42)
    expect(s.pendingEdits.has('Pt_dBm_1')).toBe(true)
    expect(s.dirtyCount).toBe(1)
    expect(s.lastEdited).toMatchObject({ kind: 'radar', id: 1 })
  })

  it('updateField 对不存在的实体是 no-op', () => {
    const s = useSceneStore()
    s.updateField('radar', 99, 'x', 1)
    expect(s.pendingEdits.size).toBe(0)
  })

  it('pulseEntity 记录 pulsed（拖拽后卡片高亮用）', () => {
    const s = useSceneStore()
    s.pulseEntity('jammer', 2)
    expect(s.pulsed).toMatchObject({ kind: 'jammer', id: 2 })
  })
})
