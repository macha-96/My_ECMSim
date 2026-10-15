import { createApp } from 'vue'
import { createPinia } from 'pinia'
import ElementPlus, { ElMessage } from 'element-plus'
import 'element-plus/dist/index.css'
import 'element-plus/theme-chalk/dark/css-vars.css'
import 'element-plus/es/components/message/style/css'
import App from './App.vue'
import router from './router'
import { useAuthStore } from './stores/auth'
import { ApiError, setUnauthorizedHandler } from './api/http'
import './style.css'

const app = createApp(App)
const pinia = createPinia()
app.use(pinia)
app.use(ElementPlus)
app.use(router)
document.documentElement.classList.add('dark')

app.config.errorHandler = (err, _instance, info) => {
  console.error('[app error]', err, info)
}

// 规范 §6.1：未处理的 Promise 拒绝全局提示，避免静默失败
window.addEventListener('unhandledrejection', (ev: PromiseRejectionEvent) => {
  console.error('[unhandledrejection]', ev.reason)
  // 401 已由未授权回调统一提示，这里跳过避免重复弹窗
  if (ev.reason instanceof ApiError && ev.reason.status === 401) return
  try {
    ElMessage.error(String(ev.reason ?? '未知异步错误'))
  } catch {
    /* 忽略提示自身失败 */
  }
})

async function bootstrap(): Promise<void> {
  const auth = useAuthStore(pinia)
  // 挂载前先同步登录态（Cookie 有效则直接进主界面），保证首个路由守卫拿到真实状态
  await auth.init()

  // 全局 401 处理：同步重置登录态（防守卫跳转环）+ 提示 + 回登录页
  setUnauthorizedHandler(() => {
    auth.sessionExpired()
    ElMessage.error('登录已过期，请重新登录')
    void router.replace('/login')
  })

  app.mount('#app')
}

void bootstrap()
