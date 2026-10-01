import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { createWebSocketController } from './useWebSocket'

class FakeSocket {
  static instances: FakeSocket[] = []
  onopen: (() => void) | null = null
  onmessage: ((ev: { data: string }) => void) | null = null
  onclose: (() => void) | null = null
  onerror: (() => void) | null = null
  closed = false
  url: string

  constructor(url: string) {
    this.url = url
    FakeSocket.instances.push(this)
  }

  close(): void {
    this.closed = true
    this.onclose?.()
  }
}

describe('useWebSocket', () => {
  beforeEach(() => {
    FakeSocket.instances = []
    vi.useFakeTimers()
  })
  afterEach(() => {
    vi.useRealTimers()
  })

  function makeCtrl() {
    const messages: string[] = []
    const ctrl = createWebSocketController({
      onMessage: d => messages.push(d),
      createSocket: url => new FakeSocket(url) as unknown as WebSocket,
      retryDelayMs: () => 10,
    })
    return { ctrl, messages }
  }

  it('connect 构造正确 URL 且初始 connecting', () => {
    const { ctrl } = makeCtrl()
    ctrl.connect('sess-1')
    expect(FakeSocket.instances).toHaveLength(1)
    expect(FakeSocket.instances[0].url).toContain('/ws?session_id=sess-1')
    expect(ctrl.status.value).toBe('connecting')
  })

  it('onopen → connected，消息转发给 onMessage', () => {
    const { ctrl, messages } = makeCtrl()
    ctrl.connect('sess-1')
    FakeSocket.instances[0].onopen?.()
    expect(ctrl.status.value).toBe('connected')
    FakeSocket.instances[0].onmessage?.({ data: '{"radars":[]}' })
    expect(messages).toEqual(['{"radars":[]}'])
  })

  it('断开 → reconnecting，按退避创建新 socket', () => {
    const { ctrl } = makeCtrl()
    ctrl.connect('sess-1')
    FakeSocket.instances[0].onopen?.()
    FakeSocket.instances[0].onclose?.()
    expect(ctrl.status.value).toBe('reconnecting')
    expect(FakeSocket.instances).toHaveLength(1)
    vi.advanceTimersByTime(10)
    expect(FakeSocket.instances).toHaveLength(2)
    expect(ctrl.status.value).toBe('connecting')
  })

  it('连续失败递增尝试（retryDelayMs 收到递增 attempt）', () => {
    const delays: number[] = []
    const ctrl = createWebSocketController({
      onMessage: () => undefined,
      createSocket: url => new FakeSocket(url) as unknown as WebSocket,
      retryDelayMs: a => { delays.push(a); return 10 },
    })
    ctrl.connect('s')
    FakeSocket.instances[0].onclose?.() // attempt 0
    vi.advanceTimersByTime(10)
    FakeSocket.instances[1].onclose?.() // attempt 1
    vi.advanceTimersByTime(10)
    expect(delays).toEqual([0, 1])
  })

  it('disconnect 停止重连并置 disconnected', () => {
    const { ctrl } = makeCtrl()
    ctrl.connect('sess-1')
    ctrl.disconnect()
    expect(ctrl.status.value).toBe('disconnected')
    vi.advanceTimersByTime(10000)
    expect(FakeSocket.instances).toHaveLength(1) // 无新连接
  })

  it('connect 覆盖旧会话：旧 socket 关闭不触发重连', () => {
    const { ctrl } = makeCtrl()
    ctrl.connect('sess-1')
    const first = FakeSocket.instances[0]
    first.onopen?.()
    ctrl.connect('sess-2')
    expect(first.closed).toBe(true)
    expect(FakeSocket.instances).toHaveLength(2)
    expect(FakeSocket.instances[1].url).toContain('session_id=sess-2')
    vi.advanceTimersByTime(1000)
    expect(FakeSocket.instances).toHaveLength(2) // 旧 socket 未引发重连
  })
})
