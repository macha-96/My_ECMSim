import { beforeEach, describe, expect, it, vi } from 'vitest'
import { flushPromises, mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import LoginView from './LoginView.vue'
import * as api from '../api/auth'

const pushMock = vi.fn()

vi.mock('../api/auth', () => ({
  register: vi.fn(), login: vi.fn(), logout: vi.fn(), status: vi.fn(),
}))
vi.mock('vue-router', () => ({
  useRouter: () => ({ push: pushMock }),
}))

async function fillAndSubmit(w: ReturnType<typeof mount>, username: string, password: string): Promise<void> {
  const inputs = w.findAll('input')
  await inputs[0].setValue(username)
  await inputs[1].setValue(password)
  await w.find('button.submit-btn').trigger('click')
  await flushPromises()
}

describe('LoginView', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    document.body.innerHTML = ''
  })

  it('登录成功后跳转到 /', async () => {
    vi.mocked(api.login).mockResolvedValue({ success: true, session_id: 'sess-auth' })
    const w = mount(LoginView, { attachTo: document.body })
    await fillAndSubmit(w, 'alice', 'secret123')
    expect(api.login).toHaveBeenCalledWith('alice', 'secret123')
    expect(pushMock).toHaveBeenCalledWith('/')
    w.unmount()
  })

  it('登录失败时在表单旁展示后端错误文案且不跳转', async () => {
    vi.mocked(api.login).mockRejectedValue(new Error('invalid username or password'))
    const w = mount(LoginView, { attachTo: document.body })
    await fillAndSubmit(w, 'alice', 'wrong-pass')
    expect(w.find('.form-error').text()).toContain('invalid username or password')
    expect(pushMock).not.toHaveBeenCalled()
    w.unmount()
  })

  it('切换到注册模式后提交调用 register 并在成功后跳转 /', async () => {
    vi.mocked(api.register).mockResolvedValue({ success: true, user_id: 7 })
    vi.mocked(api.login).mockResolvedValue({ success: true, session_id: 'sess-auth' })
    const w = mount(LoginView, { attachTo: document.body })
    await w.find('button.mode-switch').trigger('click')
    await fillAndSubmit(w, 'alice', 'secret123')
    expect(api.register).toHaveBeenCalledWith('alice', 'secret123')
    expect(pushMock).toHaveBeenCalledWith('/')
    w.unmount()
  })

  it('注册失败（用户名已存在）时展示后端文案且不跳转', async () => {
    vi.mocked(api.register).mockRejectedValue(new Error('username already exists'))
    const w = mount(LoginView, { attachTo: document.body })
    await w.find('button.mode-switch').trigger('click')
    await fillAndSubmit(w, 'alice', 'secret123')
    expect(w.find('.form-error').text()).toContain('username already exists')
    expect(pushMock).not.toHaveBeenCalled()
    w.unmount()
  })

  it('失败后重试成功时清除旧错误提示', async () => {
    vi.mocked(api.login).mockRejectedValueOnce(new Error('invalid username or password'))
    vi.mocked(api.login).mockResolvedValueOnce({ success: true, session_id: 'sess-auth' })
    const w = mount(LoginView, { attachTo: document.body })
    await fillAndSubmit(w, 'alice', 'wrong-pass')
    expect(w.find('.form-error').exists()).toBe(true)
    await fillAndSubmit(w, 'alice', 'secret123')
    expect(w.find('.form-error').exists()).toBe(false)
    expect(pushMock).toHaveBeenCalledWith('/')
    w.unmount()
  })

  it('提交进行中重复点击不重复发请求', async () => {
    let resolveLogin: (() => void) | null = null
    vi.mocked(api.login).mockImplementation(() => new Promise(res => {
      resolveLogin = () => res({ success: true, session_id: 'sess-auth' })
    }))
    const w = mount(LoginView, { attachTo: document.body })
    const inputs = w.findAll('input')
    await inputs[0].setValue('alice')
    await inputs[1].setValue('secret123')
    await w.find('button.submit-btn').trigger('click')
    await w.find('button.submit-btn').trigger('click')
    await flushPromises()
    expect(api.login).toHaveBeenCalledTimes(1)
    ;(resolveLogin as (() => void) | null)?.()
    await flushPromises()
    w.unmount()
  })
})
