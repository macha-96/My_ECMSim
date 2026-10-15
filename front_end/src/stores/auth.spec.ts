import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { createPinia, setActivePinia } from 'pinia'
import * as api from '../api/auth'
import { useAuthStore } from './auth'

vi.mock('../api/auth', () => ({
  register: vi.fn(), login: vi.fn(), logout: vi.fn(), status: vi.fn(),
}))

describe('auth store', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    localStorage.clear()
    vi.mocked(api.status).mockResolvedValue({ success: true, authenticated: false })
  })
  afterEach(() => vi.unstubAllGlobals())

  it('init 认证有效时置 loggedIn 并记录 userId', async () => {
    vi.mocked(api.status).mockResolvedValue({ success: true, authenticated: true, user_id: 7 })
    const s = useAuthStore()
    await s.init()
    expect(s.loggedIn).toBe(true)
    expect(s.userId).toBe(7)
  })

  it('init 未登录（无 Cookie/已过期）时保持未登录', async () => {
    const s = useAuthStore()
    await s.init()
    expect(s.loggedIn).toBe(false)
  })

  it('init 后端不可达时不抛错且视为未登录（登录页仍可显示）', async () => {
    vi.mocked(api.status).mockRejectedValue(new ApiErrorStub())
    const s = useAuthStore()
    await expect(s.init()).resolves.toBeUndefined()
    expect(s.loggedIn).toBe(false)
  })

  it('login 成功后 loggedIn=true 且记住用户名', async () => {
    vi.mocked(api.login).mockResolvedValue({ success: true, session_id: 'sess-auth' })
    const s = useAuthStore()
    await s.login('alice', 'secret123')
    expect(s.loggedIn).toBe(true)
    expect(s.username).toBe('alice')
    expect(api.login).toHaveBeenCalledWith('alice', 'secret123')
  })

  it('login 密码错误时抛错且保持未登录', async () => {
    vi.mocked(api.login).mockRejectedValue(new Error('invalid username or password'))
    const s = useAuthStore()
    await expect(s.login('alice', 'wrong')).rejects.toThrow('invalid username or password')
    expect(s.loggedIn).toBe(false)
  })

  it('register 成功后自动登录并记录 userId', async () => {
    vi.mocked(api.register).mockResolvedValue({ success: true, user_id: 7 })
    vi.mocked(api.login).mockResolvedValue({ success: true, session_id: 'sess-auth' })
    const s = useAuthStore()
    await s.register('alice', 'secret123')
    expect(api.login).toHaveBeenCalledWith('alice', 'secret123')
    expect(s.loggedIn).toBe(true)
    expect(s.userId).toBe(7)
    expect(s.username).toBe('alice')
  })

  it('logout 后回到未登录并清空用户名', async () => {
    vi.mocked(api.login).mockResolvedValue({ success: true, session_id: 'sess-auth' })
    const s = useAuthStore()
    await s.login('alice', 'secret123')
    await s.logout()
    expect(s.loggedIn).toBe(false)
    expect(s.username).toBe('')
    expect(s.userId).toBeNull()
    expect(api.logout).toHaveBeenCalledTimes(1)
  })

  it('logout 后端失败也重置本地状态', async () => {
    vi.mocked(api.login).mockResolvedValue({ success: true, session_id: 'sess-auth' })
    vi.mocked(api.logout).mockRejectedValue(new Error('网络请求失败'))
    const s = useAuthStore()
    await s.login('alice', 'secret123')
    await expect(s.logout()).resolves.toBeUndefined()
    expect(s.loggedIn).toBe(false)
  })

  it('刷新后 init 从本地存储恢复用户名（无需重新登录）', async () => {
    vi.mocked(api.login).mockResolvedValue({ success: true, session_id: 'sess-auth' })
    vi.mocked(api.status).mockResolvedValue({ success: true, authenticated: true, user_id: 7 })
    const first = useAuthStore()
    await first.login('alice', 'secret123')
    // 模拟刷新：新 store 实例，仅 Cookie 仍然有效
    setActivePinia(createPinia())
    const refreshed = useAuthStore()
    await refreshed.init()
    expect(refreshed.loggedIn).toBe(true)
    expect(refreshed.username).toBe('alice')
  })

  it('sessionExpired（401）同步置未登录，且保留用户名便于重新登录', async () => {
    vi.mocked(api.login).mockResolvedValue({ success: true, session_id: 'sess-auth' })
    const s = useAuthStore()
    await s.login('alice', 'secret123')
    s.sessionExpired()
    expect(s.loggedIn).toBe(false)
    expect(s.userId).toBeNull()
    expect(s.username).toBe('alice')
  })
})

/** 模拟后端不可达（真实场景为 ApiError 网络失败） */
class ApiErrorStub extends Error {
  constructor() {
    super('网络请求失败')
    this.name = 'ApiError'
  }
}
