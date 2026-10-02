import { createApp } from 'vue'
import { createPinia } from 'pinia'
import ElementPlus from 'element-plus'
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

app.mount('#app')
