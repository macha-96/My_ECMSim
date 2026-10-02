import { describe, expect, it } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import AppHeader from './AppHeader.vue'

describe('AppHeader', () => {
  function mountHeader(props: { wsStatus: 'connected' | 'connecting' | 'reconnecting' | 'disconnected'; sessionId: string | null }) {
    setActivePinia(createPinia())
    return mount(AppHeader, { props })
  }

  it('connected 显示已连接徽章', () => {
    const w = mountHeader({ wsStatus: 'connected', sessionId: 's1' })
    expect(w.text()).toContain('已连接')
  })

  it('reconnecting 显示重连中', () => {
    const w = mountHeader({ wsStatus: 'reconnecting', sessionId: null })
    expect(w.text()).toContain('重连中')
  })

  it('显示会话 ID', () => {
    const w = mountHeader({ wsStatus: 'connected', sessionId: 'sess-abc' })
    expect(w.text()).toContain('sess-abc')
  })

  it('无会话时显示 —', () => {
    const w = mountHeader({ wsStatus: 'disconnected', sessionId: null })
    expect(w.text()).toContain('—')
  })

  it('pending>0 显示待同步计数', async () => {
    const w = mountHeader({ wsStatus: 'connected', sessionId: 's1' })
    const { useSceneStore } = await import('../stores/scene')
    const scene = useSceneStore()
    scene.radars.push({ id: 1, x: 0, y: 0, Pt_dBm: 1, G_dB: 1, freq: 1, bandwidth: 1, sigma: 1, thresh_db: 1 })
    scene.updateField('radar', 1, 'x', 2)
    await w.vm.$nextTick()
    expect(w.text()).toContain('待同步 1')
  })
})
