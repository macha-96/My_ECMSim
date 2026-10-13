// 场景/实体/仿真的 HTTP 封装 — URL 与 body 字段与 C++ 路由严格一致
import type {
  CreateSessionRes, EntityRes, Jammer, Radar, SceneConfigRes, SceneListRes, SimulateRes,
} from '../types/api'
import { getJson, postJson } from './http'

export function createSession(): Promise<CreateSessionRes> {
  return postJson<CreateSessionRes>('/api/scene', {})
}

export function fetchSceneConfig(sessionId: string): Promise<SceneConfigRes> {
  return postJson<SceneConfigRes>('/api/scene', { action: 'config', session_id: sessionId })
}

export function deleteScene(sessionId: string): Promise<EntityRes> {
  return postJson<EntityRes>('/api/scene', { action: 'delete', session_id: sessionId })
}

export function addRadar(sessionId: string, radar: Radar): Promise<EntityRes> {
  return postJson<EntityRes>('/api/radars', { session_id: sessionId, radar })
}

export function updateRadar(sessionId: string, radar: Radar): Promise<EntityRes> {
  return postJson<EntityRes>('/api/radar', { action: 'update', session_id: sessionId, radar })
}

export function deleteRadar(sessionId: string, radarId: number): Promise<EntityRes> {
  return postJson<EntityRes>('/api/radar', { action: 'del', session_id: sessionId, radar_id: radarId })
}

export function addJammer(sessionId: string, jammer: Jammer): Promise<EntityRes> {
  return postJson<EntityRes>('/api/jammers', { session_id: sessionId, jammer })
}

export function updateJammer(sessionId: string, jammer: Jammer): Promise<EntityRes> {
  return postJson<EntityRes>('/api/jammer', { action: 'update', session_id: sessionId, jammer })
}

export function deleteJammer(sessionId: string, jammerId: number): Promise<EntityRes> {
  return postJson<EntityRes>('/api/jammer', { action: 'del', session_id: sessionId, jammer_id: jammerId })
}

export function simulate(sessionId: string): Promise<SimulateRes> {
  return postJson<SimulateRes>('/api/simulate', { session_id: sessionId })
}

export function listScenes(): Promise<SceneListRes> {
  return getJson<SceneListRes>('/api/scene/list')
}
