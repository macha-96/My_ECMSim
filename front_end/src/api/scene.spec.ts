import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { Jammer, Radar } from '../types/api'
import {
  addJammer, addRadar, createSession, deleteRadar, fetchSceneConfig, simulate, updateRadar,
} from './scene'

const radar: Radar = { id: 1, x: 5000, y: 5000, Pt_dBm: 20, G_dB: 25, freq: 1e10, bandwidth: 1e6, sigma: 5, thresh_db: -50 }

describe('scene api', () => {
  let fetchMock: ReturnType<typeof vi.fn>

  beforeEach(() => {
    fetchMock = vi.fn().mockResolvedValue({ ok: true, json: async () => ({ success: true }) })
    vi.stubGlobal('fetch', fetchMock)
  })
  afterEach(() => vi.unstubAllGlobals())

  function bodyOf(callIndex: number): Record<string, unknown> {
    const init = fetchMock.mock.calls[callIndex][1] as RequestInit
    return JSON.parse(String(init.body)) as Record<string, unknown>
  }

  it('createSession 发送空对象到 /api/scene', async () => {
    await createSession()
    expect(fetchMock.mock.calls[0][0]).toBe('/api/scene')
    expect(bodyOf(0)).toEqual({})
  })

  it('fetchSceneConfig 携带 action=config', async () => {
    await fetchSceneConfig('sess-1')
    expect(fetchMock.mock.calls[0][0]).toBe('/api/scene')
    expect(bodyOf(0)).toEqual({ action: 'config', session_id: 'sess-1' })
  })

  it('addRadar POST /api/radars', async () => {
    await addRadar('sess-1', radar)
    expect(fetchMock.mock.calls[0][0]).toBe('/api/radars')
    expect(bodyOf(0)).toEqual({ session_id: 'sess-1', radar })
  })

  it('updateRadar 带 action=update', async () => {
    await updateRadar('sess-1', radar)
    expect(fetchMock.mock.calls[0][0]).toBe('/api/radar')
    expect(bodyOf(0)).toEqual({ action: 'update', session_id: 'sess-1', radar })
  })

  it('deleteRadar 用 radar_id', async () => {
    await deleteRadar('sess-1', 7)
    expect(bodyOf(0)).toEqual({ action: 'del', session_id: 'sess-1', radar_id: 7 })
  })

  it('addJammer POST /api/jammers 且字段名为 jammer', async () => {
    const jammer: Jammer = { id: 1, x: 6000, y: 6000, Pj_dBm: 40, Gj_dB: 20, jam_freq: 1e10, jam_type: 'NOISE_JAM' }
    await addJammer('sess-1', jammer)
    expect(fetchMock.mock.calls[0][0]).toBe('/api/jammers')
    expect(bodyOf(0)).toEqual({ session_id: 'sess-1', jammer })
  })

  it('simulate POST /api/simulate', async () => {
    await simulate('sess-1')
    expect(fetchMock.mock.calls[0][0]).toBe('/api/simulate')
    expect(bodyOf(0)).toEqual({ session_id: 'sess-1' })
  })
})
