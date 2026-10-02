import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount, type VueWrapper } from '@vue/test-utils'
import SplitBar from './SplitBar.vue'

function down(el: Element, x: number, y: number): void {
  el.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: x, clientY: y }))
}
function move(el: Element, x: number, y: number): void {
  el.dispatchEvent(new MouseEvent('pointermove', { bubbles: true, clientX: x, clientY: y }))
}
function up(el: Element, x: number, y: number): void {
  el.dispatchEvent(new MouseEvent('pointerup', { bubbles: true, clientX: x, clientY: y }))
}
// dispatchEvent 不等待 Vue 响应式刷新，断言前统一 flush
async function tick(w: VueWrapper): Promise<void> {
  await w.vm.$nextTick()
}

describe('SplitBar horizontal, fixed a', () => {
  beforeEach(() => {
    localStorage.clear()
    vi.spyOn(HTMLElement.prototype, 'clientWidth', 'get').mockReturnValue(1000)
    vi.spyOn(HTMLElement.prototype, 'clientHeight', 'get').mockReturnValue(600)
    vi.stubGlobal('ResizeObserver', class {
      observe() {}
      unobserve() {}
      disconnect() {}
    })
  })

  function mountBar(extra: Record<string, unknown> = {}) {
    return mount(SplitBar, {
      props: { direction: 'horizontal', minA: 100, minB: 100, storageKey: 'ecmsim.testSplit', defaultValue: 300, ...extra },
      slots: { a: '<div class="pa">A</div>', b: '<div class="pb">B</div>' },
      attachTo: document.body,
    })
  }

  it('首绘 fixed 窗格为默认 px', () => {
    const w = mountBar()
    expect((w.find('.pane-a').element as HTMLElement).style.flex).toContain('300px')
    w.unmount()
  })

  it('拖拽改变尺寸并持久化', async () => {
    const w = mountBar()
    const handle = w.find('.handle')
    down(handle.element, 500, 0)
    move(handle.element, 600, 0)
    await tick(w)
    expect((w.find('.pane-a').element as HTMLElement).style.flex).toContain('400px')
    up(handle.element, 600, 0)
    expect(localStorage.getItem('ecmsim.testSplit')).toBe('400')
    w.unmount()
  })

  it('钳制到最小值', async () => {
    const w = mountBar()
    const handle = w.find('.handle')
    down(handle.element, 500, 0)
    move(handle.element, -5000, 0)
    await tick(w)
    expect((w.find('.pane-a').element as HTMLElement).style.flex).toContain('100px')
    w.unmount()
  })

  it('恢复记忆值', async () => {
    localStorage.setItem('ecmsim.testSplit', '450')
    const w = mountBar()
    await tick(w)
    expect((w.find('.pane-a').element as HTMLElement).style.flex).toContain('450px')
    w.unmount()
  })

  it('fixed b 时反向增减', async () => {
    const w = mountBar({ fixedPane: 'b' })
    const handle = w.find('.handle')
    down(handle.element, 500, 0)
    move(handle.element, 600, 0) // 右拖 → b 缩小
    await tick(w)
    expect((w.find('.pane-b').element as HTMLElement).style.flex).toContain('200px')
    w.unmount()
  })
})

describe('SplitBar vertical', () => {
  beforeEach(() => {
    localStorage.clear()
    vi.spyOn(HTMLElement.prototype, 'clientWidth', 'get').mockReturnValue(1000)
    vi.spyOn(HTMLElement.prototype, 'clientHeight', 'get').mockReturnValue(600)
    vi.stubGlobal('ResizeObserver', class {
      observe() {}
      unobserve() {}
      disconnect() {}
    })
  })

  it('上下拖拽改变高度', async () => {
    const w = mount(SplitBar, {
      props: { direction: 'vertical', minA: 50, minB: 50, storageKey: 'ecmsim.testV', defaultValue: 200 },
      slots: { a: '<div>A</div>', b: '<div>B</div>' },
      attachTo: document.body,
    })
    const handle = w.find('.handle')
    handle.element.dispatchEvent(new MouseEvent('pointerdown', { bubbles: true, clientX: 0, clientY: 300 }))
    handle.element.dispatchEvent(new MouseEvent('pointermove', { bubbles: true, clientX: 0, clientY: 200 }))
    await w.vm.$nextTick()
    expect((w.find('.pane-a').element as HTMLElement).style.flex).toContain('100px')
    w.unmount()
  })
})
