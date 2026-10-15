// 统一 HTTP 出口：网络错误 / HTTP 非 2xx / 业务 success:false 都转成 ApiError
export class ApiError extends Error {
  readonly detail?: unknown
  /** HTTP 状态码：仅 HTTP 非 2xx 时携带；业务 success:false（HTTP 200）不带，供调用方区分会话过期 */
  readonly status?: number

  constructor(message: string, detail?: unknown, status?: number) {
    super(message)
    this.name = 'ApiError'
    this.detail = detail
    this.status = status
  }
}

type UnauthorizedHandler = () => void
let unauthorizedHandler: UnauthorizedHandler | null = null

/** 注册全局未授权（401）处理：main.ts 接入「提示登录过期 + 重置登录态 + 跳 /login」；传 null 注销 */
export function setUnauthorizedHandler(fn: UnauthorizedHandler | null): void {
  unauthorizedHandler = fn
}

function notifyUnauthorized(): void {
  if (!unauthorizedHandler) return
  try {
    unauthorizedHandler()
  } catch {
    /* 回调自身异常不阻塞错误抛出 */
  }
}

export function errText(e: unknown): string {
  return e instanceof Error ? e.message : String(e)
}

export async function postJson<T>(url: string, body: unknown): Promise<T> {
  let resp: Response
  try {
    resp = await fetch(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    })
  } catch (e) {
    throw new ApiError('网络请求失败', e)
  }
  if (!resp.ok) {
    if (resp.status === 401) notifyUnauthorized()
    throw new ApiError(`HTTP ${resp.status}`, resp.status, resp.status)
  }
  let json: unknown
  try {
    json = await resp.json()
  } catch (e) {
    throw new ApiError('响应解析失败', e)
  }
  const obj = json as { success?: boolean; error?: string }
  if (obj && obj.success === false) {
    throw new ApiError(obj.error || '请求失败', json)
  }
  return json as T
}

/** GET 出口：与 postJson 相同的错误语义（网络/HTTP/业务失败 → ApiError） */
export async function getJson<T>(url: string): Promise<T> {
  let resp: Response
  try {
    resp = await fetch(url, { method: 'GET' })
  } catch (e) {
    throw new ApiError('网络请求失败', e)
  }
  if (!resp.ok) {
    if (resp.status === 401) notifyUnauthorized()
    throw new ApiError(`HTTP ${resp.status}`, resp.status, resp.status)
  }
  let json: unknown
  try {
    json = await resp.json()
  } catch (e) {
    throw new ApiError('响应解析失败', e)
  }
  const obj = json as { success?: boolean; error?: string }
  if (obj && obj.success === false) {
    throw new ApiError(obj.error || '请求失败', json)
  }
  return json as T
}
