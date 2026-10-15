import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { login, logout, register, status } from './auth'

describe('auth api', () => {
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

  it('register POST /auth/register 携带 username/password', async () => {
    await register('alice', 'secret123')
    expect(fetchMock.mock.calls[0][0]).toBe('/auth/register')
    expect(bodyOf(0)).toEqual({ username: 'alice', password: 'secret123' })
  })

  it('login POST /auth/login 携带 username/password', async () => {
    await login('alice', 'secret123')
    expect(fetchMock.mock.calls[0][0]).toBe('/auth/login')
    expect(bodyOf(0)).toEqual({ username: 'alice', password: 'secret123' })
  })

  it('logout POST /auth/logout', async () => {
    await logout()
    expect(fetchMock.mock.calls[0][0]).toBe('/auth/logout')
    expect(fetchMock.mock.calls[0][1].method).toBe('POST')
  })

  it('status GET /auth/status 返回 authenticated 状态', async () => {
    fetchMock.mockResolvedValue({ ok: true, json: async () => ({ success: true, authenticated: true, user_id: 7 }) })
    const r = await status()
    expect(fetchMock.mock.calls[0][0]).toBe('/auth/status')
    expect(fetchMock.mock.calls[0][1].method).toBe('GET')
    expect(r).toEqual({ success: true, authenticated: true, user_id: 7 })
  })

  it('login 密码错误时抛出后端 error 文案', async () => {
    fetchMock.mockResolvedValue({ ok: true, json: async () => ({ success: false, error: 'invalid username or password' }) })
    await expect(login('alice', 'wrong-pass')).rejects.toThrow('invalid username or password')
  })
})
