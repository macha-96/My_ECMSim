import { afterEach, describe, expect, it, vi } from 'vitest'
import { ApiError, postJson, setUnauthorizedHandler } from './http'

function stubFetch(impl: unknown) {
  const fn = vi.fn().mockImplementation(impl as () => Promise<unknown>)
  vi.stubGlobal('fetch', fn)
  return fn
}

/** 取回 Promise 拒绝值并断言为 ApiError（无拒绝时抛错让测试失败） */
async function catchApiError(p: Promise<unknown>): Promise<ApiError> {
  try {
    await p
  } catch (err) {
    return err as ApiError
  }
  throw new Error('expected rejection')
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

describe('ApiError status（会话过期判定契约）', () => {
  afterEach(() => vi.unstubAllGlobals())

  it('HTTP 401 时 ApiError.status 为 401', async () => {
    stubFetch(async () => ({ ok: false, status: 401, json: async () => ({}) }))
    const e = await catchApiError(postJson('/api/scene', {}))
    expect(e).toBeInstanceOf(ApiError)
    expect(e.status).toBe(401)
  })

  it('HTTP 500 时 ApiError.status 为 500', async () => {
    stubFetch(async () => ({ ok: false, status: 500, json: async () => ({}) }))
    const e = await catchApiError(postJson('/api/simulate', {}))
    expect(e.status).toBe(500)
  })

  it('业务 success:false（HTTP 200）不带 status，避免误判会话过期', async () => {
    stubFetch(async () => ({ ok: true, status: 200, json: async () => ({ success: false, error: 'invalid username or password' }) }))
    const e = await catchApiError(postJson('/auth/login', {}))
    expect(e).toBeInstanceOf(ApiError)
    expect(e.status).toBeUndefined()
  })
})

describe('setUnauthorizedHandler（401 全局跳转契约）', () => {
  afterEach(() => {
    vi.unstubAllGlobals()
    setUnauthorizedHandler(null)
  })

  it('HTTP 401 时触发未授权回调（且仍抛 ApiError）', async () => {
    const handler = vi.fn()
    setUnauthorizedHandler(handler)
    stubFetch(async () => ({ ok: false, status: 401, json: async () => ({}) }))
    await expect(postJson('/api/scene', {})).rejects.toThrow('HTTP 401')
    expect(handler).toHaveBeenCalledTimes(1)
  })

  it('HTTP 500 不触发未授权回调', async () => {
    const handler = vi.fn()
    setUnauthorizedHandler(handler)
    stubFetch(async () => ({ ok: false, status: 500, json: async () => ({}) }))
    await expect(postJson('/api/simulate', {})).rejects.toThrow('HTTP 500')
    expect(handler).not.toHaveBeenCalled()
  })

  it('业务 success:false（HTTP 200）不触发未授权回调', async () => {
    const handler = vi.fn()
    setUnauthorizedHandler(handler)
    stubFetch(async () => ({ ok: true, status: 200, json: async () => ({ success: false, error: 'x' }) }))
    await expect(postJson('/auth/login', {})).rejects.toThrow('x')
    expect(handler).not.toHaveBeenCalled()
  })
})
