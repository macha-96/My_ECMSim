import { ref, type Ref } from 'vue'

export type WsStatus = 'connecting' | 'connected' | 'reconnecting' | 'disconnected'

export interface WsControllerOptions {
  onMessage: (data: string) => void
  onStatus?: (s: WsStatus) => void
  /** 测试注入；默认使用全局 WebSocket */
  createSocket?: (url: string) => WebSocket
  /** 测试注入；默认指数退避 1s→2s→…→30s 封顶 */
  retryDelayMs?: (attempt: number) => number
}

export interface WsController {
  connect(sid: string): void
  disconnect(): void
  readonly status: Ref<WsStatus>
}

function defaultUrl(sid: string): string {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws'
  return `${proto}://${location.host}/ws?session_id=${sid}`
}

export function createWebSocketController(opts: WsControllerOptions): WsController {
  const createSocket = opts.createSocket ?? ((url: string) => new WebSocket(url))
  const retryDelayMs = opts.retryDelayMs ?? ((a: number) => Math.min(30000, 1000 * 2 ** a))

  const status = ref<WsStatus>('disconnected')
  let ws: WebSocket | null = null
  let sessionId: string | null = null
  let attempt = 0
  let closedByUser = false
  let retryTimer: ReturnType<typeof setTimeout> | null = null

  function setStatus(s: WsStatus): void {
    status.value = s
    opts.onStatus?.(s)
  }

  function open(): void {
    if (!sessionId) return
    // 退避等待期由 onclose 置为 reconnecting；实际发起连接尝试时统一置为 connecting
    setStatus('connecting')
    let sock: WebSocket
    try {
      sock = createSocket(defaultUrl(sessionId))
    } catch {
      scheduleReconnect()
      return
    }
    ws = sock
    sock.onopen = () => {
      attempt = 0
      setStatus('connected')
    }
    sock.onmessage = (ev: MessageEvent) => opts.onMessage(String(ev.data))
    sock.onclose = () => {
      if (closedByUser) return
      setStatus('reconnecting')
      scheduleReconnect()
    }
    sock.onerror = () => {
      sock.close()
    }
  }

  function scheduleReconnect(): void {
    const delay = retryDelayMs(attempt)
    attempt += 1
    retryTimer = setTimeout(open, delay)
  }

  function connect(sid: string): void {
    sessionId = sid
    attempt = 0
    closedByUser = false
    if (retryTimer) {
      clearTimeout(retryTimer)
      retryTimer = null
    }
    if (ws) {
      ws.onclose = null // 旧 socket 关闭不触发重连
      ws.close()
      ws = null
    }
    open()
  }

  function disconnect(): void {
    closedByUser = true
    if (retryTimer) {
      clearTimeout(retryTimer)
      retryTimer = null
    }
    if (ws) {
      ws.onclose = null
      ws.close()
      ws = null
    }
    setStatus('disconnected')
  }

  return { connect, disconnect, status }
}
