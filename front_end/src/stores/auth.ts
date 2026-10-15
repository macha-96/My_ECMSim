import { ref } from 'vue'
import { defineStore } from 'pinia'
import * as api from '../api/auth'

/** 登录用户名的本地缓存（仅展示用，凭据是 HttpOnly Cookie，JS 不可见） */
const USERNAME_KEY = 'ecmsim.username'

export const useAuthStore = defineStore('auth', () => {
  const loggedIn = ref(false)
  const username = ref('')
  const userId = ref<number | null>(null)

  function reset(): void {
    loggedIn.value = false
    username.value = ''
    userId.value = null
  }

  /** 启动时同步登录状态（GET /auth/status）；后端不可达也视为未登录，保证登录页可显示 */
  async function init(): Promise<void> {
    try {
      const r = await api.status()
      if (r.authenticated) {
        loggedIn.value = true
        userId.value = r.user_id ?? null
        username.value = localStorage.getItem(USERNAME_KEY) ?? (r.user_id != null ? `用户${r.user_id}` : '')
      } else {
        reset()
      }
    } catch {
      reset()
    }
  }

  /** 登录：成功置 loggedIn；失败（后端文案如密码错误）原样抛给调用方展示 */
  async function login(name: string, password: string): Promise<void> {
    await api.login(name, password)
    loggedIn.value = true
    username.value = name
    try {
      localStorage.setItem(USERNAME_KEY, name)
    } catch {
      /* 本地存储不可用时仅影响刷新后的用户名展示 */
    }
  }

  /** 注册：成功后自动登录（后端注册接口不下发 Cookie，需再调 login） */
  async function register(name: string, password: string): Promise<void> {
    const r = await api.register(name, password)
    userId.value = r.user_id ?? null
    await login(name, password)
  }

  /** 登出：后端失败也重置本地状态（是否真正登出以服务端 Cookie 清除为准） */
  async function logout(): Promise<void> {
    try {
      await api.logout()
    } catch {
      /* 后端不可达不阻塞本地登出 */
    }
    reset()
    try {
      localStorage.removeItem(USERNAME_KEY)
    } catch {
      /* 忽略 */
    }
  }

  /** 会话过期（接口返回 401）：同步置未登录，避免路由守卫把用户弹回主界面造成跳转环；保留用户名便于重新登录 */
  function sessionExpired(): void {
    loggedIn.value = false
    userId.value = null
  }

  return { loggedIn, username, userId, init, login, register, logout, sessionExpired }
})
