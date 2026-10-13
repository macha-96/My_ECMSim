// 统一 HTTP 出口：网络错误 / HTTP 非 2xx / 业务 success:false 都转成 ApiError
export class ApiError extends Error {
  readonly detail?: unknown

  constructor(message: string, detail?: unknown) {
    super(message)
    this.name = 'ApiError'
    this.detail = detail
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
    throw new ApiError(`HTTP ${resp.status}`, resp.status)
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
    throw new ApiError(`HTTP ${resp.status}`, resp.status)
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
