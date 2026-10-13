import { describe, expect, it, vi, beforeEach } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import AppHeader from './AppHeader.vue'

const listMock = vi.fn()

vi.mock('../api/scene', () => ({
  listScenes: (...args: unknown[]) => listMock(...args),
}))

vi.mock('element-plus', () => ({
  ElMessage: { error: vi.fn(), success: vi.fn() },
}))

describe('AppHeader', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    listMock.mockResolvedValue({
      success: true,
      sessions: [
        { session_id: 'sess-abc', last_access: Math.floor(Date.now() / 1000) - 60, radar_count: 2, jammer_count: 1, ws_connections: 1 },
        { session_id: 'sess-xyz', last_access: Math.floor(Date.now() / 1000) - 300, radar_count: 1, jammer_count: 0, ws_connections: 0 },
      ],
    })
  })

  function mountHeader(props: { wsStatus: 'connected' | 'connecting' | 'reconnecting' | 'disconnected'; sessionId: string | null }) {
    return mount(AppHeader, { props, attachTo: document.body })
  }

  it('connected 显示已连接徽章', () => {
    const w = mountHeader({ wsStatus: 'connected', sessionId: 's1' })
    expect(w.text()).toContain('已连接')
    w.unmount()
  })

  it('reconnecting 显示重连中', () => {
    const w = mountHeader({ wsStatus: 'reconnecting', sessionId: null })
    expect(w.text()).toContain('重连中')
    w.unmount()
  })

  it('显示会话 ID', () => {
    const w = mountHeader({ wsStatus: 'connected', sessionId: 'sess-abc' })
    expect(w.text()).toContain('sess-abc')
    w.unmount()
  })

  it('无会话时显示 —', () => {
    const w = mountHeader({ wsStatus: 'disconnected', sessionId: null })
    expect(w.text()).toContain('—')
    w.unmount()
  })

  it('pending>0 显示待同步计数', async () => {
    const w = mountHeader({ wsStatus: 'connected', sessionId: 's1' })
    const { useSceneStore } = await import('../stores/scene')
    const scene = useSceneStore()
    scene.radars.push({ id: 1, x: 0, y: 0, Pt_dBm: 1, G_dB: 1, freq: 1, bandwidth: 1, sigma: 1, thresh_db: 1 })
    scene.updateField('radar', 1, 'x', 2)
    await w.vm.$nextTick()
    expect(w.text()).toContain('待同步 1')
    w.unmount()
  })

  it('悬停会话 badge 拉取列表并显示下拉', async () => {
    const w = mountHeader({ wsStatus: 'connected', sessionId: 'sess-abc' })
    await w.find('.session-wrapper').trigger('mouseenter')
    await w.vm.$nextTick()
    await new Promise(r => setTimeout(r, 0))
    expect(listMock).toHaveBeenCalledTimes(1)
    const dd = document.querySelector('.popover-dropdown')
    expect(dd).not.toBeNull()
    expect(dd!.textContent).toContain('sess-abc')
    expect(dd!.textContent).toContain('sess-xyz')
    w.unmount()
  })

  it('下拉中当前会话带徽标', async () => {
    const w = mountHeader({ wsStatus: 'connected', sessionId: 'sess-abc' })
    await w.find('.session-wrapper').trigger('mouseenter')
    await w.vm.$nextTick()
    await new Promise(r => setTimeout(r, 0))
    const items = document.querySelectorAll('.popover-item')
    expect((items[0] as HTMLElement).classList.contains('current')).toBe(true)
    expect(items[0].textContent).toContain('当前')
    w.unmount()
  })

  it('点击其他会话触发切换', async () => {
    const w = mountHeader({ wsStatus: 'connected', sessionId: 'sess-abc' })
    await w.find('.session-wrapper').trigger('mouseenter')
    await w.vm.$nextTick()
    await new Promise(r => setTimeout(r, 0))
    const { useSceneStore } = await import('../stores/scene')
    const scene = useSceneStore()
    const spy = vi.spyOn(scene, 'fetchScene').mockResolvedValue()
    const items = document.querySelectorAll('.popover-item')
    ;(items[1] as HTMLElement).click()
    await w.vm.$nextTick()
    expect(scene.sessionId).toBe('sess-xyz')
    expect(spy).toHaveBeenCalled()
    w.unmount()
  })

  it('拉取失败弹错误提示', async () => {
    listMock.mockRejectedValue(new Error('down'))
    const w = mountHeader({ wsStatus: 'connected', sessionId: 's1' })
    await w.find('.session-wrapper').trigger('mouseenter')
    await w.vm.$nextTick()
    await new Promise(r => setTimeout(r, 0))
    const { ElMessage } = await import('element-plus')
    expect(vi.mocked(ElMessage.error)).toHaveBeenCalledWith('down')
    w.unmount()
  })
})
