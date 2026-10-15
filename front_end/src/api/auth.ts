// 认证 HTTP 封装 — URL 与 body 字段与 C++ /auth/* 路由严格一致
import type { AuthStatusRes, LoginRes, LogoutRes, RegisterRes } from '../types/api'
import { getJson, postJson } from './http'

export function register(username: string, password: string): Promise<RegisterRes> {
  return postJson<RegisterRes>('/auth/register', { username, password })
}

export function login(username: string, password: string): Promise<LoginRes> {
  return postJson<LoginRes>('/auth/login', { username, password })
}

export function logout(): Promise<LogoutRes> {
  return postJson<LogoutRes>('/auth/logout', {})
}

export function status(): Promise<AuthStatusRes> {
  return getJson<AuthStatusRes>('/auth/status')
}
