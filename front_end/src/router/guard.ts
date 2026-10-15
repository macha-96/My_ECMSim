import type { RouteLocationRaw } from 'vue-router'
import { useAuthStore } from '../stores/auth'

/** 路由守卫：未登录只能进 /login；已登录访问 /login 时送回主界面 */
export function authGuard(to: { path: string }): RouteLocationRaw | true {
  const auth = useAuthStore()
  if (to.path === '/login') {
    return auth.loggedIn ? { path: '/' } : true
  }
  return auth.loggedIn ? true : { path: '/login' }
}
