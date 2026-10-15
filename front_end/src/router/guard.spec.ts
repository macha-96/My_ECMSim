import { beforeEach, describe, expect, it } from 'vitest'
import { createPinia, setActivePinia } from 'pinia'
import { authGuard } from './guard'
import { useAuthStore } from '../stores/auth'

describe('router authGuard（登录态路由决策）', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
  })

  it('未登录访问 / 时重定向到 /login', () => {
    expect(authGuard({ path: '/' })).toEqual({ path: '/login' })
  })

  it('已登录访问 / 时放行', () => {
    const auth = useAuthStore()
    auth.loggedIn = true
    expect(authGuard({ path: '/' })).toBe(true)
  })

  it('未登录访问 /login 时放行（登录页本身可达）', () => {
    expect(authGuard({ path: '/login' })).toBe(true)
  })

  it('已登录访问 /login 时重定向回 /（避免已登录还看到登录页）', () => {
    const auth = useAuthStore()
    auth.loggedIn = true
    expect(authGuard({ path: '/login' })).toEqual({ path: '/' })
  })
})
