import { beforeEach, describe, expect, it } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import BottomDrawer from './BottomDrawer.vue'

const TABS = [
  { key: 'result', label: '仿真结果' },
  { key: 'charts', label: '历史图表' },
]

describe('BottomDrawer', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    localStorage.clear()
  })

  function mountDrawer() {
    return mount(BottomDrawer, {
      props: { tabs: TABS, modelValue: 'result' },
      slots: { result: '<div>结果内容</div>', charts: '<div>图表内容</div>' },
    })
  }

  it('渲染标签与当前 tab 内容', () => {
    const w = mountDrawer()
    expect(w.text()).toContain('仿真结果')
    expect(w.text()).toContain('历史图表')
    expect(w.text()).toContain('结果内容')
    expect(w.text()).not.toContain('图表内容')
  })

  it('点击标签触发 update:modelValue', async () => {
    const w = mountDrawer()
    const chartTab = w.findAll('button').find(b => b.text().includes('历史图表'))
    await chartTab?.trigger('click')
    expect(w.emitted('update:modelValue')?.[0]).toEqual(['charts'])
  })

  it('切换 tab 后展示对应插槽', async () => {
    const w = mountDrawer()
    await w.setProps({ modelValue: 'charts' })
    expect(w.text()).toContain('图表内容')
    expect(w.text()).not.toContain('结果内容')
  })

  it('高度持久化到 localStorage', async () => {
    const w = mountDrawer()
    // 把手 pointer 拖拽向上 → 高度增大
    // VTU 对 PointerEvent 的 options 二次赋值会抛错（clientY 为继承的只读访问器），
    // 故按 sanctioned fallback 用 MouseEvent + Object.assign 派发
    const el = w.find('.handle').element
    const fire = (type: string, clientY: number): void => {
      const ev = new MouseEvent(type, { clientY, bubbles: true, cancelable: true })
      Object.assign(ev, { pointerId: 1 })
      el.dispatchEvent(ev)
    }
    fire('pointerdown', 500)
    fire('pointermove', 300)
    fire('pointerup', 300)
    const saved = Number(localStorage.getItem('ecmsim.drawerH') ?? '0')
    expect(saved).toBeGreaterThan(260)
  })

  it('再次挂载恢复已存高度', async () => {
    localStorage.setItem('ecmsim.drawerH', '400')
    const w = mountDrawer()
    expect((w.find('.drawer').element as HTMLElement).style.height).toBe('400px')
  })
})
