import { afterEach, describe, expect, it, vi } from 'vitest'
import { ApiError, postJson } from './http'

function stubFetch(impl: unknown) {
  const fn = vi.fn().mockImplementation(impl as () => Promise<unknown>)
  vi.stubGlobal('fetch', fn)
  return fn
}

describe('postJson', () => {
  afterEach(() => vi.unstubAllGlobals())

  it('成功时返回解析后的 JSON', async () => {
    stubFetch(async () => ({ ok: true, json: async () => ({ success: true, session_id: 's1' }) }))
    const r = await postJson<{ success: boolean; session_id: string }>('/api/scene', {})
    expect(r.session_id).toBe('s1')
  })

  it('以 JSON POST 发送请求体并带 Content-Type', async () => {
    const fn = stubFetch(async () => ({ ok: true, json: async () => ({ success: true }) }))
    await postJson('/api/radar', { action: 'del', radar_id: 3 })
    const [url, init] = fn.mock.calls[0] as [string, RequestInit]
    expect(url).toBe('/api/radar')
    expect(init.method).toBe('POST')
    expect(init.headers).toEqual({ 'Content-Type': 'application/json' })
    expect(JSON.parse(String(init.body))).toEqual({ action: 'del', radar_id: 3 })
  })

  it('success:false 时抛 ApiError 并携带后端 error 文案', async () => {
    stubFetch(async () => ({ ok: true, json: async () => ({ success: false, error: 'bad session' }) }))
    await expect(postJson('/api/scene', {})).rejects.toThrow('bad session')
  })

  it('HTTP 非 2xx 时抛 ApiError', async () => {
    stubFetch(async () => ({ ok: false, status: 500, json: async () => ({}) }))
    await expect(postJson('/api/simulate', {})).rejects.toThrow('HTTP 500')
  })

  it('网络异常时抛 ApiError(网络请求失败)', async () => {
    stubFetch(async () => {
      throw new TypeError('fetch failed')
    })
    await expect(postJson('/api/scene', {})).rejects.toThrow('网络请求失败')
  })

  it('ApiError 是 Error 子类且带 name', () => {
    const e = new ApiError('x')
    expect(e).toBeInstanceOf(Error)
    expect(e.name).toBe('ApiError')
  })
})
