import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import BottomDrawer from './BottomDrawer.vue'

describe('BottomDrawer（双栏）', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
    vi.spyOn(HTMLElement.prototype, 'clientWidth', 'get').mockReturnValue(1200)
    vi.spyOn(HTMLElement.prototype, 'clientHeight', 'get').mockReturnValue(600)
    vi.stubGlobal('ResizeObserver', class {
      observe() {}
      unobserve() {}
      disconnect() {}
    })
  })

  function mountDrawer() {
    return mount(BottomDrawer, {
      slots: { left: '<div>结果内容</div>', right: '<div>图表内容</div>' },
      attachTo: document.body,
    })
  }

  it('左右两栏同时渲染', () => {
    const w = mountDrawer()
    expect(w.text()).toContain('结果内容')
    expect(w.text()).toContain('图表内容')
    w.unmount()
  })

  it('无标签页切换器', () => {
    const w = mountDrawer()
    expect(w.find('.tabbar').exists()).toBe(false)
    w.unmount()
  })

  it('高度拖拽持久化到 localStorage', async () => {
    const w = mountDrawer()
    const handle = w.find('.drawer > .handle')
    handle.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientY: 500 }))
    handle.element.dispatchEvent(new MouseEvent('pointermove', { bubbles: true, clientY: 300 }))
    handle.element.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientY: 300 }))
    await w.vm.$nextTick()
    expect(Number(localStorage.getItem('ecmsim.drawerH') ?? '0')).toBeGreaterThan(260)
    w.unmount()
  })

  it('再次挂载恢复已存高度', async () => {
    localStorage.setItem('ecmsim.drawerH', '400')
    const w = mountDrawer()
    await w.vm.$nextTick()
    expect((w.find('.drawer').element as HTMLElement).style.height).toBe('400px')
    w.unmount()
  })
})
