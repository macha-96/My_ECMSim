<script setup lang="ts">
import { ref } from 'vue'
import { useRouter } from 'vue-router'
// 子路径导入（与 EntityCard 同惯例）：测试环境 vi.mock 截获 element-plus 根入口不受影响
import ElButton from 'element-plus/es/components/button/index'
import ElInput from 'element-plus/es/components/input/index'
import { useAuthStore } from '../stores/auth'
import { errText } from '../api/http'

const router = useRouter()
const auth = useAuthStore()

const mode = ref<'login' | 'register'>('login')
const username = ref('')
const password = ref('')
const errorMsg = ref('')
const busy = ref(false)

function switchMode(): void {
  mode.value = mode.value === 'login' ? 'register' : 'login'
  errorMsg.value = ''
}

async function onSubmit(): Promise<void> {
  if (busy.value) return // 提交进行中重复点击防护
  errorMsg.value = ''
  busy.value = true
  try {
    if (mode.value === 'login') await auth.login(username.value, password.value)
    else await auth.register(username.value, password.value)
    await router.push('/')
  } catch (e) {
    errorMsg.value = errText(e)
  } finally {
    busy.value = false
  }
}
</script>

<template>
  <div class="login-page">
    <div class="login-card glass-panel">
      <div class="brand">
        <span class="logo">ECM</span>
        <h1>电子对抗仿真平台</h1>
        <p class="sub">{{ mode === 'login' ? '登录后开始仿真' : '注册新账号' }}</p>
      </div>
      <div class="fields">
        <ElInput
          v-model="username"
          placeholder="用户名"
          autocomplete="username"
        />
        <ElInput
          v-model="password"
          type="password"
          placeholder="密码"
          show-password
          autocomplete="current-password"
          @keyup.enter="onSubmit"
        />
      </div>
      <p v-if="errorMsg" class="form-error" role="alert">{{ errorMsg }}</p>
      <ElButton
        class="submit-btn"
        type="primary"
        :loading="busy"
        @click="onSubmit"
      >
        {{ mode === 'login' ? '登 录' : '注册并登录' }}
      </ElButton>
      <div class="switch-row">
        <span>{{ mode === 'login' ? '还没有账号？' : '已有账号？' }}</span>
        <button type="button" class="mode-switch" @click="switchMode">
          {{ mode === 'login' ? '去注册' : '去登录' }}
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.login-page {
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
}
.login-card {
  width: 360px;
  padding: 32px 28px 24px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}
.brand {
  text-align: center;
  margin-bottom: 6px;
}
.brand .logo {
  display: inline-block;
  font-family: var(--font-mono);
  font-weight: 700;
  font-size: 22px;
  letter-spacing: 2px;
  color: var(--accent);
}
.brand h1 {
  font-size: 17px;
  margin: 6px 0 2px;
  font-weight: 600;
}
.brand .sub {
  font-size: 12px;
  color: var(--text-2);
  margin: 0;
}
.fields {
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.form-error {
  margin: 0;
  font-size: 12px;
  color: var(--danger);
}
.submit-btn {
  width: 100%;
}
.switch-row {
  text-align: center;
  font-size: 12px;
  color: var(--text-2);
}
.mode-switch {
  background: none;
  border: none;
  color: var(--cyan);
  font-size: 12px;
  cursor: pointer;
  padding: 0;
}
.mode-switch:hover {
  text-decoration: underline;
}
</style>
