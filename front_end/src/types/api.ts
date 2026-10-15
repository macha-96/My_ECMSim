// ECMSim 后端 API 的共享类型（与 C++ 服务端 JSON 协议一一对应）

export type EntityKind = 'radar' | 'jammer'

export interface Radar {
  id: number
  x: number
  y: number
  Pt_dBm: number
  G_dB: number
  freq: number // Hz
  bandwidth: number // Hz
  sigma: number // m^2
  thresh_db: number
}

export interface Jammer {
  id: number
  x: number
  y: number
  Pj_dBm: number
  Gj_dB: number
  jam_freq: number // Hz
  jam_type: 'NOISE_JAM' | 'RANGE_DECEPT'
}

export interface JammerDetail {
  jammer_id: number
  freq_delta_Hz: number
  freq_match_factor: number
  original_power_W: number
  effective_power_W: number
}

export interface SimResult {
  radar_id: number
  SINR_dB: number
  jsr_dB: number
  detect_success: boolean
  total_effective_jam_power_W?: number
  jam_success_score?: number
  is_deception_active?: boolean
  decept_effect_score?: number
  jammer_details?: JammerDetail[]
}

/** 一次仿真的历史步（供 ECharts 折线） */
export interface HistStep {
  labels: number[] // radar_id 列表
  sinr: number[] // 与 labels 对齐的 SINR(dB)
  zeta: number[] // 与 labels 对齐的频域匹配因子
}

export interface CreateSessionRes {
  success: boolean
  session_id?: string
  error?: string
}

export interface SceneConfigRes {
  success: boolean
  radars?: Radar[]
  jammers?: Jammer[]
  error?: string
}

export interface EntityRes {
  success: boolean
  error?: string
}

export interface SimulateRes {
  success: boolean
  sim_results?: SimResult[]
  error?: string
}

export interface SceneListItem {
  session_id: string
  last_access: number
  radar_count: number
  jammer_count: number
  ws_connections: number
}

export interface SceneListRes {
  success: boolean
  sessions?: SceneListItem[]
  total?: number
  error?: string
}

export interface RegisterRes {
  success: boolean
  user_id?: number
  error?: string
}

export interface LoginRes {
  success: boolean
  session_id?: string
  error?: string
}

export interface LogoutRes {
  success: boolean
  error?: string
}

export interface AuthStatusRes {
  success: boolean
  authenticated: boolean
  user_id?: number
  error?: string
}
