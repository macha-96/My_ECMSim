import { createApp } from 'vue'
import { createPinia } from 'pinia'
import ElementPlus, { ElMessage } from 'element-plus'
import 'element-plus/dist/index.css'
import 'element-plus/theme-chalk/dark/css-vars.css'
import 'element-plus/es/components/message/style/css'
import App from './App.vue'
import './style.css'

const app = createApp(App)
app.use(createPinia())
app.use(ElementPlus)
document.documentElement.classList.add('dark')

app.config.errorHandler = (err, _instance, info) => {
  console.error('[app error]', err, info)
}

// 规范 §6.1：未处理的 Promise 拒绝全局提示，避免静默失败
window.addEventListener('unhandledrejection', (ev: PromiseRejectionEvent) => {
  console.error('[unhandledrejection]', ev.reason)
  try {
    ElMessage.error(String(ev.reason ?? '未知异步错误'))
  } catch {
    /* 忽略提示自身失败 */
  }
})

app.mount('#app')
