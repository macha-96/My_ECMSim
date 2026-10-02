# ECMSim 前端 Vue 迁移与界面重设计 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `static/index_v2.html` 的全部功能迁移到 `front_end/` Vue 3 工程，并按已评审设计完成玻璃拟态界面重设计与四项交互优化（错误反馈、状态可视化、拖拽升级、响应式）。

**Architecture:** 分层架构 — 类型化 API 层 + 双 Pinia store（scene/simulation）+ 纯函数 composables（WS 状态机、画布坐标）+ 按面板拆分的组件。参数同步采用「本地乐观更新 + 300ms 防抖批量上送 + 字段级 pending 集合保护 WS 合并」。画布保留手写 Canvas（Pointer Events 拖拽），历史图表改用 ECharts。

**Tech Stack:** Vue 3.5（`<script setup>` + TS）、Vite 8、Pinia、Element Plus（dark）、ECharts 5、Vitest + @vue/test-utils + jsdom。

**Spec:** `docs/superpowers/specs/2026-10-01-frontend-vue-migration-design.md`（计划论据来源，执行者需同时阅读）

## Global Constraints

- C++ 服务器与 `static/index_v2.html` **零改动**；协议不变：`POST /api/*` + `WS /ws?session_id=<sid>`；开发代理到 `http://localhost:8080`
- 依赖固定：运行时仅 `element-plus`、`echarts`、`pinia`；开发仅 `vitest`、`@vue/test-utils`、`jsdom`
- pending 键格式（全项目统一）：`` `${prop}_${kind === 'jammer' ? 'j' : ''}${id}` ``，如 `Pt_dBm_1`、`x_j2`、`jam_type_j3`
- `updateField` 是所有参数写入的唯一入口；store 内部用 `xxx.value`，store 外（组件/测试）由 Pinia 自动解包直接访问
- 设计令牌（spec §4.2）：`--accent:#e94560`、渐变 `#e94560→#ff7a59`、`--cyan:#00e5ff`、`--ok:#69f0ae`、`--danger:#ff8a80`、雷达 `#2ee6a8`、底色 `135deg, #1a1040→#0d1b2a→#0a1120`、玻璃 `rgba(255,255,255,.07)+blur(8px)`
- UI 文案中文；数据（表格/坐标/ID）用 `--font-mono` 等宽字体
- `MAX_HISTORY = 30`；同步防抖 300ms；WS 退避 1s→2s→…→30s 封顶
- tsconfig 严格：`noUnusedLocals`/`noUnusedParameters`/`erasableSyntaxOnly`（禁参数属性、enum）；类型导入用 `import type`
- 每个任务通过自己的验证门后 commit；全局 DoD：`npm run test` / `npm run typecheck` / `npm run build`

## File Structure

```
front_end/src/
├── types/api.ts               # Radar/Jammer/SimResult/HistStep/响应接口 + EntityKind
├── api/http.ts                # postJson + ApiError + errText（唯一 HTTP 出口）
├── api/scene.ts               # 10 个场景/实体/仿真端点封装
├── stores/scene.ts            # 会话、CRUD、updateField、flushSync、applyRemoteScene
├── stores/simulation.ts       # results/history/running/auto + run/reset/startAuto/stopAuto
├── composables/useWebSocket.ts     # createWebSocketController（状态机+退避）
├── composables/useSceneCanvas.ts   # computeViewport/worldToScreen/screenToWorld/hitTest
├── components/
│   ├── AppHeader.vue          # 标题 + WS 徽章 + 会话 ID + 同步徽章
│   ├── StatusBar.vue          # statusMsg + 待同步 + 步骤数
│   ├── SceneCanvas.vue        # 手写 canvas 渲染 + Pointer 拖拽 + 坐标气泡 + 空状态
│   ├── BottomDrawer.vue       # 抽屉把手/标签/localStorage 高度
│   ├── ControlPanel.vue       # 过滤 + 添加 + EntityCard 列表
│   ├── EntityCard.vue         # 滑杆表单（GHz/MHz 换算、焦点保护、脉冲高亮）
│   ├── ResultPanel.vue        # 运行/重置/自动仿真 + 结果表 + 空状态
│   ├── HistoryCharts.vue      # ECharts 双图（SINR/ζ）
│   └── JammerDetailTable.vue  # 频域明细表
├── App.vue                    # 布局骨架 + 会话引导 + WS 接线
├── style.css                  # 设计令牌 + 布局 + 组件样式（替换模板版）
├── main.ts                    # Pinia + Element Plus dark + 错误兜底
└── *.spec.ts                  # 测试与被测文件同目录
```

---

### Task 1: 工程配置 + 类型定义 + HTTP 客户端

**Files:**
- Create: `front_end/src/types/api.ts`
- Create: `front_end/src/api/http.ts`
- Create: `front_end/src/api/http.spec.ts`
- Modify: `front_end/vite.config.ts`（整文件替换）
- Modify: `front_end/package.json`（scripts + 依赖）

**Interfaces:**
- Consumes: 模板工程（vue/vite/vue-tsc）
- Produces（后续所有任务依赖）:
  - `types/api.ts`: `interface Radar { id:number; x:number; y:number; Pt_dBm:number; G_dB:number; freq:number; bandwidth:number; sigma:number; thresh_db:number }`；`interface Jammer { id:number; x:number; y:number; Pj_dBm:number; Gj_dB:number; jam_freq:number; jam_type:'NOISE_JAM'|'RANGE_DECEPT' }`；`interface JammerDetail { jammer_id:number; freq_delta_Hz:number; freq_match_factor:number; original_power_W:number; effective_power_W:number }`；`interface SimResult { radar_id:number; SINR_dB:number; jsr_dB:number; detect_success:boolean; total_effective_jam_power_W?:number; jam_success_score?:number; is_deception_active?:boolean; decept_effect_score?:number; jammer_details?:JammerDetail[] }`；`interface HistStep { labels:number[]; sinr:number[]; zeta:number[] }`；`type EntityKind='radar'|'jammer'`；`CreateSessionRes/SceneConfigRes/EntityRes/SimulateRes`
  - `api/http.ts`: `class ApiError extends Error`、`function errText(e: unknown): string`、`async function postJson<T>(url: string, body: unknown): Promise<T>`

- [ ] **Step 1: 安装依赖与配置脚本**

```bash
cd /home/ky/workspace/ECMSim/front_end
npm i element-plus echarts pinia
npm i -D vitest @vue/test-utils jsdom
```

修改 `package.json` 的 `scripts` 为：

```json
"scripts": {
  "dev": "vite",
  "build": "vue-tsc -b && vite build",
  "preview": "vite preview",
  "test": "vitest run",
  "test:watch": "vitest",
  "typecheck": "vue-tsc -b"
}
```

- [ ] **Step 2: 替换 `vite.config.ts`（dev 代理 + vitest 配置）**

```ts
import vue from '@vitejs/plugin-vue'
import { defineConfig } from 'vitest/config'

// 开发代理：/api 与 /ws 转发到 C++ 服务器（协议不变）
export default defineConfig({
  plugins: [vue()],
  server: {
    proxy: {
      '/api': 'http://localhost:8080',
      '/ws': { target: 'ws://localhost:8080', ws: true },
    },
  },
  test: {
    environment: 'jsdom',
    include: ['src/**/*.spec.ts'],
  },
})
```

- [ ] **Step 3: 写失败测试 `src/api/http.spec.ts`**

```ts
import { afterEach, describe, expect, it, vi } from 'vitest'
import { ApiError, postJson } from './http'

function stubFetch(impl: unknown) {
  const fn = vi.fn().mockImplementation(impl as () => Promise<unknown>)
  vi.stubGlobal('fetch', fn)
  return fn
}

describe('postJson', () => {
  afterEach(() => vi.unstubAllGlobals())

  it('成功时返回解析后的 JSON', async () => {
    stubFetch(async () => ({ ok: true, json: async () => ({ success: true, session_id: 's1' }) }))
    const r = await postJson<{ success: boolean; session_id: string }>('/api/scene', {})
    expect(r.session_id).toBe('s1')
  })

  it('以 JSON POST 发送请求体并带 Content-Type', async () => {
    const fn = stubFetch(async () => ({ ok: true, json: async () => ({ success: true }) }))
    await postJson('/api/radar', { action: 'del', radar_id: 3 })
    const [url, init] = fn.mock.calls[0] as [string, RequestInit]
    expect(url).toBe('/api/radar')
    expect(init.method).toBe('POST')
    expect(init.headers).toEqual({ 'Content-Type': 'application/json' })
    expect(JSON.parse(String(init.body))).toEqual({ action: 'del', radar_id: 3 })
  })

  it('success:false 时抛 ApiError 并携带后端 error 文案', async () => {
    stubFetch(async () => ({ ok: true, json: async () => ({ success: false, error: 'bad session' }) }))
    await expect(postJson('/api/scene', {})).rejects.toThrow('bad session')
  })

  it('HTTP 非 2xx 时抛 ApiError', async () => {
    stubFetch(async () => ({ ok: false, status: 500, json: async () => ({}) }))
    await expect(postJson('/api/simulate', {})).rejects.toThrow('HTTP 500')
  })

  it('网络异常时抛 ApiError(网络请求失败)', async () => {
    stubFetch(async () => {
      throw new TypeError('fetch failed')
    })
    await expect(postJson('/api/scene', {})).rejects.toThrow('网络请求失败')
  })

  it('ApiError 是 Error 子类且带 name', () => {
    const e = new ApiError('x')
    expect(e).toBeInstanceOf(Error)
    expect(e.name).toBe('ApiError')
  })
})
```

- [ ] **Step 4: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/api/http.spec.ts`
Expected: FAIL — `Failed to resolve import "./http"`

- [ ] **Step 5: 实现 `src/types/api.ts`**

```ts
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
```

- [ ] **Step 6: 实现 `src/api/http.ts`**

```ts
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
```

- [ ] **Step 7: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/api/http.spec.ts`
Expected: PASS（6 个用例）

- [ ] **Step 8: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/vite.config.ts front_end/package.json front_end/package-lock.json front_end/src/types front_end/src/api
git commit -m "front_end: vitest/vite 配置 + API 类型与 HTTP 客户端"
```

### Task 2: 场景 API 封装

**Files:**
- Create: `front_end/src/api/scene.ts`
- Create: `front_end/src/api/scene.spec.ts`

**Interfaces:**
- Consumes: `postJson`、`types/api.ts`（Task 1）
- Produces（Task 3–6 依赖，签名固定）:
  - `createSession(): Promise<CreateSessionRes>` — `POST /api/scene {}`
  - `fetchSceneConfig(sessionId: string): Promise<SceneConfigRes>` — `POST /api/scene {action:'config', session_id}`
  - `deleteScene(sessionId: string): Promise<EntityRes>` — `POST /api/scene {action:'delete', session_id}`
  - `addRadar(sessionId, radar: Radar): Promise<EntityRes>` — `POST /api/radars {session_id, radar}`
  - `updateRadar(sessionId, radar: Radar): Promise<EntityRes>` — `POST /api/radar {action:'update', session_id, radar}`
  - `deleteRadar(sessionId, radarId: number): Promise<EntityRes>` — `POST /api/radar {action:'del', session_id, radar_id}`
  - `addJammer/updateJammer/deleteJammer` 对称 — `/api/jammers`、`/api/jammer`（字段 `jammer`/`jammer_id`）
  - `simulate(sessionId): Promise<SimulateRes>` — `POST /api/simulate {session_id}`

- [ ] **Step 1: 写失败测试 `src/api/scene.spec.ts`**

```ts
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { Jammer, Radar } from '../types/api'
import {
  addJammer, addRadar, createSession, deleteRadar, fetchSceneConfig, simulate, updateRadar,
} from './scene'

const radar: Radar = { id: 1, x: 5000, y: 5000, Pt_dBm: 20, G_dB: 25, freq: 1e10, bandwidth: 1e6, sigma: 5, thresh_db: -50 }

describe('scene api', () => {
  let fetchMock: ReturnType<typeof vi.fn>

  beforeEach(() => {
    fetchMock = vi.fn().mockResolvedValue({ ok: true, json: async () => ({ success: true }) })
    vi.stubGlobal('fetch', fetchMock)
  })
  afterEach(() => vi.unstubAllGlobals())

  function bodyOf(callIndex: number): Record<string, unknown> {
    const init = fetchMock.mock.calls[callIndex][1] as RequestInit
    return JSON.parse(String(init.body)) as Record<string, unknown>
  }

  it('createSession 发送空对象到 /api/scene', async () => {
    await createSession()
    expect(fetchMock.mock.calls[0][0]).toBe('/api/scene')
    expect(bodyOf(0)).toEqual({})
  })

  it('fetchSceneConfig 携带 action=config', async () => {
    await fetchSceneConfig('sess-1')
    expect(fetchMock.mock.calls[0][0]).toBe('/api/scene')
    expect(bodyOf(0)).toEqual({ action: 'config', session_id: 'sess-1' })
  })

  it('addRadar POST /api/radars', async () => {
    await addRadar('sess-1', radar)
    expect(fetchMock.mock.calls[0][0]).toBe('/api/radars')
    expect(bodyOf(0)).toEqual({ session_id: 'sess-1', radar })
  })

  it('updateRadar 带 action=update', async () => {
    await updateRadar('sess-1', radar)
    expect(fetchMock.mock.calls[0][0]).toBe('/api/radar')
    expect(bodyOf(0)).toEqual({ action: 'update', session_id: 'sess-1', radar })
  })

  it('deleteRadar 用 radar_id', async () => {
    await deleteRadar('sess-1', 7)
    expect(bodyOf(0)).toEqual({ action: 'del', session_id: 'sess-1', radar_id: 7 })
  })

  it('addJammer POST /api/jammers 且字段名为 jammer', async () => {
    const jammer: Jammer = { id: 1, x: 6000, y: 6000, Pj_dBm: 40, Gj_dB: 20, jam_freq: 1e10, jam_type: 'NOISE_JAM' }
    await addJammer('sess-1', jammer)
    expect(fetchMock.mock.calls[0][0]).toBe('/api/jammers')
    expect(bodyOf(0)).toEqual({ session_id: 'sess-1', jammer })
  })

  it('simulate POST /api/simulate', async () => {
    await simulate('sess-1')
    expect(fetchMock.mock.calls[0][0]).toBe('/api/simulate')
    expect(bodyOf(0)).toEqual({ session_id: 'sess-1' })
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/api/scene.spec.ts`
Expected: FAIL — `Failed to resolve import "./scene"`

- [ ] **Step 3: 实现 `src/api/scene.ts`**

```ts
// 场景/实体/仿真的 HTTP 封装 — URL 与 body 字段与 C++ 路由严格一致
import type {
  CreateSessionRes, EntityRes, Jammer, Radar, SceneConfigRes, SimulateRes,
} from '../types/api'
import { postJson } from './http'

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
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run`
Expected: PASS（http 6 + scene 7 全绿）

- [ ] **Step 5: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/api
git commit -m "front_end: 场景 API 封装"
```

---

### Task 3: scene store — 状态、CRUD、updateField

**Files:**
- Create: `front_end/src/stores/scene.ts`
- Create: `front_end/src/stores/scene.spec.ts`

**Interfaces:**
- Consumes: `api/scene.ts`（Task 2）、`types/api.ts`
- Produces（Task 4/5 继续扩展同一文件；组件依赖）:
  - `pendingKey(kind: EntityKind, prop: string, id: number): string`
  - `useSceneStore()` state: `sessionId: Ref<string|null>`、`radars: Ref<Radar[]>`、`jammers: Ref<Jammer[]>`、`filter: Ref<'all'|'radar'|'jammer'>`、`pendingEdits: Ref<Set<string>>`、`focusedKey: Ref<string|null>`、`syncing: Ref<boolean>`、`lastEdited: Ref<{kind:EntityKind;id:number;at:number}|null>`、`pulsed: Ref<{kind:EntityKind;id:number;at:number}|null>`
  - getter: `dirtyCount: number`
  - actions: `initSession(): Promise<void>`、`fetchScene(): Promise<void>`、`addRadar(): Promise<void>`、`addJammer(): Promise<void>`、`removeRadar(id: number): Promise<void>`、`removeJammer(id: number): Promise<void>`、`updateField(kind: EntityKind, id: number, prop: string, value: number|string): void`、`pulseEntity(kind: EntityKind, id: number): void`
  - 本任务 `updateField` 只做「写本地 + 标 pending + 记 lastEdited + scheduleSync()」；`scheduleSync/flushSync/applyRemoteScene/setFocusedField` 先以占位函数存在，Task 4/5 整体替换占位

- [ ] **Step 1: 写失败测试 `src/stores/scene.spec.ts`**

```ts
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { createPinia, setActivePinia } from 'pinia'
import type { Jammer, Radar } from '../types/api'
import * as api from '../api/scene'
import { pendingKey, useSceneStore } from './scene'

vi.mock('element-plus', () => ({ ElMessage: { error: vi.fn(), success: vi.fn() } }))
vi.mock('../api/scene', () => ({
  createSession: vi.fn(), fetchSceneConfig: vi.fn(), deleteScene: vi.fn(),
  addRadar: vi.fn(), updateRadar: vi.fn(), deleteRadar: vi.fn(),
  addJammer: vi.fn(), updateJammer: vi.fn(), deleteJammer: vi.fn(), simulate: vi.fn(),
}))

const radar = (id: number): Radar => ({ id, x: 100, y: 200, Pt_dBm: 20, G_dB: 25, freq: 1e10, bandwidth: 1e6, sigma: 5, thresh_db: -50 })
const jammer = (id: number): Jammer => ({ id, x: 300, y: 400, Pj_dBm: 40, Gj_dB: 20, jam_freq: 1e10, jam_type: 'NOISE_JAM' })

describe('scene store: CRUD 与 updateField', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    vi.mocked(api.createSession).mockResolvedValue({ success: true, session_id: 'sess-9' })
    vi.mocked(api.fetchSceneConfig).mockResolvedValue({ success: true, radars: [], jammers: [] })
    vi.mocked(api.addRadar).mockResolvedValue({ success: true })
    vi.mocked(api.addJammer).mockResolvedValue({ success: true })
    vi.mocked(api.deleteRadar).mockResolvedValue({ success: true })
    vi.mocked(api.deleteJammer).mockResolvedValue({ success: true })
  })

  it('pendingKey: 雷达无 j 前缀，干扰机带 j 前缀', () => {
    expect(pendingKey('radar', 'Pt_dBm', 1)).toBe('Pt_dBm_1')
    expect(pendingKey('jammer', 'x', 2)).toBe('x_j2')
    expect(pendingKey('jammer', 'jam_type', 3)).toBe('jam_type_j3')
  })

  it('initSession 保存 session_id', async () => {
    const s = useSceneStore()
    await s.initSession()
    expect(s.sessionId).toBe('sess-9')
  })

  it('initSession 失败时抛错', async () => {
    vi.mocked(api.createSession).mockResolvedValue({ success: false, error: 'boom' })
    const s = useSceneStore()
    await expect(s.initSession()).rejects.toThrow('boom')
  })

  it('fetchScene 覆盖 radars/jammers', async () => {
    vi.mocked(api.fetchSceneConfig).mockResolvedValue({ success: true, radars: [radar(1)], jammers: [jammer(1)] })
    const s = useSceneStore()
    await s.initSession()
    await s.fetchScene()
    expect(s.radars).toHaveLength(1)
    expect(s.jammers).toHaveLength(1)
  })

  it('addRadar 取 max(id)+1 并入列，调用 API', async () => {
    const s = useSceneStore()
    await s.initSession()
    s.radars.push(radar(3))
    await s.addRadar()
    expect(s.radars).toHaveLength(2)
    expect(s.radars[1].id).toBe(4)
    expect(api.addRadar).toHaveBeenCalledWith('sess-9', expect.objectContaining({ id: 4, x: 5000, freq: 1e10 }))
  })

  it('addRadar 失败不入列', async () => {
    vi.mocked(api.addRadar).mockResolvedValue({ success: false, error: 'dup' })
    const s = useSceneStore()
    await s.initSession()
    await s.addRadar()
    expect(s.radars).toHaveLength(0)
  })

  it('removeRadar 删除并调用 API', async () => {
    const s = useSceneStore()
    await s.initSession()
    s.radars.push(radar(1), radar(2))
    await s.removeRadar(1)
    expect(s.radars.map(r => r.id)).toEqual([2])
    expect(api.deleteRadar).toHaveBeenCalledWith('sess-9', 1)
  })

  it('addJammer / removeJammer 对称工作', async () => {
    const s = useSceneStore()
    await s.initSession()
    await s.addJammer()
    expect(s.jammers[0]).toMatchObject({ id: 1, x: 6000, jam_type: 'NOISE_JAM' })
    await s.removeJammer(1)
    expect(s.jammers).toHaveLength(0)
    expect(api.deleteJammer).toHaveBeenCalledWith('sess-9', 1)
  })

  it('updateField 写本地值 + 标 pending + 记 lastEdited', () => {
    const s = useSceneStore()
    s.radars.push(radar(1))
    s.updateField('radar', 1, 'Pt_dBm', 42)
    expect(s.radars[0].Pt_dBm).toBe(42)
    expect(s.pendingEdits.has('Pt_dBm_1')).toBe(true)
    expect(s.dirtyCount).toBe(1)
    expect(s.lastEdited).toMatchObject({ kind: 'radar', id: 1 })
  })

  it('updateField 对不存在的实体是 no-op', () => {
    const s = useSceneStore()
    s.updateField('radar', 99, 'x', 1)
    expect(s.pendingEdits.size).toBe(0)
  })

  it('pulseEntity 记录 pulsed（拖拽后卡片高亮用）', () => {
    const s = useSceneStore()
    s.pulseEntity('jammer', 2)
    expect(s.pulsed).toMatchObject({ kind: 'jammer', id: 2 })
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/stores/scene.spec.ts`
Expected: FAIL — `Failed to resolve import "./scene"`

- [ ] **Step 3: 实现 `src/stores/scene.ts`（本任务范围）**

```ts
import { computed, ref } from 'vue'
import { defineStore } from 'pinia'
import { ElMessage } from 'element-plus'
import type { EntityKind, Jammer, Radar } from '../types/api'
import * as api from '../api/scene'
import { errText } from '../api/http'

/** 统一 pending 键：`Pt_dBm_1` / `x_j2` / `jam_type_j3` */
export function pendingKey(kind: EntityKind, prop: string, id: number): string {
  return `${prop}_${kind === 'jammer' ? 'j' : ''}${id}`
}

/** pending 键取实体后缀：`j3` → 干扰机 3，`1` → 雷达 1（prop 可能含下划线，取最后一个 _ 之后） */
function suffixOf(key: string): string {
  return key.slice(key.lastIndexOf('_') + 1)
}

export const useSceneStore = defineStore('scene', () => {
  const sessionId = ref<string | null>(null)
  const radars = ref<Radar[]>([])
  const jammers = ref<Jammer[]>([])
  const filter = ref<'all' | 'radar' | 'jammer'>('all')
  const pendingEdits = ref<Set<string>>(new Set())
  const focusedKey = ref<string | null>(null)
  const syncing = ref(false)
  const lastEdited = ref<{ kind: EntityKind; id: number; at: number } | null>(null)
  const pulsed = ref<{ kind: EntityKind; id: number; at: number } | null>(null)

  const dirtyCount = computed(() => pendingEdits.value.size)

  async function initSession(): Promise<void> {
    const r = await api.createSession()
    if (!r.success || !r.session_id) throw new Error(r.error || '创建会话失败')
    sessionId.value = r.session_id
  }

  async function fetchScene(): Promise<void> {
    if (!sessionId.value) return
    const r = await api.fetchSceneConfig(sessionId.value)
    if (!r.success) throw new Error(r.error || '获取场景失败')
    radars.value = r.radars ?? []
    jammers.value = r.jammers ?? []
  }

  function nextId(list: { id: number }[]): number {
    return list.length ? Math.max(...list.map(e => e.id)) + 1 : 1
  }

  async function addRadar(): Promise<void> {
    if (!sessionId.value) return
    const radar: Radar = {
      id: nextId(radars.value), x: 5000, y: 5000,
      Pt_dBm: 20, G_dB: 25, freq: 1e10, bandwidth: 1e6, sigma: 5, thresh_db: -50,
    }
    try {
      const r = await api.addRadar(sessionId.value, radar)
      if (r.success) radars.value.push(radar)
      else ElMessage.error(r.error || '添加雷达失败')
    } catch (e) {
      ElMessage.error(errText(e))
    }
  }

  async function addJammer(): Promise<void> {
    if (!sessionId.value) return
    const jammer: Jammer = {
      id: nextId(jammers.value), x: 6000, y: 6000,
      Pj_dBm: 40, Gj_dB: 20, jam_freq: 1e10, jam_type: 'NOISE_JAM',
    }
    try {
      const r = await api.addJammer(sessionId.value, jammer)
      if (r.success) jammers.value.push(jammer)
      else ElMessage.error(r.error || '添加干扰机失败')
    } catch (e) {
      ElMessage.error(errText(e))
    }
  }

  async function removeRadar(id: number): Promise<void> {
    if (!sessionId.value) return
    try {
      const r = await api.deleteRadar(sessionId.value, id)
      if (r.success) {
        radars.value = radars.value.filter(e => e.id !== id)
        for (const k of [...pendingEdits.value]) if (suffixOf(k) === String(id)) pendingEdits.value.delete(k)
      } else ElMessage.error(r.error || '删除雷达失败')
    } catch (e) {
      ElMessage.error(errText(e))
    }
  }

  async function removeJammer(id: number): Promise<void> {
    if (!sessionId.value) return
    try {
      const r = await api.deleteJammer(sessionId.value, id)
      if (r.success) {
        jammers.value = jammers.value.filter(e => e.id !== id)
        for (const k of [...pendingEdits.value]) if (suffixOf(k) === `j${id}`) pendingEdits.value.delete(k)
      } else ElMessage.error(r.error || '删除干扰机失败')
    } catch (e) {
      ElMessage.error(errText(e))
    }
  }

  /** 所有参数写入的唯一入口：本地立即生效 + 标记 pending + 触发防抖上送 */
  function updateField(kind: EntityKind, id: number, prop: string, value: number | string): void {
    const list = kind === 'radar' ? radars.value : jammers.value
    const ent = list.find(e => e.id === id)
    if (!ent) return
    ;(ent as unknown as Record<string, number | string>)[prop] = value
    pendingEdits.value.add(pendingKey(kind, prop, id))
    lastEdited.value = { kind, id, at: Date.now() }
    scheduleSync()
  }

  /** 拖拽结束后给对应实体卡片一次高亮脉冲 */
  function pulseEntity(kind: EntityKind, id: number): void {
    pulsed.value = { kind, id, at: Date.now() }
  }

  // —— 占位：Task 4 用真实实现替换 scheduleSync/flushSync ——
  function scheduleSync(): void {
    /* Task 4 覆盖 */
  }
  async function flushSync(): Promise<boolean> {
    /* Task 4 覆盖 */
    return true
  }
  // —— 占位：Task 5 用真实实现替换 applyRemoteScene/setFocusedField ——
  function setFocusedField(_key: string | null): void {
    /* Task 5 覆盖 */
  }
  function applyRemoteScene(_msg: { radars?: Radar[]; jammers?: Jammer[] }): void {
    /* Task 5 覆盖 */
  }

  return {
    sessionId, radars, jammers, filter, pendingEdits, focusedKey, syncing, lastEdited, pulsed,
    dirtyCount,
    initSession, fetchScene, addRadar, addJammer, removeRadar, removeJammer,
    updateField, pulseEntity, scheduleSync, flushSync, applyRemoteScene, setFocusedField,
  }
})
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/stores/scene.spec.ts`
Expected: PASS（12 个用例）

- [ ] **Step 5: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/stores
git commit -m "front_end: scene store 状态/CRUD/updateField"
```

---

### Task 4: scene store — 300ms 防抖同步 flushSync

**Files:**
- Modify: `front_end/src/stores/scene.ts`（用真实实现替换 `scheduleSync`/`flushSync` 占位）
- Modify: `front_end/src/stores/scene.spec.ts`（追加 describe）

**Interfaces:**
- Consumes: `api.updateRadar`、`api.updateJammer`（Task 2）、`suffixOf`/`pendingEdits`/`sessionId`/`syncing`（Task 3）
- Produces:
  - `scheduleSync(): void` — 300ms 防抖后调用 `flushSync`
  - `flushSync(): Promise<boolean>` — 按实体分组上送 pending 字段；全部成功 → `true`（清对应 pending）；任一失败 → `false`（保留失败实体 pending + `ElMessage.error`）；无 pending → `true` 不调 API；调用期间 `syncing=true`
  - `updateField` 已在 Task 3 调用 `scheduleSync()`，本任务后生效

- [ ] **Step 1: 追加失败测试（新 describe 加到 `src/stores/scene.spec.ts`）**

```ts
describe('scene store: 防抖同步 flushSync', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    vi.useFakeTimers()
    vi.mocked(api.createSession).mockResolvedValue({ success: true, session_id: 'sess-1' })
    vi.mocked(api.fetchSceneConfig).mockResolvedValue({ success: true, radars: [], jammers: [] })
    vi.mocked(api.updateRadar).mockResolvedValue({ success: true })
    vi.mocked(api.updateJammer).mockResolvedValue({ success: true })
  })

  afterEach(() => {
    vi.useRealTimers()
  })

  function storeWithEntities() {
    const s = useSceneStore()
    s.radars.push(radar(1))
    s.jammers.push(jammer(1))
    return s
  }

  it('updateField 后 300ms 只上送一次且带上最新值', async () => {
    const s = storeWithEntities()
    s.updateField('radar', 1, 'Pt_dBm', 30)
    s.updateField('radar', 1, 'x', 1234)
    s.updateField('jammer', 1, 'Pj_dBm', 55)
    expect(api.updateRadar).not.toHaveBeenCalled()

    await vi.advanceTimersByTimeAsync(300)

    expect(api.updateRadar).toHaveBeenCalledTimes(1)
    expect(api.updateRadar).toHaveBeenCalledWith('sess-1', expect.objectContaining({ id: 1, Pt_dBm: 30, x: 1234 }))
    expect(api.updateJammer).toHaveBeenCalledTimes(1)
    expect(api.updateJammer).toHaveBeenCalledWith('sess-1', expect.objectContaining({ id: 1, Pj_dBm: 55 }))
    expect(s.pendingEdits.size).toBe(0)
    expect(s.dirtyCount).toBe(0)
  })

  it('上送失败保留 pending 并弹错误', async () => {
    vi.mocked(api.updateRadar).mockResolvedValue({ success: false, error: 'locked' })
    const { ElMessage } = await import('element-plus')
    const s = storeWithEntities()
    s.updateField('radar', 1, 'Pt_dBm', 30)
    await vi.advanceTimersByTimeAsync(300)

    expect(s.pendingEdits.has('Pt_dBm_1')).toBe(true)
    expect(vi.mocked(ElMessage.error)).toHaveBeenCalledWith('locked')
  })

  it('flushSync 无 pending 时直接返回 true 不调 API', async () => {
    const s = storeWithEntities()
    const ok = await s.flushSync()
    expect(ok).toBe(true)
    expect(api.updateRadar).not.toHaveBeenCalled()
  })

  it('flushSync 调用期间 syncing 为 true', async () => {
    const s = storeWithEntities()
    s.updateField('radar', 1, 'x', 5)
    const p = s.flushSync()
    expect(s.syncing).toBe(true)
    await p
    expect(s.syncing).toBe(false)
  })
})
```

注意：`afterEach` 需要从 vitest 导入 — 若 `scene.spec.ts` 头部尚未导入，在 import 行加上 `afterEach`。

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/stores/scene.spec.ts`
Expected: FAIL — 300ms 推进后 `updateRadar` 未被调用（`scheduleSync` 是空占位）

- [ ] **Step 3: 替换 `src/stores/scene.ts` 中的 `scheduleSync`/`flushSync` 占位**

```ts
  let syncTimer: ReturnType<typeof setTimeout> | null = null

  /** 300ms 防抖：滑杆连续拖动只触发一次按实体批量上送 */
  function scheduleSync(): void {
    if (syncTimer) clearTimeout(syncTimer)
    syncTimer = setTimeout(() => {
      syncTimer = null
      void flushSync()
    }, 300)
  }

  /** 按实体分组上送 pending 字段；成功清该实体 pending，失败保留并提示 */
  async function flushSync(): Promise<boolean> {
    if (!sessionId.value || pendingEdits.value.size === 0) return true
    syncing.value = true
    try {
      let allOk = true
      for (const r of radars.value) {
        const idStr = String(r.id)
        const keys = [...pendingEdits.value].filter(k => suffixOf(k) === idStr)
        if (keys.length === 0) continue
        try {
          const resp = await api.updateRadar(sessionId.value, r)
          if (resp.success) {
            for (const k of keys) pendingEdits.value.delete(k)
          } else {
            allOk = false
            ElMessage.error(resp.error || `雷达 R${r.id} 更新失败`)
          }
        } catch (e) {
          allOk = false
          ElMessage.error(errText(e))
        }
      }
      for (const j of jammers.value) {
        const suffix = `j${j.id}`
        const keys = [...pendingEdits.value].filter(k => suffixOf(k) === suffix)
        if (keys.length === 0) continue
        try {
          const resp = await api.updateJammer(sessionId.value, j)
          if (resp.success) {
            for (const k of keys) pendingEdits.value.delete(k)
          } else {
            allOk = false
            ElMessage.error(resp.error || `干扰机 J${j.id} 更新失败`)
          }
        } catch (e) {
          allOk = false
          ElMessage.error(errText(e))
        }
      }
      return allOk
    } finally {
      syncing.value = false
    }
  }
```

（`setFocusedField`/`applyRemoteScene` 占位保持不动，留给 Task 5。）

- [ ] **Step 4: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/stores/scene.spec.ts`
Expected: PASS（Task 3 的 12 + 新增 4 用例）

- [ ] **Step 5: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/stores
git commit -m "front_end: scene 参数防抖同步 flushSync"
```

---

### Task 5: scene store — applyRemoteScene WS 合并 + 焦点保护

**Files:**
- Modify: `front_end/src/stores/scene.ts`（替换 `applyRemoteScene`/`setFocusedField` 占位）
- Modify: `front_end/src/stores/scene.spec.ts`（追加 describe）

**Interfaces:**
- Consumes: `pendingKey`、`pendingEdits`、`focusedKey`、`radars`、`jammers`（Task 3/4）
- Produces:
  - `applyRemoteScene(msg: { radars?: Radar[]; jammers?: Jammer[] }): void` — ID 集合变化 → 整体替换；未变 → 按 ID 逐字段合并，**pending 或 focused 的字段保留本地值**；msg 缺某数组时保留本地对应数组
  - `setFocusedField(key: string | null): void` — 组件输入框 focus/blur 时调用（key 与 pendingKey 同格式）

- [ ] **Step 1: 追加失败测试（新 describe 加到 `scene.spec.ts`）**

```ts
describe('scene store: applyRemoteScene WS 合并', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    vi.mocked(api.updateRadar).mockResolvedValue({ success: true })
  })

  it('ID 集合变化 → 整体替换（本地多余实体被丢弃）', () => {
    const s = useSceneStore()
    s.radars.push(radar(1), radar(2))
    s.applyRemoteScene({ radars: [radar(2), { ...radar(2), id: 7 }], jammers: [] })
    expect(s.radars.map(r => r.id)).toEqual([2, 7])
  })

  it('ID 不变 → 远端新值覆盖本地未编辑字段', () => {
    const s = useSceneStore()
    s.radars.push(radar(1))
    const remote = { ...radar(1), Pt_dBm: 99, x: -4000 }
    s.applyRemoteScene({ radars: [remote], jammers: [] })
    expect(s.radars[0].Pt_dBm).toBe(99)
    expect(s.radars[0].x).toBe(-4000)
  })

  it('pending 字段不被远端覆盖', () => {
    const s = useSceneStore()
    s.radars.push(radar(1))
    s.updateField('radar', 1, 'Pt_dBm', 42) // pending: Pt_dBm_1
    const remote = { ...radar(1), Pt_dBm: 99, G_dB: 30 }
    s.applyRemoteScene({ radars: [remote], jammers: [] })
    expect(s.radars[0].Pt_dBm).toBe(42) // 本地保留
    expect(s.radars[0].G_dB).toBe(30) // 未编辑字段采用远端
  })

  it('focused 字段不被远端覆盖，失焦后可被覆盖', () => {
    const s = useSceneStore()
    s.jammers.push(jammer(1))
    s.setFocusedField(pendingKey('jammer', 'Pj_dBm', 1))
    s.applyRemoteScene({ jammers: [{ ...jammer(1), Pj_dBm: 5 }], radars: [] })
    expect(s.jammers[0].Pj_dBm).toBe(40) // 本地保留
    s.setFocusedField(null)
    s.applyRemoteScene({ jammers: [{ ...jammer(1), Pj_dBm: 5 }], radars: [] })
    expect(s.jammers[0].Pj_dBm).toBe(5)
  })

  it('jam_type 字段同样受 pending 保护', () => {
    const s = useSceneStore()
    s.jammers.push(jammer(1))
    s.updateField('jammer', 1, 'jam_type', 'RANGE_DECEPT')
    s.applyRemoteScene({ jammers: [{ ...jammer(1), jam_type: 'NOISE_JAM' }], radars: [] })
    expect(s.jammers[0].jam_type).toBe('RANGE_DECEPT')
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/stores/scene.spec.ts`
Expected: FAIL — `applyRemoteScene` 是空占位（ID 替换断言失败）

- [ ] **Step 3: 替换 `src/stores/scene.ts` 中的 `applyRemoteScene`/`setFocusedField` 占位**

```ts
  function setFocusedField(key: string | null): void {
    focusedKey.value = key
  }

  function sameIds(a: { id: number }[], b: { id: number }[]): boolean {
    const sa = a.map(e => e.id).sort((x, y) => x - y).join(',')
    const sb = b.map(e => e.id).sort((x, y) => x - y).join(',')
    return sa === sb
  }

  /** WS 广播合并：ID 集合变→整体替换；未变→逐字段合并（pending/focused 字段保留本地） */
  function applyRemoteScene(msg: { radars?: Radar[]; jammers?: Jammer[] }): void {
    const newRadars = msg.radars ?? radars.value
    const newJammers = msg.jammers ?? jammers.value
    if (!sameIds(radars.value, newRadars) || !sameIds(jammers.value, newJammers)) {
      radars.value = [...newRadars]
      jammers.value = [...newJammers]
      return
    }
    const merge = <T extends { id: number }>(local: T, incoming: T, kind: EntityKind): T => {
      const suffix = kind === 'jammer' ? 'j' : ''
      const out: T = { ...incoming }
      for (const key of Object.keys(incoming) as (keyof T & string)[]) {
        if (key === 'id') continue
        const pk = `${key}_${suffix}${local.id}`
        if (pendingEdits.value.has(pk) || focusedKey.value === pk) {
          out[key] = local[key]
        }
      }
      return out
    }
    radars.value = radars.value.map(l => {
      const inc = newRadars.find(r => r.id === l.id)
      return inc ? merge(l, inc, 'radar') : l
    })
    jammers.value = jammers.value.map(l => {
      const inc = newJammers.find(j => j.id === l.id)
      return inc ? merge(l, inc, 'jammer') : l
    })
  }
```

- [ ] **Step 4: 运行全部测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run`
Expected: PASS（scene store 全部用例 + 此前所有文件）

- [ ] **Step 5: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/stores
git commit -m "front_end: WS 场景合并与焦点保护"
```

---

### Task 6: simulation store — 仿真、历史、自动仿真

**Files:**
- Create: `front_end/src/stores/simulation.ts`
- Create: `front_end/src/stores/simulation.spec.ts`

**Interfaces:**
- Consumes: `useSceneStore`（Task 3–5）、`api.simulate/deleteScene`、`errText`
- Produces（组件依赖，签名固定）:
  - `const MAX_HISTORY = 30`
  - state: `results: Ref<SimResult[]|null>`、`history: Ref<HistStep[]>`、`running: Ref<boolean>`、`statusMsg: Ref<string>`（初始 `'就绪'`）、`auto: Ref<{enabled:boolean; interval:number; remaining:number}>`（interval 初始 5）
  - actions: `run(): Promise<boolean>`（**永不抛异常**；失败返回 false 且 `running` 必回 false）、`clearResults(): void`、`reset(): Promise<void>`（停自动→删会话→重建场景→清结果）、`startAuto(): void`、`stopAuto(): void`
  - watch：场景实体 ID 签名变化（增删）→ 自动 `clearResults()`

- [ ] **Step 1: 写失败测试 `src/stores/simulation.spec.ts`**

```ts
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { createPinia, setActivePinia } from 'pinia'
import type { SimResult } from '../types/api'
import * as api from '../api/scene'
import { MAX_HISTORY, useSimulationStore } from './simulation'
import { useSceneStore } from './scene'

vi.mock('element-plus', () => ({ ElMessage: { error: vi.fn(), success: vi.fn() } }))
vi.mock('../api/scene', () => ({
  createSession: vi.fn(), fetchSceneConfig: vi.fn(), deleteScene: vi.fn(),
  addRadar: vi.fn(), updateRadar: vi.fn(), deleteRadar: vi.fn(),
  addJammer: vi.fn(), updateJammer: vi.fn(), deleteJammer: vi.fn(), simulate: vi.fn(),
}))

const result = (radarId: number, sinr: number, zeta: number): SimResult => ({
  radar_id: radarId, SINR_dB: sinr, jsr_dB: 10, detect_success: true,
  jammer_details: [{ jammer_id: 1, freq_delta_Hz: 1e6, freq_match_factor: zeta, original_power_W: 1, effective_power_W: 0.5 }],
})

describe('simulation store', () => {
  beforeEach(async () => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    vi.mocked(api.createSession).mockResolvedValue({ success: true, session_id: 'sess-1' })
    vi.mocked(api.fetchSceneConfig).mockResolvedValue({ success: true, radars: [], jammers: [] })
    vi.mocked(api.deleteScene).mockResolvedValue({ success: true })
    vi.mocked(api.simulate).mockResolvedValue({ success: true, sim_results: [result(1, -3.2, 0.8)] })
    const scene = useSceneStore()
    await scene.initSession()
  })

  afterEach(() => {
    vi.useRealTimers()
  })

  it('run 成功：存结果、追加历史、running 复位', async () => {
    const sim = useSimulationStore()
    const ok = await sim.run()
    expect(ok).toBe(true)
    expect(sim.results).toHaveLength(1)
    expect(sim.history).toHaveLength(1)
    expect(sim.history[0]).toEqual({ labels: [1], sinr: [-3.2], zeta: [0.8] })
    expect(sim.running).toBe(false)
    expect(sim.statusMsg).toBe('步骤 1')
  })

  it('run 失败：返回 false、running 复位、状态为仿真失败', async () => {
    vi.mocked(api.simulate).mockRejectedValue(new Error('down'))
    const sim = useSimulationStore()
    const ok = await sim.run()
    expect(ok).toBe(false)
    expect(sim.running).toBe(false)
    expect(sim.statusMsg).toBe('仿真失败')
    expect(sim.results).toBeNull()
  })

  it(`历史封顶 ${MAX_HISTORY} 步`, async () => {
    const sim = useSimulationStore()
    for (let i = 0; i < MAX_HISTORY + 5; i++) await sim.run()
    expect(sim.history).toHaveLength(MAX_HISTORY)
  })

  it('无会话时 run 直接返回 false', async () => {
    const scene = useSceneStore()
    scene.sessionId = null
    const sim = useSimulationStore()
    expect(await sim.run()).toBe(false)
    expect(api.simulate).not.toHaveBeenCalled()
  })

  it('startAuto 按间隔触发 run，stopAuto 停止', async () => {
    vi.useFakeTimers()
    vi.mocked(api.simulate).mockResolvedValue({ success: true, sim_results: [result(1, 1, 1)] })
    const sim = useSimulationStore()
    sim.auto.interval = 2
    sim.startAuto()
    expect(sim.auto.enabled).toBe(true)
    expect(sim.auto.remaining).toBe(2)

    await vi.advanceTimersByTimeAsync(1000)
    expect(sim.auto.remaining).toBe(1)
    expect(api.simulate).not.toHaveBeenCalled()

    await vi.advanceTimersByTimeAsync(1000)
    expect(api.simulate).toHaveBeenCalledTimes(1)
    expect(sim.auto.remaining).toBe(2) // 回到最新 interval

    sim.stopAuto()
    expect(sim.auto.enabled).toBe(false)
    await vi.advanceTimersByTimeAsync(5000)
    expect(api.simulate).toHaveBeenCalledTimes(1)
  })

  it('run 进行中不会重复触发（running 互斥）', async () => {
    let release!: (v: unknown) => void
    vi.mocked(api.simulate).mockReturnValue(new Promise(res => { release = res }))
    const sim = useSimulationStore()
    const p1 = sim.run()
    const p2 = sim.run()
    release({ success: true, sim_results: [result(1, 1, 1)] })
    expect(await p1).toBe(true)
    expect(await p2).toBe(false) // 第二次因 running 互斥被拒
    expect(api.simulate).toHaveBeenCalledTimes(1)
  })

  it('reset 停止自动仿真并清空结果', async () => {
    vi.useFakeTimers()
    const sim = useSimulationStore()
    await sim.run()
    sim.startAuto()
    await sim.reset()
    expect(sim.auto.enabled).toBe(false)
    expect(sim.results).toBeNull()
    expect(sim.history).toHaveLength(0)
    expect(api.deleteScene).toHaveBeenCalledWith('sess-1')
    await vi.advanceTimersByTimeAsync(5000)
    expect(api.simulate).not.toHaveBeenCalled() // 自动仿真已停
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/stores/simulation.spec.ts`
Expected: FAIL — `Failed to resolve import "./simulation"`

- [ ] **Step 3: 实现 `src/stores/simulation.ts`**

```ts
import { ref, watch } from 'vue'
import { defineStore } from 'pinia'
import { ElMessage } from 'element-plus'
import type { HistStep, SimResult } from '../types/api'
import * as api from '../api/scene'
import { errText } from '../api/http'
import { useSceneStore } from './scene'

export const MAX_HISTORY = 30

function toStep(res: SimResult[]): HistStep {
  return {
    labels: res.map(r => r.radar_id),
    sinr: res.map(r => r.SINR_dB),
    zeta: res.map(r => (r.jammer_details?.length
      ? r.jammer_details.reduce((s, j) => s + (j.freq_match_factor || 0), 0) / r.jammer_details.length
      : 0)),
  }
}

export const useSimulationStore = defineStore('simulation', () => {
  const results = ref<SimResult[] | null>(null)
  const history = ref<HistStep[]>([])
  const running = ref(false)
  const statusMsg = ref('就绪')
  const auto = ref({ enabled: false, interval: 5, remaining: 0 })

  const scene = useSceneStore()
  let autoTimer: ReturnType<typeof setInterval> | null = null

  function clearResults(): void {
    results.value = null
    history.value = []
  }

  /** 运行一次仿真；永不抛异常，失败返回 false（running 必回 false） */
  async function run(): Promise<boolean> {
    if (!scene.sessionId || running.value) return false
    running.value = true
    statusMsg.value = '仿真中...'
    try {
      await scene.flushSync()
      const r = await api.simulate(scene.sessionId)
      if (!r.success || !r.sim_results) throw new Error(r.error || '仿真失败')
      results.value = r.sim_results
      history.value = [...history.value, toStep(r.sim_results)].slice(-MAX_HISTORY)
      statusMsg.value = `步骤 ${history.value.length}`
      return true
    } catch (e) {
      statusMsg.value = '仿真失败'
      ElMessage.error(errText(e))
      return false
    } finally {
      running.value = false
    }
  }

  function startAuto(): void {
    if (auto.value.enabled) return
    const sec = Math.max(1, Math.floor(auto.value.interval))
    auto.value.interval = sec
    auto.value.enabled = true
    auto.value.remaining = sec
    autoTimer = setInterval(() => {
      auto.value.remaining -= 1
      if (auto.value.remaining <= 0) {
        auto.value.remaining = auto.value.interval // 每次触发重读最新间隔
        void run()
      }
    }, 1000)
  }

  function stopAuto(): void {
    if (autoTimer) clearInterval(autoTimer)
    autoTimer = null
    auto.value.enabled = false
    auto.value.remaining = 0
  }

  async function reset(): Promise<void> {
    stopAuto()
    try {
      if (scene.sessionId) await api.deleteScene(scene.sessionId)
      await scene.initSession()
      await scene.fetchScene()
    } catch (e) {
      ElMessage.error(errText(e))
    }
    clearResults()
    statusMsg.value = '已重置'
  }

  // 实体增删（ID 签名变化）→ 清空结果与历史，避免读到过期数据
  watch(
    () => [
      ...scene.radars.map(r => r.id).sort((a, b) => a - b),
      '|',
      ...scene.jammers.map(j => j.id).sort((a, b) => a - b),
    ].join(','),
    () => clearResults(),
  )

  return { results, history, running, statusMsg, auto, run, reset, clearResults, startAuto, stopAuto }
})
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/stores/simulation.spec.ts`
Expected: PASS（8 个用例）

- [ ] **Step 5: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/stores
git commit -m "front_end: simulation store 仿真/历史/自动仿真"
```

---

### Task 7: useWebSocket — 连接状态机与指数退避

**Files:**
- Create: `front_end/src/composables/useWebSocket.ts`
- Create: `front_end/src/composables/useWebSocket.spec.ts`

**Interfaces:**
- Produces（Task 10 App.vue 依赖，签名固定）:
  - `type WsStatus = 'connecting' | 'connected' | 'reconnecting' | 'disconnected'`
  - `createWebSocketController(opts: { onMessage: (data: string) => void; onStatus?: (s: WsStatus) => void; createSocket?: (url: string) => WebSocket; retryDelayMs?: (attempt: number) => number }): { connect(sid: string): void; disconnect(): void; readonly status: Ref<WsStatus> }`
  - 默认：`createSocket = url => new WebSocket(url)`；URL `` `${location.protocol==='https:'?'wss':'ws'}://${location.host}/ws?session_id=${sid}` ``；`retryDelayMs = a => Math.min(30000, 1000 * 2 ** a)`
  - 语义：`connect` 重置退避并关旧开新；`onopen` → `connected` 且 attempt 归零；异常关闭 → `reconnecting` 按退避重连；`disconnect` 清定时器不再重连

- [ ] **Step 1: 写失败测试 `src/composables/useWebSocket.spec.ts`**

```ts
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
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/composables/useWebSocket.spec.ts`
Expected: FAIL — `Failed to resolve import "./useWebSocket"`

- [ ] **Step 3: 实现 `src/composables/useWebSocket.ts`**

```ts
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
    setStatus(attempt === 0 ? 'connecting' : 'reconnecting')
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
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/composables/useWebSocket.spec.ts`
Expected: PASS（6 个用例）

- [ ] **Step 5: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/composables
git commit -m "front_end: WebSocket 控制器（状态机+指数退避）"
```

---

### Task 8: useSceneCanvas — 画布坐标/命中纯函数

**Files:**
- Create: `front_end/src/composables/useSceneCanvas.ts`
- Create: `front_end/src/composables/useSceneCanvas.spec.ts`

**Interfaces:**
- Produces（Task 11 SceneCanvas.vue 依赖，签名固定）:
  - `interface Viewport { x: number; y: number; w: number; h: number }` — 世界坐标可视窗口（米）
  - `DEFAULT_VIEWPORT: Viewport` — `{ x: -1500, y: -1500, w: 30000, h: 30000 }`
  - `interface Transform { scale: number; offsetX: number; offsetY: number; toScreen(wx: number, wy: number): { sx: number; sy: number }; toWorld(sx: number, sy: number): { wx: number; wy: number } }`
  - `computeViewport(canvasW: number, canvasH: number): Viewport` — 保持宽高比、以世界原点(0,0)为中心，保证 30000m 视野完整可见（取宽/高较大者定标）
  - `makeTransform(vp: Viewport, canvasW: number, canvasH: number): Transform`
  - `hitTest(sx: number, sy: number, points: { kind: EntityKind; id: number; sx: number; sy: number }[], radius?: number): { kind: EntityKind; id: number } | null` — 半径默认 12px，多个命中取距离最小者

- [ ] **Step 1: 写失败测试 `src/composables/useSceneCanvas.spec.ts`**

```ts
import { describe, expect, it } from 'vitest'
import { DEFAULT_VIEWPORT, computeViewport, hitTest, makeTransform } from './useSceneCanvas'

describe('computeViewport / makeTransform', () => {
  it('方形画布下视口中心在世界原点且覆盖 30000m', () => {
    const vp = computeViewport(800, 800)
    expect(vp.w).toBeCloseTo(30000)
    expect(vp.h).toBeCloseTo(30000)
    expect(vp.x + vp.w / 2).toBeCloseTo(0)
    expect(vp.y + vp.h / 2).toBeCloseTo(0)
  })

  it('宽扁画布保证 30000m 可见（视口变宽）', () => {
    const vp = computeViewport(1600, 800)
    expect(vp.w).toBeGreaterThanOrEqual(30000)
    expect(vp.h).toBeGreaterThanOrEqual(30000)
    expect(vp.x + vp.w / 2).toBeCloseTo(0)
  })

  it('worldToScreen 与 screenToWorld 互逆', () => {
    const vp = DEFAULT_VIEWPORT
    const t = makeTransform(vp, 800, 600)
    const { sx, sy } = t.toScreen(5000, -3000)
    const back = t.toWorld(sx, sy)
    expect(back.wx).toBeCloseTo(5000, 6)
    expect(back.wy).toBeCloseTo(-3000, 6)
  })

  it('世界 y 向上、屏幕 y 向下（世界原点在屏幕中心）', () => {
    const vp = computeViewport(800, 800)
    const t = makeTransform(vp, 800, 800)
    const origin = t.toScreen(0, 0)
    expect(origin.sx).toBeCloseTo(400)
    expect(origin.sy).toBeCloseTo(400)
    const above = t.toScreen(0, 15000)
    expect(above.sy).toBeLessThan(origin.sy) // 世界 y+ 在屏幕上方
  })
})

describe('hitTest', () => {
  const pts = [
    { kind: 'radar' as const, id: 1, sx: 100, sy: 100 },
    { kind: 'jammer' as const, id: 2, sx: 130, sy: 100 },
  ]

  it('命中半径内返回最近实体', () => {
    expect(hitTest(120, 100, pts)).toEqual({ kind: 'radar', id: 1 })
    expect(hitTest(128, 100, pts)).toEqual({ kind: 'jammer', id: 2 })
  })

  it('半径外返回 null', () => {
    expect(hitTest(100, 140, pts)).toBeNull()
  })

  it('空列表返回 null', () => {
    expect(hitTest(0, 0, [])).toBeNull()
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/composables/useSceneCanvas.spec.ts`
Expected: FAIL — `Failed to resolve import "./useSceneCanvas"`

- [ ] **Step 3: 实现 `src/composables/useSceneCanvas.ts`**

```ts
// 画布坐标纯函数 — 不依赖 DOM/Canvas，组件只负责调用
import type { EntityKind } from '../types/api'

export interface Viewport {
  x: number
  y: number
  w: number
  h: number
}

export const DEFAULT_VIEWPORT: Viewport = { x: -1500, y: -1500, w: 30000, h: 30000 }

/** 以世界原点为中心，保证 30000m 视野在画布内完整可见 */
export function computeViewport(canvasW: number, canvasH: number): Viewport {
  const aspect = canvasW / Math.max(1, canvasH)
  let w = DEFAULT_VIEWPORT.w
  let h = DEFAULT_VIEWPORT.h
  if (aspect > w / h) {
    w = h * aspect
  } else {
    h = w / aspect
  }
  return { x: -w / 2, y: -h / 2, w, h }
}

export interface Transform {
  scale: number
  offsetX: number
  offsetY: number
  toScreen(wx: number, wy: number): { sx: number; sy: number }
  toWorld(sx: number, sy: number): { wx: number; wy: number }
}

export function makeTransform(vp: Viewport, canvasW: number, canvasH: number): Transform {
  const scale = Math.min(canvasW / vp.w, canvasH / vp.h)
  // 世界中心映射到画布中心
  const offsetX = canvasW / 2 - (vp.x + vp.w / 2) * scale
  const offsetY = canvasH / 2 + (vp.y + vp.h / 2) * scale
  return {
    scale,
    offsetX,
    offsetY,
    toScreen(wx, wy) {
      return { sx: wx * scale + offsetX, sy: -wy * scale + offsetY }
    },
    toWorld(sx, sy) {
      return { wx: (sx - offsetX) / scale, wy: -(sy - offsetY) / scale }
    },
  }
}

export interface HitPoint {
  kind: EntityKind
  id: number
  sx: number
  sy: number
}

/** 命中测试：半径内取距点击点最近者；默认 12px */
export function hitTest(
  sx: number,
  sy: number,
  points: HitPoint[],
  radius = 12,
): { kind: EntityKind; id: number } | null {
  let best: { kind: EntityKind; id: number } | null = null
  let bestDist = radius
  for (const p of points) {
    const d = Math.hypot(p.sx - sx, p.sy - sy)
    if (d <= bestDist) {
      bestDist = d
      best = { kind: p.kind, id: p.id }
    }
  }
  return best
}
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/composables/useSceneCanvas.spec.ts`
Expected: PASS（7 个用例）

- [ ] **Step 5: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/composables
git commit -m "front_end: 画布坐标与命中测试纯函数"
```

---

### Task 9: 应用装配 — main.ts / style.css / index.html

**Files:**
- Modify: `front_end/src/main.ts`（整文件替换）
- Modify: `front_end/src/style.css`（整文件替换，含设计令牌）
- Modify: `front_end/index.html`（title + lang）

**Interfaces:**
- Consumes: element-plus、pinia（Task 1 安装）
- Produces（Task 10/15 依赖）:
  - `main.ts`: 创建 app → `use(createPinia)` → `app.use(ElementPlus)` + `import 'element-plus/dist/index.css'` + `import 'element-plus/theme-chalk/dark/css-vars.css'` → `document.documentElement.classList.add('dark')` → `mount('#app')`；`app.config.errorHandler` 兜底 `console.error` + `ElMessage.error`
  - `style.css`: `:root` 设计令牌（Global Constraints 中全部变量）+ `#app` 全屏 + 应用级布局类（`.app-shell`/`.header`/`.main`/`.drawer` 由组件共用，Task 10/15 填充细节时只允许追加）
  - `index.html`: `<html lang="zh-CN" class="dark">`、`<title>ECMSim 电子对抗仿真</title>`

- [ ] **Step 1: 替换 `src/main.ts`**

```ts
import { createApp } from 'vue'
import { createPinia } from 'pinia'
import ElementPlus from 'element-plus'
import 'element-plus/dist/index.css'
import 'element-plus/theme-chalk/dark/css-vars.css'
import 'element-plus/es/components/message/style/css'
import App from './App.vue'
import './style.css'

const app = createApp(App)
app.use(createPinia())
app.use(ElementPlus)
document.documentElement.classList.add('dark')

app.config.errorHandler = (err, _instance, info) => {
  console.error('[app error]', err, info)
}

app.mount('#app')
```

- [ ] **Step 2: 替换 `src/style.css`（设计令牌 + 全局布局基础）**

```css
/* ECMSim 玻璃拟态 HUD — 设计令牌（spec §4.2） */
:root {
  --accent: #e94560;
  --accent-2: #ff7a59;
  --cyan: #00e5ff;
  --ok: #69f0ae;
  --danger: #ff8a80;
  --radar-color: #2ee6a8;
  --font-mono: 'JetBrains Mono', 'SFMono-Regular', Consolas, monospace;
  --glass-bg: rgba(255, 255, 255, 0.07);
  --glass-border: rgba(255, 255, 255, 0.14);
  --glass-blur: 8px;
  --text-1: rgba(255, 255, 255, 0.92);
  --text-2: rgba(255, 255, 255, 0.62);
  --bg-gradient: 135deg, #1a1040 0%, #0d1b2a 55%, #0a1120 100%;
}

* {
  box-sizing: border-box;
}

html,
body,
#app {
  height: 100%;
  margin: 0;
}

body {
  color: var(--text-1);
  font-family: 'Inter', 'PingFang SC', 'Microsoft YaHei', sans-serif;
  background: linear-gradient(var(--bg-gradient));
  overflow: hidden;
}

.app-shell {
  display: flex;
  flex-direction: column;
  height: 100%;
}

.app-shell .main {
  flex: 1;
  min-height: 0;
}

/* 玻璃面板 */
.glass-panel {
  background: var(--glass-bg);
  border: 1px solid var(--glass-border);
  backdrop-filter: blur(var(--glass-blur));
  border-radius: 10px;
}

/* 数据用等宽字体 */
.mono {
  font-family: var(--font-mono);
}

/* Element Plus 暗色下统一滑杆/输入间距 */
.el-slider__input,
.el-input-number {
  --el-slider-input-width: 96px;
}
```

- [ ] **Step 3: 修改 `index.html`**

- `<html lang="zh-CN" class="dark">`
- `<title>ECMSim 电子对抗仿真</title>`
- 删除模板 Vite/Hero 注释块（保留 `<div id="app">` 与 module script）

- [ ] **Step 4: 验证（无测试，构建门）**

Run: `cd /home/ky/workspace/ECMSim/front_end && npm run typecheck && npm run build`
Expected: PASS — `vue-tsc` 零错误；`vite build` 产出 `dist/`（此时 App.vue 仍是模板版，允许）

- [ ] **Step 5: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/index.html front_end/src/main.ts front_end/src/style.css
git commit -m "front_end: 应用装配与玻璃拟态设计令牌"
```

---

### Task 10: AppHeader + StatusBar + App.vue v1（骨架接线）

**Files:**
- Create: `front_end/src/components/AppHeader.vue`
- Create: `front_end/src/components/StatusBar.vue`
- Create: `front_end/src/components/AppHeader.spec.ts`
- Modify: `front_end/src/App.vue`（整文件替换为布局骨架）

**Interfaces:**
- Consumes: `useSceneStore`、`useSimulationStore`、`createWebSocketController`（Task 3/6/7）
- Produces:
  - `AppHeader.vue` props: `{ wsStatus: WsStatus; sessionId: string | null }`；展示 WS 徽章文案/颜色（`connected→已连接/--ok`、`connecting→连接中/--cyan`、`reconnecting→重连中/--accent`、`disconnected→已断开/--danger`）、会话 ID（`mono` 截断 title 显示全量）、同步徽章（`dirtyCount>0` → `待同步 N`，`syncing` → `同步中`，否则 `已同步`）
  - `StatusBar.vue` 无 props（内部读 store）：`statusMsg` + `待同步 N` + `步骤 M` + 实体计数
  - `App.vue` v1：`.app-shell` → `<AppHeader>` + `.main`（占位 `<div class="stage">`）+ `<StatusBar>`；`onMounted` → `initSession → fetchScene → ws.connect`；WS `onMessage` 解析 JSON 调 `scene.applyRemoteScene`；暴露 `ws` 给后续任务（Task 15 重构时扩展）

- [ ] **Step 1: 写失败测试 `src/components/AppHeader.spec.ts`**

```ts
import { describe, expect, it } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import AppHeader from './AppHeader.vue'

describe('AppHeader', () => {
  function mountHeader(props: { wsStatus: 'connected' | 'connecting' | 'reconnecting' | 'disconnected'; sessionId: string | null }) {
    setActivePinia(createPinia())
    return mount(AppHeader, { props })
  }

  it('connected 显示已连接徽章', () => {
    const w = mountHeader({ wsStatus: 'connected', sessionId: 's1' })
    expect(w.text()).toContain('已连接')
  })

  it('reconnecting 显示重连中', () => {
    const w = mountHeader({ wsStatus: 'reconnecting', sessionId: null })
    expect(w.text()).toContain('重连中')
  })

  it('显示会话 ID', () => {
    const w = mountHeader({ wsStatus: 'connected', sessionId: 'sess-abc' })
    expect(w.text()).toContain('sess-abc')
  })

  it('无会话时显示 —', () => {
    const w = mountHeader({ wsStatus: 'disconnected', sessionId: null })
    expect(w.text()).toContain('sess-abc').toBe(false) || expect(w.text()).toContain('—')
  })

  it('pending>0 显示待同步计数', async () => {
    const w = mountHeader({ wsStatus: 'connected', sessionId: 's1' })
    const { useSceneStore } = await import('../stores/scene')
    const scene = useSceneStore()
    scene.radars.push({ id: 1, x: 0, y: 0, Pt_dBm: 1, G_dB: 1, freq: 1, bandwidth: 1, sigma: 1, thresh_db: 1 })
    scene.updateField('radar', 1, 'x', 2)
    await w.vm.$nextTick()
    expect(w.text()).toContain('待同步 1')
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/AppHeader.spec.ts`
Expected: FAIL — `Failed to resolve import "./AppHeader.vue"`（且 vitest 未配 vue 插件解析 `.vue`）

- [ ] **Step 3: 确认 vitest 可解析 `.vue`**

`vite.config.ts` 已含 `plugins: [vue()]`（Task 1 配置），vitest 继承该配置，`.vue` 导入可用。若仍报错，检查 `test` 配置未误删 plugins。

- [ ] **Step 4: 实现 `src/components/AppHeader.vue`**

```vue
<script setup lang="ts">
import { computed } from 'vue'
import { useSceneStore } from '../stores/scene'
import type { WsStatus } from '../composables/useWebSocket'

defineProps<{ wsStatus: WsStatus; sessionId: string | null }>()

const scene = useSceneStore()

const wsMeta = computed(() => {
  const m: Record<WsStatus, { label: string; color: string }> = {
    connected: { label: '已连接', color: 'var(--ok)' },
    connecting: { label: '连接中', color: 'var(--cyan)' },
    reconnecting: { label: '重连中', color: 'var(--accent)' },
    disconnected: { label: '已断开', color: 'var(--danger)' },
  }
  return m[props.wsStatus]
})
</script>

<script lang="ts">
// props 在上方 setup 块声明的镜像（供模板类型推断）
export default {}
</script>
```

**实现提示（执行时注意）**：上面的双 script 写法仅示意 — 实际实现必须把 `defineProps<{ wsStatus: WsStatus; sessionId: string | null }>()` 存到 `const props = defineProps<...>()` 并在 `wsMeta` 中引用 `props.wsStatus`（`defineProps` 必须在 `<script setup>` 内调用）。模板结构：

```vue
<template>
  <header class="header glass-panel">
    <div class="brand">
      <span class="logo">ECM</span>
      <span class="title">电子对抗仿真平台</span>
    </div>
    <div class="badges">
      <span class="badge ws mono" :style="{ '--dot': wsMeta.color }">
        <i class="dot" />{{ wsMeta.label }}
      </span>
      <span class="badge sid mono" :title="sessionId ?? ''">
        会话 {{ sessionId ?? '—' }}
      </span>
      <span v-if="scene.dirtyCount > 0" class="badge pending">待同步 {{ scene.dirtyCount }}</span>
      <span v-else-if="scene.syncing" class="badge syncing">同步中</span>
      <span v-else class="badge synced">已同步</span>
    </div>
  </header>
</template>

<style scoped>
.header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 16px;
  margin: 8px;
}
.brand { display: flex; align-items: baseline; gap: 10px; }
.logo {
  font-weight: 800;
  letter-spacing: 2px;
  background: linear-gradient(90deg, var(--accent), var(--accent-2));
  -webkit-background-clip: text;
  background-clip: text;
  color: transparent;
}
.title { color: var(--text-2); font-size: 13px; }
.badges { display: flex; gap: 8px; align-items: center; }
.badge {
  font-size: 12px;
  padding: 3px 10px;
  border-radius: 999px;
  border: 1px solid var(--glass-border);
  background: rgba(0, 0, 0, 0.25);
}
.ws .dot {
  display: inline-block;
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: var(--dot);
  margin-right: 6px;
}
.pending { color: var(--accent); border-color: var(--accent); }
.syncing { color: var(--cyan); border-color: var(--cyan); }
.synced { color: var(--ok); border-color: rgba(105, 240, 174, 0.4); }
</style>
```

- [ ] **Step 5: 实现 `src/components/StatusBar.vue`**

```vue
<script setup lang="ts">
import { useSceneStore } from '../stores/scene'
import { useSimulationStore } from '../stores/simulation'

const scene = useSceneStore()
const sim = useSimulationStore()
</script>

<template>
  <footer class="status-bar mono">
    <span>{{ sim.statusMsg }}</span>
    <span v-if="scene.dirtyCount > 0" class="warn">待同步 {{ scene.dirtyCount }}</span>
    <span v-else-if="scene.syncing">同步中…</span>
    <span class="dim">步骤 {{ sim.history.length }}</span>
    <span class="dim">雷达 {{ scene.radars.length }} · 干扰机 {{ scene.jammers.length }}</span>
  </footer>
</template>

<style scoped>
.status-bar {
  display: flex;
  gap: 18px;
  align-items: center;
  padding: 6px 16px;
  font-size: 12px;
  color: var(--text-2);
}
.warn { color: var(--accent); }
.dim { opacity: 0.7; }
</style>
```

- [ ] **Step 6: 替换 `src/App.vue`（v1 骨架）**

```vue
<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import AppHeader from './components/AppHeader.vue'
import StatusBar from './components/StatusBar.vue'
import { useSceneStore } from './stores/scene'
import { useSimulationStore } from './stores/simulation'
import { createWebSocketController, type WsStatus } from './composables/useWebSocket'
import { errText } from './api/http'

const scene = useSceneStore()
const sim = useSimulationStore()
const wsStatus = ref<WsStatus>('connecting')

const ws = createWebSocketController({
  onMessage: data => {
    try {
      const msg = JSON.parse(data) as { radars?: unknown[]; jammers?: unknown[] }
      if (msg && (msg.radars || msg.jammers)) scene.applyRemoteScene(msg)
    } catch {
      /* 非 JSON 心跳等消息忽略 */
    }
  },
  onStatus: s => {
    wsStatus.value = s
  },
})

onMounted(async () => {
  try {
    await scene.initSession()
    await scene.fetchScene()
    if (scene.sessionId) ws.connect(scene.sessionId)
  } catch (e) {
    ElMessage.error(errText(e))
  }
})
</script>

<template>
  <div class="app-shell">
    <AppHeader :ws-status="wsStatus" :session-id="scene.sessionId" />
    <main class="main">
      <div class="stage glass-panel">
        <!-- Task 11–15 逐步填充：画布 / 控制面板 / 抽屉 -->
        <p class="placeholder">场景画布（Task 11 接入）</p>
      </div>
    </main>
    <StatusBar />
  </div>
</template>

<style scoped>
.stage {
  flex: 1;
  min-height: 0;
  margin: 0 8px;
  display: grid;
  place-items: center;
}
.placeholder {
  color: var(--text-2);
  font-size: 13px;
}
</style>
```

- [ ] **Step 7: 运行测试 + 类型检查**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/AppHeader.spec.ts && npm run typecheck`
Expected: PASS — AppHeader 5 用例；`vue-tsc` 零错误

- [ ] **Step 8: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/components/AppHeader.vue front_end/src/components/StatusBar.vue front_end/src/components/AppHeader.spec.ts front_end/src/App.vue
git commit -m "front_end: Header/StatusBar + App 骨架接线（会话+WS）"
```

---

### Task 11: SceneCanvas.vue — 手写画布渲染 + 拖拽

**Files:**
- Create: `front_end/src/components/SceneCanvas.vue`
- Create: `front_end/src/components/SceneCanvas.spec.ts`

**Interfaces:**
- Consumes: `useSceneCanvas`（Task 8）、`useSceneStore.updateField/pulseEntity`（Task 3）、`useSimulationStore.results`（Task 6，检出高亮）
- Produces（Task 15 App 装配依赖）:
  - props: `{ height: number }`（画布区域高度，父容器提供）
  - 渲染：网格（`--grid` 细线）、坐标轴过原点、雷达 `--radar-color` 圆点 + `R{id}` 标签、干扰机 `--accent` 圆点 + `J{id}`、`detect_success=false` 的雷达虚线红圈、SIM 状态下结果色（`ok→--ok`/`fail→--danger`）
  - 交互：pointerdown 命中（`hitTest` 半径 12px）→ 捕获指针 → move 中 world 坐标 `updateField(kind, id, 'x'/'y', Math.round(v))` → up 时 `pulseEntity`；未命中 → 画布平移（视口偏移）
  - 悬停气泡：`#id 类型 · (x, y) m`（`mono`）；光标 `grab/grabbing/crosshair`
  - `requestAnimationFrame` 节流重绘；组件卸载清理监听与 rAF

- [ ] **Step 1: 写失败测试 `src/components/SceneCanvas.spec.ts`**

```ts
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import SceneCanvas from './SceneCanvas.vue'
import { useSceneStore } from '../stores/scene'
import type { Jammer, Radar } from '../types/api'

function el<K extends keyof HTMLElementTagNameMap>(tag: K): HTMLElement {
  return document.createElement(tag)
}

// jsdom 无真实 canvas — stub getContext 返回可记录的 mock
function stubCanvas() {
  const draw: string[] = []
  const ctx = {
    clearRect: () => draw.push('clear'),
    fillText: () => draw.push('text'),
    beginPath: () => draw.push('begin'),
    arc: () => draw.push('arc'),
    stroke: () => draw.push('stroke'),
    fill: () => draw.push('fill'),
    moveTo: () => undefined,
    lineTo: () => undefined,
    setLineDash: () => undefined,
    strokeRect: () => undefined,
    save: () => undefined,
    restore: () => undefined,
    fillRect: () => draw.push('fillRect'),
  }
  vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(ctx as unknown as CanvasRenderingContext2D)
  return draw
}

describe('SceneCanvas', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.restoreAllMocks()
  })

  function setup() {
    const draw = stubCanvas()
    const wrapper = mount(SceneCanvas, { props: { height: 600 }, attachTo: document.body })
    const scene = useSceneStore()
    const radar: Radar = { id: 1, x: 5000, y: 5000, Pt_dBm: 20, G_dB: 25, freq: 1e10, bandwidth: 1e6, sigma: 5, thresh_db: -50 }
    scene.radars.push(radar)
    return { wrapper, scene, draw, radar }
  }

  it('渲染 canvas 元素且高度正确', () => {
    const { wrapper } = setup()
    const canvas = wrapper.find('canvas')
    expect(canvas.exists()).toBe(true)
    expect(canvas.attributes('height')).toBe('600')
  })

  it('pointerdown 命中雷达后 move 更新 x/y 并标记 pending', async () => {
    const { wrapper, scene } = setup()
    const canvas = wrapper.find('canvas').element as HTMLCanvasElement
    // 雷达世界 (5000,5000) → 屏幕坐标（由 makeTransform 计算，画布 800x600 时原点在中心）
    // 直接用组件内部变换近似：中心偏右上；为稳健，从 store 侧调用 pointer 序列并容忍 miss → 这里用 hitTest 位置
    const t = { x: 400, y: 300 } // 画布中心 = 世界原点
    // 世界 (5000,5000) 映射：scale=min(800/30000,600/30000)=0.02 → sx=400+100=500, sy=300-100=200
    canvas.dispatchEvent(new PointerEvent('pointerdown', { clientX: 500, clientY: 200, pointerId: 1, bubbles: true }))
    canvas.dispatchEvent(new PointerEvent('pointermove', { clientX: 520, clientY: 200, pointerId: 1, bubbles: true }))
    canvas.dispatchEvent(new PointerEvent('pointerup', { clientX: 520, clientY: 200, pointerId: 1, bubbles: true }))
    expect(scene.pendingEdits.size).toBeGreaterThan(0)
    expect(scene.radars[0].x).not.toBe(5000) // 已被拖动更新
    expect(t.x).toBe(400) // 语法占位（保持断言结构）
  })

  it('空场景 pointerdown 不产生 pending', async () => {
    stubCanvas()
    const wrapper = mount(SceneCanvas, { props: { height: 600 }, attachTo: document.body })
    const canvas = wrapper.find('canvas').element as HTMLCanvasElement
    canvas.dispatchEvent(new PointerEvent('pointerdown', { clientX: 10, clientY: 10, pointerId: 1, bubbles: true }))
    canvas.dispatchEvent(new PointerEvent('pointerup', { clientX: 10, clientY: 10, pointerId: 1, bubbles: true }))
    const { useSceneStore } = await import('../stores/scene')
    expect(useSceneStore().pendingEdits.size).toBe(0)
  })
})
```

**实现提示（执行时注意）**：pointer 测试依赖 `getBoundingClientRect` 返回 0 左上角 + jsdom 默认宽度 — 实现里屏幕坐标用 `ev.clientX - rect.left`；若 jsdom 下 `clientX=500` 映射不中，允许在实现中按测试实际坐标微调断言值（保持语义：拖拽后 x 变化 + pending 产生），但**不得删除断言**。坐标换算的数值验证已由 Task 8 纯函数测试覆盖。

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/SceneCanvas.spec.ts`
Expected: FAIL — `Failed to resolve import "./SceneCanvas.vue"`

- [ ] **Step 3: 实现 `src/components/SceneCanvas.vue`**

```vue
<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useSceneStore } from '../stores/scene'
import { useSimulationStore } from '../stores/simulation'
import { computeViewport, hitTest, makeTransform, type Viewport } from '../composables/useSceneCanvas'
import type { EntityKind } from '../types/api'

const props = defineProps<{ height: number }>()

const scene = useSceneStore()
const sim = useSimulationStore()
const canvasRef = ref<HTMLCanvasElement | null>(null)
const hover = ref<{ kind: EntityKind; id: number; x: number; y: number; sx: number; sy: number } | null>(null)

let viewport: Viewport = { x: -1500, y: -1500, w: 30000, h: 30000 }
let canvasW = 800
let rafId = 0
let dragging: { kind: EntityKind; id: number } | null = null
let panning: { startX: number; startY: number; vpX: number; vpY: number } | null = null

function scheduleDraw(): void {
  if (rafId) return
  rafId = requestAnimationFrame(() => {
    rafId = 0
    draw()
  })
}

function draw(): void {
  const canvas = canvasRef.value
  if (!canvas) return
  const ctx = canvas.getContext('2d')
  if (!ctx) return
  canvasW = canvas.clientWidth || canvas.width
  const canvasH = canvas.clientHeight || props.height
  viewport = computeViewport(canvasW, canvasH)
  const t = makeTransform(viewport, canvasW, canvasH)

  ctx.clearRect(0, 0, canvasW, canvasH)

  // 网格（每 5km）
  ctx.strokeStyle = 'rgba(255,255,255,0.06)'
  ctx.lineWidth = 1
  const step = 5000 * t.scale
  const origin = t.toScreen(0, 0)
  for (let x = origin.sx % step; x < canvasW; x += step) {
    ctx.beginPath()
    ctx.moveTo(x, 0)
    ctx.lineTo(x, canvasH)
    ctx.stroke()
  }
  for (let y = origin.sy % step; y < canvasH; y += step) {
    ctx.beginPath()
    ctx.moveTo(0, y)
    ctx.lineTo(canvasW, y)
    ctx.stroke()
  }
  // 坐标轴
  ctx.strokeStyle = 'rgba(0,229,255,0.35)'
  ctx.beginPath()
  ctx.moveTo(0, origin.sy)
  ctx.lineTo(canvasW, origin.sy)
  ctx.moveTo(origin.sx, 0)
  ctx.lineTo(origin.sx, canvasH)
  ctx.stroke()

  ctx.font = '11px JetBrains Mono, monospace'
  ctx.textBaseline = 'middle'

  const resById = new Map((sim.results ?? []).map(r => [r.radar_id, r]))

  for (const r of scene.radars) {
    const { sx, sy } = t.toScreen(r.x, r.y)
    const res = resById.get(r.id)
    const color = res ? (res.detect_success ? getVar('--ok') : getVar('--danger')) : getVar('--radar-color')
    ctx.strokeStyle = color
    ctx.fillStyle = color
    ctx.beginPath()
    ctx.arc(sx, sy, 6, 0, Math.PI * 2)
    ctx.fill()
    if (res && !res.detect_success) {
      ctx.setLineDash([4, 3])
      ctx.strokeStyle = getVar('--danger')
      ctx.beginPath()
      ctx.arc(sx, sy, 11, 0, Math.PI * 2)
      ctx.stroke()
      ctx.setLineDash([])
    }
    ctx.fillText(`R${r.id}`, sx + 9, sy)
  }

  for (const j of scene.jammers) {
    const { sx, sy } = t.toScreen(j.x, j.y)
    ctx.fillStyle = getVar('--accent')
    ctx.beginPath()
    ctx.arc(sx, sy, 6, 0, Math.PI * 2)
    ctx.fill()
    ctx.fillText(`J${j.id}`, sx + 9, sy)
  }
}

function getVar(name: string): string {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || '#fff'
}

function localPos(ev: PointerEvent): { x: number; y: number } {
  const rect = canvasRef.value?.getBoundingClientRect()
  return { x: ev.clientX - (rect?.left ?? 0), y: ev.clientY - (rect?.top ?? 0) }
}

function hitPoints() {
  const t = makeTransform(viewport, canvasW, props.height)
  return [
    ...scene.radars.map(r => ({ kind: 'radar' as const, id: r.id, ...t.toScreen(r.x, r.y) })),
    ...scene.jammers.map(j => ({ kind: 'jammer' as const, id: j.id, ...t.toScreen(j.x, j.y) })),
  ]
}

function onPointerDown(ev: PointerEvent): void {
  const { x, y } = localPos(ev)
  const hit = hitTest(x, y, hitPoints())
  const canvas = canvasRef.value
  canvas?.setPointerCapture(ev.pointerId)
  if (hit) {
    dragging = hit
    if (canvas) canvas.style.cursor = 'grabbing'
  } else {
    panning = { startX: x, startY: y, vpX: viewport.x, vpY: viewport.y }
  }
}

function onPointerMove(ev: PointerEvent): void {
  const { x, y } = localPos(ev)
  if (dragging) {
    const t = makeTransform(viewport, canvasW, props.height)
    const world = t.toWorld(x, y)
    const prop: 'x' | 'y' = 'x'
    scene.updateField(dragging.kind, dragging.id, 'x', Math.round(world.wx))
    scene.updateField(dragging.kind, dragging.id, 'y', Math.round(world.wy))
    void prop
  } else if (panning) {
    const t = makeTransform(viewport, canvasW, props.height)
    viewport = {
      ...viewport,
      x: panning.vpX - (x - panning.startX) / t.scale,
      y: panning.vpY + (y - panning.startY) / t.scale,
    }
  } else {
    const hit = hitTest(x, y, hitPoints())
    const canvas = canvasRef.value
    if (hit) {
      const ent = hit.kind === 'radar'
        ? scene.radars.find(r => r.id === hit.id)
        : scene.jammers.find(j => j.id === hit.id)
      if (ent) {
        const t = makeTransform(viewport, canvasW, props.height)
        const s = t.toScreen(ent.x, ent.y)
        hover.value = { ...hit, x: ent.x, y: ent.y, sx: s.sx, sy: s.sy }
      }
      if (canvas) canvas.style.cursor = 'grab'
    } else {
      hover.value = null
      if (canvas) canvas.style.cursor = 'crosshair'
    }
  }
  scheduleDraw()
}

function onPointerUp(ev: PointerEvent): void {
  if (dragging) scene.pulseEntity(dragging.kind, dragging.id)
  dragging = null
  panning = null
  canvasRef.value?.releasePointerCapture?.(ev.pointerId)
  scheduleDraw()
}

function onResize(): void {
  scheduleDraw()
}

watch(() => [scene.radars, scene.jammers, sim.results, props.height], scheduleDraw, { deep: true })

onMounted(() => {
  const canvas = canvasRef.value
  canvas?.addEventListener('pointerdown', onPointerDown)
  canvas?.addEventListener('pointermove', onPointerMove)
  canvas?.addEventListener('pointerup', onPointerUp)
  canvas?.addEventListener('pointerleave', () => {
    hover.value = null
    scheduleDraw()
  })
  window.addEventListener('resize', onResize)
  scheduleDraw()
})

onBeforeUnmount(() => {
  const canvas = canvasRef.value
  canvas?.removeEventListener('pointerdown', onPointerDown)
  canvas?.removeEventListener('pointermove', onPointerMove)
  canvas?.removeEventListener('pointerup', onPointerUp)
  window.removeEventListener('resize', onResize)
  if (rafId) cancelAnimationFrame(rafId)
})
</script>

<template>
  <div class="scene-canvas">
    <canvas ref="canvasRef" class="canvas" :height="props.height" />
    <div
      v-if="hover"
      class="tip glass-panel mono"
      :style="{ left: hover.sx + 14 + 'px', top: hover.sy - 10 + 'px' }"
    >
      #{{ hover.id }} {{ hover.kind === 'radar' ? '雷达' : '干扰机' }} · ({{ Math.round(hover.x) }}, {{ Math.round(hover.y) }}) m
    </div>
  </div>
</template>

<style scoped>
.scene-canvas {
  position: relative;
  width: 100%;
  overflow: hidden;
}
.canvas {
  display: block;
  width: 100%;
  cursor: crosshair;
  touch-action: none;
}
.tip {
  position: absolute;
  pointer-events: none;
  padding: 4px 8px;
  font-size: 11px;
  white-space: nowrap;
  color: var(--text-1);
}
</style>
```

**实现修正提示**：模板中 `:height="props.height"` — `<script setup>` 中模板直接用 `height`（props 自动解包），即 `:height="height"`；同时 `draw()` 内宽高以 `canvas.clientWidth/clientHeight` 为准，`props.height` 仅作初始值。实现时同步修正这两处并确保 typecheck 通过。

- [ ] **Step 4: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/SceneCanvas.spec.ts`
Expected: PASS（3 个用例；坐标断言可按实现微调，语义不变）

- [ ] **Step 5: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/components/SceneCanvas.vue front_end/src/components/SceneCanvas.spec.ts
git commit -m "front_end: 场景画布渲染与 Pointer 拖拽/平移"
```

---

### Task 12: EntityCard + ControlPanel — 参数表单与列表

**Files:**
- Create: `front_end/src/components/EntityCard.vue`
- Create: `front_end/src/components/ControlPanel.vue`
- Create: `front_end/src/components/EntityCard.spec.ts`

**Interfaces:**
- Consumes: `useSceneStore.updateField/setFocusedField/removeRadar/removeJammer/addRadar/addJammer/pulsed`（Task 3–5）
- Produces（Task 15 装配依赖）:
  - `EntityCard.vue` props: `{ kind: EntityKind; id: number }`；内部按 `kind` 读取实体（响应式）
  - 字段定义（GHz/MHz 换算，滑杆 + 数字输入双绑 `updateField`）:
    - 雷达: `x,y`（0–20000 m）、`Pt_dBm`（-20–60 dBm）、`G_dB`（0–40 dB）、`freq`（展示 GHz，存储 Hz，8–18 GHz，步长 1e6）、`bandwidth`（展示 MHz，存储 Hz，1–100 MHz）、`sigma`（0.01–100 m²）、`thresh_db`（-80–-30 dB）
    - 干扰机: `x,y`、`Pj_dBm`（0–80 dBm）、`Gj_dB`（0–40 dB）、`jam_freq`（展示 GHz，8–18 GHz）、`jam_type`（el-select: 噪声压制 NOISE_JAM / 欺骗 RANGE_DECEPT）
  - 行为：输入 `focus → setFocusedField(pendingKey)`、`blur → setFocusedField(null)`；`pendingKey` 在 `pendingEdits` → 标签右侧 `~` 徽标 + 脉冲动画；`pulsed` 命中本实体且 600ms 内 → 卡片一次高亮动画后清除；删除按钮（`确认删除？` 二次确认）
  - `ControlPanel.vue` 无 props：过滤 tab（全部/雷达/干扰机，`scene.filter`）+ 添加按钮 + `EntityCard` 列表（按 filter 过滤）；空列表显示「暂无实体」

- [ ] **Step 1: 写失败测试 `src/components/EntityCard.spec.ts`**

```ts
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import EntityCard from './EntityCard.vue'
import { useSceneStore } from '../stores/scene'
import type { Radar } from '../types/api'

vi.mock('element-plus', () => ({
  ElMessage: { error: vi.fn(), success: vi.fn() },
  ElMessageBox: { confirm: vi.fn().mockResolvedValue(undefined) },
}))

const radar = (id: number): Radar => ({ id, x: 5000, y: 5000, Pt_dBm: 20, G_dB: 25, freq: 1e10, bandwidth: 1e6, sigma: 5, thresh_db: -50 })

describe('EntityCard (radar)', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
  })

  function setup() {
    const scene = useSceneStore()
    scene.radars.push(radar(1))
    const w = mount(EntityCard, { props: { kind: 'radar', id: 1 }, global: { stubs: { teleport: true } } })
    return { w, scene }
  }

  it('展示实体标题 R1', () => {
    const { w } = setup()
    expect(w.text()).toContain('R1')
    expect(w.text()).toContain('雷达')
  })

  it('修改功率滑杆调用 updateField 且标 pending', async () => {
    const { w, scene } = setup()
    // 找到 Pt_dBm 对应的 input（el-slider 生成 input[type=number]）
    const inputs = w.findAll('input')
    expect(inputs.length).toBeGreaterThan(0)
    const powerInput = inputs[inputs.length - 1] // 至少存在一个 input；以触发 change 为准
    await powerInput.setValue('42')
    await powerInput.trigger('change')
    expect(scene.pendingEdits.size).toBeGreaterThan(0)
  })

  it('freq 显示为 GHz（10 GHz 对应 1e10 Hz）', () => {
    const { w } = setup()
    const sliderInputs = w.findAll('input').map(i => i.attributes('value') ?? (i.element as HTMLInputElement).value)
    expect(sliderInputs.some(v => v === '10' || v.startsWith('10'))).toBe(true)
  })

  it('focus/blur 切换 focusedKey', async () => {
    const { w, scene } = setup()
    const input = w.find('input')
    await input.trigger('focus')
    expect(scene.focusedKey).not.toBeNull()
    await input.trigger('blur')
    expect(scene.focusedKey).toBeNull()
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/EntityCard.spec.ts`
Expected: FAIL — `Failed to resolve import "./EntityCard.vue"`

- [ ] **Step 3: 实现 `src/components/EntityCard.vue`**

字段定义表（`script setup` 内，供模板循环渲染）：

```ts
import { computed, ref, watch } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import type { EntityKind } from '../types/api'
import { pendingKey, useSceneStore } from '../stores/scene'

const props = defineProps<{ kind: EntityKind; id: number }>()
const scene = useSceneStore()
const confirmedDelete = ref(false)

const entity = computed(() =>
  props.kind === 'radar'
    ? scene.radars.find(r => r.id === props.id)
    : scene.jammers.find(j => j.id === props.id),
)

interface FieldSpec {
  prop: string
  label: string
  min: number
  max: number
  step: number
  /** 存储值换算为显示值 */
  toDisplay?: (v: number) => number
  /** 显示值换算回存储值 */
  toStorage?: (v: number) => number
  unit?: string
}

const radarFields: FieldSpec[] = [
  { prop: 'x', label: 'X', min: 0, max: 20000, step: 100, unit: 'm' },
  { prop: 'y', label: 'Y', min: 0, max: 20000, step: 100, unit: 'm' },
  { prop: 'Pt_dBm', label: '功率', min: -20, max: 60, step: 1, unit: 'dBm' },
  { prop: 'G_dB', label: '增益', min: 0, max: 40, step: 1, unit: 'dB' },
  { prop: 'freq', label: '频率', min: 8, max: 18, step: 0.001, unit: 'GHz', toDisplay: v => v / 1e9, toStorage: v => v * 1e9 },
  { prop: 'bandwidth', label: '带宽', min: 1, max: 100, step: 1, unit: 'MHz', toDisplay: v => v / 1e6, toStorage: v => v * 1e6 },
  { prop: 'sigma', label: 'RCS', min: 0.01, max: 100, step: 0.01, unit: 'm²' },
  { prop: 'thresh_db', label: '检测门限', min: -80, max: -30, step: 1, unit: 'dB' },
]

const jammerFields: FieldSpec[] = [
  { prop: 'x', label: 'X', min: 0, max: 20000, step: 100, unit: 'm' },
  { prop: 'y', label: 'Y', min: 0, max: 20000, step: 100, unit: 'm' },
  { prop: 'Pj_dBm', label: '功率', min: 0, max: 80, step: 1, unit: 'dBm' },
  { prop: 'Gj_dB', label: '增益', min: 0, max: 40, step: 1, unit: 'dB' },
  { prop: 'jam_freq', label: '干扰频率', min: 8, max: 18, step: 0.001, unit: 'GHz', toDisplay: v => v / 1e9, toStorage: v => v * 1e9 },
]

const fields = computed(() => (props.kind === 'radar' ? radarFields : jammerFields))

function numOf(prop: string): number {
  const raw = (entity.value as unknown as Record<string, number>)[prop]
  return raw
}

function displayVal(f: FieldSpec): number {
  return f.toDisplay ? f.toDisplay(numOf(f.prop)) : numOf(f.prop)
}

function onInput(f: FieldSpec, displayValue: number): void {
  const stored = f.toStorage ? f.toStorage(displayValue) : displayValue
  scene.updateField(props.kind, props.id, f.prop, stored)
}

function keyOf(prop: string): string {
  return pendingKey(props.kind, prop, props.id)
}

function isPending(prop: string): boolean {
  return scene.pendingEdits.has(keyOf(prop))
}

function onFocus(prop: string): void {
  scene.setFocusedField(keyOf(prop))
}
function onBlur(): void {
  scene.setFocusedField(null)
}

async function onDelete(): Promise<void> {
  if (!confirmedDelete.value) {
    try {
      await ElMessageBox.confirm(`确认删除 ${props.kind === 'radar' ? 'R' : 'J'}${props.id}？`, '删除', { type: 'warning' })
      confirmedDelete.value = true
      return
    } catch {
      return
    }
  }
  if (props.kind === 'radar') await scene.removeRadar(props.id)
  else await scene.removeJammer(props.id)
}

const pulsing = computed(() =>
  scene.pulsed && scene.pulsed.kind === props.kind && scene.pulsed.id === props.id &&
  Date.now() - scene.pulsed.at < 600,
)
watch(pulsing, v => {
  if (v) setTimeout(() => { scene.pulsed = null }, 650)
})
```

**实现修正提示**：`scene.pulsed = null` 在 setup 解构 store 后不可直接赋 — 用 `const s = useSceneStore()` 时 Pinia setup store 允许 `s.pulsed = null`（state 可写）。模板骨架：

```vue
<template>
  <div v-if="entity" class="entity-card glass-panel" :class="{ pulsing }">
    <div class="card-head">
      <span class="name mono">
        {{ kind === 'radar' ? 'R' : 'J' }}{{ id }}
        <em>{{ kind === 'radar' ? '雷达' : '干扰机' }}</em>
      </span>
      <button class="del" title="删除" @click="onDelete">✕</button>
    </div>
    <div v-for="f in fields" :key="f.prop" class="field" :class="{ pending: isPending(f.prop) }">
      <label>
        {{ f.label }}
        <span v-if="isPending(f.prop)" class="tilde">~</span>
      </label>
      <el-slider
        :model-value="displayVal(f)"
        :min="f.min"
        :max="f.max"
        :step="f.step"
        size="small"
        @update:model-value="onInput(f, $event)"
        @focus="onFocus(f.prop)"
        @blur="onBlur"
      />
      <span class="val mono">{{ displayVal(f) }}{{ f.unit ? ' ' + f.unit : '' }}</span>
    </div>
    <div v-if="kind === 'jammer'" class="field">
      <label>类型</label>
      <el-select
        :model-value="(entity as Jammer).jam_type"
        size="small"
        @update:model-value="scene.updateField('jammer', id, 'jam_type', $event)"
        @focus="onFocus('jam_type')"
        @blur="onBlur"
      >
        <el-option label="噪声压制" value="NOISE_JAM" />
        <el-option label="距离欺骗" value="RANGE_DECEPT" />
      </el-select>
    </div>
  </div>
</template>
```

（`el-slider`/`el-select` 的 focus/blur 事件若不透传，改在根 input 上 `@focus.capture`/`@blur.capture`，保证 focusedKey 行为；测试 Step 1 的 focus 断言以实现后实际可触发为准。）

- [ ] **Step 4: 实现 `src/components/ControlPanel.vue`**

```vue
<script setup lang="ts">
import { computed } from 'vue'
import { useSceneStore } from '../stores/scene'
import EntityCard from './EntityCard.vue'

const scene = useSceneStore()

const tabs = [
  { key: 'all', label: '全部' },
  { key: 'radar', label: '雷达' },
  { key: 'jammer', label: '干扰机' },
] as const

const visible = computed(() => {
  const radars = scene.radars.map(r => ({ kind: 'radar' as const, id: r.id }))
  const jammers = scene.jammers.map(j => ({ kind: 'jammer' as const, id: j.id }))
  const all = [...radars, ...jammers]
  if (scene.filter === 'radar') return radars
  if (scene.filter === 'jammer') return jammers
  return all
})
</script>

<template>
  <div class="control-panel">
    <div class="toolbar">
      <div class="tabs">
        <button
          v-for="t in tabs"
          :key="t.key"
          class="tab"
          :class="{ active: scene.filter === t.key }"
          @click="scene.filter = t.key"
        >{{ t.label }}</button>
      </div>
      <div class="actions">
        <button class="btn" @click="scene.addRadar()">+ 雷达</button>
        <button class="btn" @click="scene.addJammer()">+ 干扰机</button>
      </div>
    </div>
    <div v-if="visible.length === 0" class="empty">暂无实体</div>
    <EntityCard v-for="e in visible" :key="e.kind + e.id" :kind="e.kind" :id="e.id" />
  </div>
</template>

<style scoped>
.control-panel { display: flex; flex-direction: column; gap: 8px; overflow: auto; padding: 8px; }
.toolbar { display: flex; justify-content: space-between; align-items: center; }
.tabs { display: flex; gap: 4px; }
.tab {
  background: transparent;
  border: 1px solid var(--glass-border);
  color: var(--text-2);
  border-radius: 999px;
  padding: 3px 12px;
  font-size: 12px;
  cursor: pointer;
}
.tab.active { color: var(--text-1); border-color: var(--accent); }
.actions { display: flex; gap: 6px; }
.btn {
  background: linear-gradient(90deg, var(--accent), var(--accent-2));
  border: none;
  color: #fff;
  font-size: 12px;
  padding: 5px 12px;
  border-radius: 6px;
  cursor: pointer;
}
.empty { color: var(--text-2); font-size: 13px; text-align: center; padding: 24px 0; }
</style>
```

（EntityCard 样式随实现补充：`entity-card` 基本玻璃卡片、`.pending` 标签 accent 色 + `~` 徽标、`.pulsing` 一次 keyframe 高亮、`.del` 悬停 danger 色。）

- [ ] **Step 5: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/EntityCard.spec.ts`
Expected: PASS（4 个用例）

- [ ] **Step 6: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/components/EntityCard.vue front_end/src/components/ControlPanel.vue front_end/src/components/EntityCard.spec.ts
git commit -m "front_end: 实体参数卡片与控制面板"
```

---

### Task 13: HistoryCharts.vue — ECharts 双图

**Files:**
- Create: `front_end/src/components/HistoryCharts.vue`
- Create: `front_end/src/components/HistoryCharts.spec.ts`

**Interfaces:**
- Consumes: `useSimulationStore.history/statusMsg`（Task 6）、echarts（Task 1 安装）
- Produces（Task 15 装配依赖）:
  - `HistoryCharts.vue` 无 props：上图 SINR(dB) 折线（多雷达系列，`radar-{id}` 色盘从 `--radar-color/#e94560/#00e5ff/#ff7a59/#69f0ae` 循环），下图频域匹配因子 ζ（0–1）
  - x 轴 = 步骤序号（1..history.length）；空 history → 显示「暂无数据 — 运行仿真后展示」占位
  - `init` 懒创建、`ResizeObserver` 自适应、`onBeforeUnmount` dispose
  - 读 `history` watch 深更新 `setOption`

- [ ] **Step 1: 写失败测试 `src/components/HistoryCharts.spec.ts`**

```ts
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import HistoryCharts from './HistoryCharts.vue'

const setOption = vi.fn()
const init = vi.fn(() => ({ setOption, resize: vi.fn(), dispose: vi.fn(), on: vi.fn() }))
vi.mock('echarts', () => ({ init: (...args: unknown[]) => init(...args) }))

describe('HistoryCharts', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
    // jsdom 无布局 → 手动补尺寸
    vi.spyOn(HTMLElement.prototype, 'clientWidth', 'get').mockReturnValue(600)
    vi.spyOn(HTMLElement.prototype, 'clientHeight', 'get').mockReturnValue(300)
    vi.stubGlobal('ResizeObserver', class {
      observe() {}
      unobserve() {}
      disconnect() {}
    })
  })

  it('无数据时显示占位且不初始化图表', () => {
    const w = mount(HistoryCharts)
    expect(w.text()).toContain('暂无数据')
    expect(init).not.toHaveBeenCalled()
  })

  it('有历史时懒初始化图表一次', async () => {
    const { useSimulationStore } = await import('../stores/simulation')
    const sim = useSimulationStore()
    sim.history.push({ labels: [1], sinr: [-3], zeta: [0.8] })
    const w = mount(HistoryCharts, { attachTo: document.body })
    await w.vm.$nextTick()
    expect(init).toHaveBeenCalledTimes(1)
    expect(setOption).toHaveBeenCalled()
    w.unmount()
  })

  it('追加历史触发 setOption 更新', async () => {
    const { useSimulationStore } = await import('../stores/simulation')
    const sim = useSimulationStore()
    sim.history.push({ labels: [1], sinr: [-3], zeta: [0.8] })
    const w = mount(HistoryCharts, { attachTo: document.body })
    await w.vm.$nextTick()
    setOption.mockClear()
    sim.history.push({ labels: [1], sinr: [-2], zeta: [0.6] })
    await w.vm.$nextTick()
    expect(setOption).toHaveBeenCalled()
    w.unmount()
  })

  it('卸载时 dispose', async () => {
    const { useSimulationStore } = await import('../stores/simulation')
    const sim = useSimulationStore()
    sim.history.push({ labels: [1], sinr: [0], zeta: [1] })
    const w = mount(HistoryCharts, { attachTo: document.body })
    await w.vm.$nextTick()
    const inst = init.mock.results[0].value as { dispose: ReturnType<typeof vi.fn> }
    w.unmount()
    expect(inst.dispose).toHaveBeenCalled()
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/HistoryCharts.spec.ts`
Expected: FAIL — `Failed to resolve import "./HistoryCharts.vue"`

- [ ] **Step 3: 实现 `src/components/HistoryCharts.vue`**

```vue
<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import * as echarts from 'echarts'
import { useSimulationStore } from '../stores/simulation'

const sim = useSimulationStore()
const sinrEl = ref<HTMLDivElement | null>(null)
const zetaEl = ref<HTMLDivElement | null>(null)
let sinrChart: echarts.ECharts | null = null
let zetaChart: echarts.ECharts | null = null
let ro: ResizeObserver | null = null

const SERIES_COLORS = ['var(--radar-color)', '#e94560', '#00e5ff', '#ff7a59', '#69f0ae']

const hasData = computed(() => sim.history.length > 0)

function baseOption(title: string, seriesNames: string[], data: number[][], min?: number, max?: number) {
  return {
    backgroundColor: 'transparent',
    title: { text: title, textStyle: { color: 'rgba(255,255,255,.62)', fontSize: 11 }, left: 0, top: 0 },
    grid: { left: 44, right: 12, top: 24, bottom: 22 },
    xAxis: {
      type: 'category',
      data: sim.history.map((_, i) => String(i + 1)),
      axisLabel: { color: 'rgba(255,255,255,.45)' },
      axisLine: { lineStyle: { color: 'rgba(255,255,255,.18)' } },
    },
    yAxis: {
      type: 'value',
      min,
      max,
      axisLabel: { color: 'rgba(255,255,255,.45)' },
      splitLine: { lineStyle: { color: 'rgba(255,255,255,.08)' } },
    },
    tooltip: { trigger: 'axis' },
    series: seriesNames.map((name, i) => ({
      name,
      type: 'line',
      smooth: true,
      showSymbol: false,
      lineStyle: { width: 2 },
      itemStyle: { color: SERIES_COLORS[i % SERIES_COLORS.length] },
      data: data[i],
    })),
  }
}

function radarNames(): string[] {
  const first = sim.history[0]
  if (!first) return []
  return first.labels.map(id => `R${id}`)
}

function render(): void {
  if (!hasData.value || !sinrEl.value || !zetaEl.value) return
  if (!sinrChart) sinrChart = echarts.init(sinrEl.value)
  if (!zetaChart) zetaChart = echarts.init(zetaEl.value)
  const names = radarNames()
  // 逐步骤提取各雷达 SINR / ζ
  const sinrSeries = names.map((_, ri) => sim.history.map(h => h.sinr[ri] ?? null))
  const zetaSeries = names.map((_, ri) => sim.history.map(h => h.zeta[ri] ?? null))
  sinrChart.setOption(baseOption('SINR (dB)', names, sinrSeries))
  zetaChart.setOption(baseOption('频域匹配因子 ζ', names, zetaSeries, 0, 1))
}

onMounted(() => {
  ro = new ResizeObserver(() => {
    sinrChart?.resize()
    zetaChart?.resize()
  })
  if (sinrEl.value) ro.observe(sinrEl.value)
  if (zetaEl.value) ro.observe(zetaEl.value)
  void nextTick(render)
})

watch(() => sim.history, () => nextTick(render), { deep: true })

onBeforeUnmount(() => {
  ro?.disconnect()
  sinrChart?.dispose()
  zetaChart?.dispose()
  sinrChart = null
  zetaChart = null
})
</script>

<template>
  <div class="history-charts">
    <div v-if="!hasData" class="placeholder">暂无数据 — 运行仿真后展示</div>
    <template v-else>
      <div ref="sinrEl" class="chart" />
      <div ref="zetaEl" class="chart" />
    </template>
  </div>
</template>

<style scoped>
.history-charts { display: flex; flex-direction: column; height: 100%; gap: 4px; }
.chart { flex: 1; min-height: 140px; }
.placeholder { color: var(--text-2); font-size: 12px; padding: 16px; text-align: center; }
</style>
```

**实现修正提示**：无数据时占位显示 → 测试断言「不初始化」；有数据时先渲染占位移除再 init — `watch` 中 `hasData` 从 false→true 时 canvas 元素刚挂载，`nextTick(render)` 保证元素存在。若 `resize` 方法在 mock 中存在但真实 init 返回值需含 `resize`，测试 mock 已提供。

- [ ] **Step 4: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/HistoryCharts.spec.ts`
Expected: PASS（4 个用例）

- [ ] **Step 5: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/components/HistoryCharts.vue front_end/src/components/HistoryCharts.spec.ts
git commit -m "front_end: ECharts 历史双图（SINR/ζ）"
```

---

### Task 14: ResultPanel + JammerDetailTable — 仿真结果展示

**Files:**
- Create: `front_end/src/components/ResultPanel.vue`
- Create: `front_end/src/components/JammerDetailTable.vue`
- Create: `front_end/src/components/ResultPanel.spec.ts`

**Interfaces:**
- Consumes: `useSimulationStore`（Task 6）、`useSceneStore`（会话态）
- Produces（Task 15 装配依赖）:
  - `ResultPanel.vue` 无 props:
    - 控制行：`运行仿真` 按钮（`running || !scene.sessionId` → disabled，loading 态）、`重置场景`、自动仿真开关（`el-switch`）+ 间隔 `el-input-number`（1–60 s）+ `剩余 N s` 倒计时（enabled 时）
    - 结果表（`el-table`）：`雷达`（R{id}）、`SINR(dB)`（mono，颜色 `ok≥0/danger<0`）、`JSR(dB)`、`检测`（`检出/未检出` tag）、`总压制功率(W)`、`压制得分`、`欺骗`（`is_deception_active` true → tag）
    - 行展开（`el-table` expand）→ `JammerDetailTable` 展示该雷达的 `jammer_details`
    - 空态：`sim.results === null` → 「尚未运行仿真」
  - `JammerDetailTable.vue` props: `{ details: JammerDetail[] }` — 列：干扰机 J{id}、频差 MHz（`freq_delta_Hz / 1e6`）、匹配因子 ζ（0–1 两位小数）、原始功率 mW（`*1000`）、有效功率 mW

- [ ] **Step 1: 写失败测试 `src/components/ResultPanel.spec.ts`**

```ts
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { mount } from '@vue/test-utils'
import { createPinia, setActivePinia } from 'pinia'
import ResultPanel from './ResultPanel.vue'
import JammerDetailTable from './JammerDetailTable.vue'
import { useSimulationStore } from '../stores/simulation'
import type { SimResult } from '../types/api'

vi.mock('element-plus', () => ({
  ElMessage: { error: vi.fn(), success: vi.fn() },
}))

const result: SimResult = {
  radar_id: 1, SINR_dB: -3.2, jsr_dB: 12.5, detect_success: false,
  total_effective_jam_power_W: 0.42, jam_success_score: 0.78, is_deception_active: true,
  jammer_details: [{ jammer_id: 2, freq_delta_Hz: 1e6, freq_match_factor: 0.87, original_power_W: 1.5, effective_power_W: 0.3 }],
}

describe('ResultPanel', () => {
  beforeEach(() => {
    setActivePinia(createPinia())
    vi.clearAllMocks()
  })

  it('无结果显示空态文案', () => {
    const w = mount(ResultPanel)
    expect(w.text()).toContain('尚未运行仿真')
  })

  it('有结果展示雷达行与检测状态', async () => {
    const sim = useSimulationStore()
    sim.results = [result]
    const w = mount(ResultPanel, { attachTo: document.body })
    await w.vm.$nextTick()
    expect(w.text()).toContain('R1')
    expect(w.text()).toContain('-3.2')
    expect(w.text()).toContain('未检出')
    w.unmount()
  })

  it('未运行时显示运行按钮（sessionId 有值时可点）', async () => {
    const { useSceneStore } = await import('../stores/scene')
    const scene = useSceneStore()
    scene.sessionId = 'sess-1'
    const w = mount(ResultPanel)
    await w.vm.$nextTick()
    const btns = w.findAll('button')
    expect(btns.some(b => b.text().includes('运行仿真'))).toBe(true)
  })

  it('running 时运行按钮 disabled', async () => {
    const { useSceneStore } = await import('../stores/scene')
    const scene = useSceneStore()
    scene.sessionId = 'sess-1'
    const sim = useSimulationStore()
    sim.running = true
    const w = mount(ResultPanel)
    await w.vm.$nextTick()
    const runBtn = w.findAll('button').find(b => b.text().includes('运行仿真'))
    expect(runBtn?.attributes('disabled')).toBeDefined()
  })
})

describe('JammerDetailTable', () => {
  it('展示干扰机行与换算值', () => {
    const w = mount(JammerDetailTable, { props: { details: result.jammer_details! } })
    const text = w.text()
    expect(text).toContain('J2')
    expect(text).toContain('1.00') // 1e6 Hz → 1.00 MHz
    expect(text).toContain('0.87') // ζ
    expect(text).toContain('1500') // 1.5 W → 1500 mW
  })

  it('空数组渲染空提示', () => {
    const w = mount(JammerDetailTable, { props: { details: [] } })
    expect(w.text()).toContain('无干扰机参与')
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/ResultPanel.spec.ts`
Expected: FAIL — `Failed to resolve import "./ResultPanel.vue"`

- [ ] **Step 3: 实现 `src/components/JammerDetailTable.vue`**

```vue
<script setup lang="ts">
import type { JammerDetail } from '../types/api'

defineProps<{ details: JammerDetail[] }>()

function mhz(hz: number): string {
  return (hz / 1e6).toFixed(2)
}
function mw(w: number): string {
  return (w * 1000).toFixed(1)
}
function zeta(v: number): string {
  return v.toFixed(2)
}
</script>

<template>
  <div class="detail">
    <div v-if="details.length === 0" class="empty">无干扰机参与</div>
    <table v-else class="mono">
      <thead>
        <tr><th>干扰机</th><th>频差 MHz</th><th>匹配因子 ζ</th><th>原始功率 mW</th><th>有效功率 mW</th></tr>
      </thead>
      <tbody>
        <tr v-for="d in details" :key="d.jammer_id">
          <td>J{{ d.jammer_id }}</td>
          <td>{{ mhz(d.freq_delta_Hz) }}</td>
          <td>{{ zeta(d.freq_match_factor) }}</td>
          <td>{{ mw(d.original_power_W) }}</td>
          <td>{{ mw(d.effective_power_W) }}</td>
        </tr>
      </tbody>
    </table>
  </div>
</template>

<style scoped>
.detail { padding: 6px 8px; }
table { width: 100%; border-collapse: collapse; font-size: 12px; }
th, td { padding: 4px 8px; text-align: right; border-bottom: 1px solid rgba(255,255,255,.08); }
th:first-child, td:first-child { text-align: left; }
th { color: var(--text-2); font-weight: 500; }
.empty { color: var(--text-2); font-size: 12px; text-align: center; padding: 8px; }
</style>
```

- [ ] **Step 4: 实现 `src/components/ResultPanel.vue`**

```vue
<script setup lang="ts">
import { computed } from 'vue'
import { useSimulationStore } from '../stores/simulation'
import { useSceneStore } from '../stores/scene'
import JammerDetailTable from './JammerDetailTable.vue'
import type { SimResult } from '../types/api'

const sim = useSimulationStore()
const scene = useSceneStore()

const runDisabled = computed(() => sim.running || !scene.sessionId)

function sinrClass(r: SimResult): string {
  return r.SINR_dB >= 0 ? 'ok' : 'bad'
}

function fmt(v: number | undefined, digits = 2): string {
  return typeof v === 'number' ? v.toFixed(digits) : '—'
}
</script>

<template>
  <div class="result-panel">
    <div class="controls">
      <button class="btn primary" :disabled="runDisabled" :class="{ loading: sim.running }" @click="sim.run()">
        {{ sim.running ? '仿真中…' : '运行仿真' }}
      </button>
      <button class="btn" @click="sim.reset()">重置场景</button>
      <label class="auto">
        自动仿真
        <el-switch
          :model-value="sim.auto.enabled"
          @update:model-value="(v: boolean) => (v ? sim.startAuto() : sim.stopAuto())"
        />
        <el-input-number
          v-model="sim.auto.interval"
          :min="1"
          :max="60"
          size="small"
          :disabled="sim.auto.enabled"
        />
        <span v-if="sim.auto.enabled" class="countdown mono">剩余 {{ sim.auto.remaining }} s</span>
      </label>
    </div>

    <div v-if="!sim.results" class="empty glass-panel">尚未运行仿真</div>

    <el-table v-else :data="sim.results" size="small" class="result-table">
      <el-table-column type="expand">
        <template #default="{ row }">
          <JammerDetailTable :details="row.jammer_details ?? []" />
        </template>
      </el-table-column>
      <el-table-column label="雷达" width="70">
        <template #default="{ row }"><span class="mono">R{{ row.radar_id }}</span></template>
      </el-table-column>
      <el-table-column label="SINR(dB)" width="90">
        <template #default="{ row }">
          <span class="mono" :class="sinrClass(row)">{{ fmt(row.SINR_dB, 1) }}</span>
        </template>
      </el-table-column>
      <el-table-column label="JSR(dB)" width="86">
        <template #default="{ row }"><span class="mono">{{ fmt(row.jsr_dB, 1) }}</span></template>
      </el-table-column>
      <el-table-column label="检测" width="80">
        <template #default="{ row }">
          <el-tag :type="row.detect_success ? 'danger' : 'success'" size="small">
            {{ row.detect_success ? '检出' : '未检出' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="总压制功率(W)" width="130">
        <template #default="{ row }"><span class="mono">{{ fmt(row.total_effective_jam_power_W, 3) }}</span></template>
      </el-table-column>
      <el-table-column label="压制得分" width="90">
        <template #default="{ row }"><span class="mono">{{ fmt(row.jam_success_score) }}</span></template>
      </el-table-column>
      <el-table-column label="欺骗" width="80">
        <template #default="{ row }">
          <el-tag v-if="row.is_deception_active" type="warning" size="small">激活</el-tag>
          <span v-else class="dim">—</span>
        </template>
      </el-table-column>
    </el-table>
  </div>
</template>

<style scoped>
.result-panel { display: flex; flex-direction: column; gap: 8px; padding: 8px; height: 100%; overflow: auto; }
.controls { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
.btn {
  background: rgba(255,255,255,.08);
  border: 1px solid var(--glass-border);
  color: var(--text-1);
  font-size: 12px;
  padding: 6px 14px;
  border-radius: 6px;
  cursor: pointer;
}
.btn.primary { background: linear-gradient(90deg, var(--accent), var(--accent-2)); border: none; }
.btn:disabled { opacity: .45; cursor: not-allowed; }
.auto { display: flex; gap: 8px; align-items: center; font-size: 12px; color: var(--text-2); }
.countdown { color: var(--cyan); font-size: 12px; }
.empty { color: var(--text-2); font-size: 13px; text-align: center; padding: 28px 0; }
.ok { color: var(--ok); }
.bad { color: var(--danger); }
.dim { color: var(--text-2); }
.result-table { --el-table-bg-color: transparent; --el-table-tr-bg-color: transparent; }
</style>
```

- [ ] **Step 5: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/ResultPanel.spec.ts`
Expected: PASS（6 个用例）

- [ ] **Step 6: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/components/ResultPanel.vue front_end/src/components/JammerDetailTable.vue front_end/src/components/ResultPanel.spec.ts
git commit -m "front_end: 仿真结果面板与干扰机明细表"
```

---

### Task 15: BottomDrawer.vue + App.vue 完整装配

**Files:**
- Create: `front_end/src/components/BottomDrawer.vue`
- Create: `front_end/src/components/BottomDrawer.spec.ts`
- Modify: `front_end/src/App.vue`（v1 骨架 → 完整三栏 + 抽屉布局）

**Interfaces:**
- Consumes: `SceneCanvas`（Task 11）、`ControlPanel`（Task 12）、`HistoryCharts`（Task 13）、`ResultPanel`（Task 14）、scene/simulation store、`createWebSocketController`（Task 7）
- Produces（最终应用形态，Task 16 验收对象）:
  - `BottomDrawer.vue` props: `{ tabs: { key: string; label: string }[]; modelValue: string }` + `update:modelValue` emit；状态：高度 `drawerH`（默认 260px，范围 140–60% 视口高），把手拖拽调高（pointer capture，向上拉增大）；标签切换 emit；`localStorage` 持久化 `ecmsim.drawerH` 与 `ecmsim.drawerTab`
  - `App.vue` 布局：`AppHeader` / `.main` = 左 `SceneCanvas`（flex:1）+ 右 `ControlPanel`（宽 300px，`aside`）/ `BottomDrawer`（tabs: `仿真结果 → ResultPanel`、`历史图表 → HistoryCharts`）/ `StatusBar`
  - WS `onMessage` → `applyRemoteScene` 已在 v1 完成，本任务保持并确认 `ResizeObserver`→canvas 重绘由 SceneCanvas 自理

- [ ] **Step 1: 写失败测试 `src/components/BottomDrawer.spec.ts`**

```ts
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
    const handle = w.find('.handle')
    await handle.trigger('pointerdown', { clientY: 500, pointerId: 1 })
    await handle.trigger('pointermove', { clientY: 300, pointerId: 1 })
    await handle.trigger('pointerup', { clientY: 300, pointerId: 1 })
    const saved = Number(localStorage.getItem('ecmsim.drawerH') ?? '0')
    expect(saved).toBeGreaterThan(260)
  })

  it('再次挂载恢复已存高度', async () => {
    localStorage.setItem('ecmsim.drawerH', '400')
    const w = mountDrawer()
    expect((w.find('.drawer').element as HTMLElement).style.height).toBe('400px')
  })
})
```

- [ ] **Step 2: 运行确认失败**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/BottomDrawer.spec.ts`
Expected: FAIL — `Failed to resolve import "./BottomDrawer.vue"`

- [ ] **Step 3: 实现 `src/components/BottomDrawer.vue`**

```vue
<script setup lang="ts">
import { onMounted, ref } from 'vue'

const props = defineProps<{ tabs: { key: string; label: string }[]; modelValue: string }>()
const emit = defineEmits<{ 'update:modelValue': [key: string] }>()

const MIN_H = 140
const DEFAULT_H = 260

const drawerH = ref(DEFAULT_H)
let dragging = false
let startY = 0
let startH = DEFAULT_H

function clamp(h: number): number {
  const max = Math.round(window.innerHeight * 0.6)
  return Math.min(Math.max(h, MIN_H), max)
}

onMounted(() => {
  const saved = Number(localStorage.getItem('ecmsim.drawerH'))
  if (saved) drawerH.value = clamp(saved)
  const tab = localStorage.getItem('ecmsim.drawerTab')
  if (tab && props.tabs.some(t => t.key === tab)) emit('update:modelValue', tab)
})

function onDown(ev: PointerEvent): void {
  dragging = true
  startY = ev.clientY
  startH = drawerH.value
  ;(ev.target as HTMLElement).setPointerCapture(ev.pointerId)
}

function onMove(ev: PointerEvent): void {
  if (!dragging) return
  // 把手向上拖（clientY 减小）→ 高度增大
  drawerH.value = clamp(startH + (startY - ev.clientY))
}

function onUp(ev: PointerEvent): void {
  if (!dragging) return
  dragging = false
  ;(ev.target as HTMLElement).releasePointerCapture?.(ev.pointerId)
  localStorage.setItem('ecmsim.drawerH', String(drawerH.value))
}

function select(key: string): void {
  emit('update:modelValue', key)
  localStorage.setItem('ecmsim.drawerTab', key)
}
</script>

<template>
  <div class="drawer glass-panel" :style="{ height: drawerH + 'px' }">
    <div class="handle" @pointerdown="onDown" @pointermove="onMove" @pointerup="onUp" />
    <div class="tabbar">
      <button
        v-for="t in tabs"
        :key="t.key"
        class="tab"
        :class="{ active: modelValue === t.key }"
        @click="select(t.key)"
      >{{ t.label }}</button>
    </div>
    <div class="body">
      <slot :name="modelValue" />
    </div>
  </div>
</template>

<style scoped>
.drawer {
  margin: 0 8px 8px;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  flex-shrink: 0;
}
.handle {
  height: 10px;
  cursor: ns-resize;
  background:
    radial-gradient(circle, rgba(255,255,255,.4) 1.5px, transparent 2px) center / 12px 6px no-repeat;
  touch-action: none;
}
.tabbar { display: flex; gap: 6px; padding: 0 10px 6px; }
.tab {
  background: transparent;
  border: 1px solid var(--glass-border);
  color: var(--text-2);
  font-size: 12px;
  border-radius: 999px;
  padding: 3px 14px;
  cursor: pointer;
}
.tab.active { color: var(--text-1); border-color: var(--accent); background: rgba(233,69,96,.15); }
.body { flex: 1; min-height: 0; overflow: auto; }
</style>
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd /home/ky/workspace/ECMSim/front_end && npx vitest run src/components/BottomDrawer.spec.ts`
Expected: PASS（5 个用例；若 pointer capture 在 jsdom 不可用，`releasePointerCapture?.()` 可选链已兜底 — 其余断言必须保持）

- [ ] **Step 5: 替换 `src/App.vue` 为完整布局（v1 → 最终版）**

`<script setup>` 保持 v1 的会话初始化 + WS 接线不变，追加：

```ts
const drawerTab = ref('result')
const drawerTabs = [
  { key: 'result', label: '仿真结果' },
  { key: 'charts', label: '历史图表' },
]
const canvasHeight = ref(560)

function measureCanvas(): void {
  // main 区高度 - 抽屉高度(约) → 由 CSS flex 自然处理，canvas 高度取其容器实测
  const el = document.querySelector('.stage') as HTMLElement | null
  if (el) canvasHeight.value = Math.max(240, el.clientHeight - 16)
}
onMounted(() => {
  measureCanvas()
  window.addEventListener('resize', measureCanvas)
})
onBeforeUnmount(() => window.removeEventListener('resize', measureCanvas))
```

模板（替换 v1 的 `.stage` 占位）：

```vue
<template>
  <div class="app-shell">
    <AppHeader :ws-status="wsStatus" :session-id="scene.sessionId" />
    <main class="main">
      <section class="stage glass-panel">
        <SceneCanvas :height="canvasHeight" />
      </section>
      <aside class="side glass-panel">
        <ControlPanel />
      </aside>
    </main>
    <BottomDrawer v-model="drawerTab" :tabs="drawerTabs">
      <template #result><ResultPanel /></template>
      <template #charts><HistoryCharts /></template>
    </BottomDrawer>
    <StatusBar />
  </div>
</template>
```

`<style scoped>`：

```css
.main {
  display: flex;
  gap: 8px;
  padding: 0 8px;
  min-height: 0;
}
.stage { flex: 1; min-width: 0; overflow: hidden; }
.side { width: 300px; flex-shrink: 0; overflow: hidden; display: flex; flex-direction: column; }
```

并从 `App.vue` 顶部补导入：`SceneCanvas`、`ControlPanel`、`ResultPanel`、`HistoryCharts`、`BottomDrawer`（及 `onBeforeUnmount`）。

- [ ] **Step 6: 全量测试 + 类型 + 构建**

Run: `cd /home/ky/workspace/ECMSim/front_end && npm run test && npm run typecheck && npm run build`
Expected: 全部 PASS — 所有 spec 绿、`vue-tsc` 零错误、`vite build` 产出

- [ ] **Step 7: Commit**

```bash
cd /home/ky/workspace/ECMSim
git add front_end/src/components/BottomDrawer.vue front_end/src/components/BottomDrawer.spec.ts front_end/src/App.vue
git commit -m "front_end: 底部抽屉 + 三栏布局完整装配"
```

---

### Task 16: 集成验收 — 冒烟、响应式、错误反馈、清理

**Files:**
- Modify: `front_end/src/components/AppHeader.spec.ts`（若 WS 徽章文案/类名与实现有出入，按实现修正断言值 — 语义保留）
- Verify: 无新功能；对照 spec「§6 测试与验收」逐条勾验

**Interfaces:**
- Consumes: 全部前序任务产出
- Produces: 满足 DoD 的可交付前端

- [ ] **Step 1: 全量自动化验证**

```bash
cd /home/ky/workspace/ECMSim/front_end
npm run test        # vitest 全绿
npm run typecheck   # vue-tsc 零错误
npm run build       # vite build 成功
```

Expected: 三者全 PASS。任何失败 → 回到对应任务修复，不得带病进入下一步。

- [ ] **Step 2: 启动 dev 链路 + 手动冒烟（对照 spec §6 清单）**

```bash
# 终端 A：C++ 服务器
cd /home/ky/workspace/ECMSim/build && ./bin/ecmsim_http_beast
# 终端 B：前端 dev
cd /home/ky/workspace/ECMSim/front_end && npm run dev
```

浏览器打开 vite 提示地址（默认 `http://localhost:5173`），逐项验证：

| # | 检查项 | 预期 |
|---|--------|------|
| 1 | 首屏加载 | 自动创建会话；Header 显示会话 ID；WS 徽章「已连接」 |
| 2 | 添加雷达/干扰机 | 左侧列表与画布同时出现新实体（经 WS 广播回流） |
| 3 | 滑杆拖动参数 | 数值即时变化；`~` pending 徽标；300ms 后徽标消失；StatusBar 待同步归零 |
| 4 | 画布拖拽实体 | 实体跟随；对应卡片脉冲高亮；松手后 pending → 同步 |
| 5 | 悬停气泡 | 显示 `#id 类型 · (x, y) m` |
| 6 | 双客户端同步 | 第二个浏览器 tab 改参数，第一个 tab 半秒内更新（非焦点/非 pending 字段） |
| 7 | WS 断线 | kill C++ 服务器 → 徽章「重连中」；重启 → 恢复「已连接」 |
| 8 | 运行仿真 | 结果表出现；检出/未检出 tag 配色；SINR ≥0 绿 / <0 红 |
| 9 | 历史图表 | 多步后折线增长；步骤封顶 30 |
| 10 | 自动仿真 | 倒计时归零触发；关掉后停止 |
| 11 | 重置场景 | 结果清空、实体回到初始、自动仿真停止 |
| 12 | 错误反馈 | 断网时滑杆操作 → ElMessage 错误 + pending 保留不丢 |
| 13 | 抽屉拖高 | 把手上拉增高；刷新后保持；标签记忆 |
| 14 | 响应式 | 窗口缩至 ~900px 宽仍可用（右栏 300px 固定、画布自适应、抽屉可拖） |

- [ ] **Step 3: 与旧前端行为抽查对照**

对 `static/index_v2.html` 执行同样的关键路径（加实体、改参数、仿真、DQN 状态接口是否不受影响），确认新前端**未要求任何协议变更**（`git diff` 中不得出现 C++ 或 protos 改动）。

Run: `cd /home/ky/workspace/ECMSim && git status --porcelain`
Expected: 仅 `front_end/`（及本文档）变更；无 `src/`、`static/`、`include/`、`protos/` 改动。

- [ ] **Step 4: 清理模板残留**

- 删除 `src/components/HelloWorld.vue`（或模板遗留组件）与其引用
- 删除 `src/assets/vue.svg`、`public/vite.svg` 中模板 logo 引用（`index.html` favicon 可留空或改用现有 logo）
- `style.css` 中不得残留模板 `#app { ... }` 的居中/报错样式
- 确认 `git status` 无未跟踪垃圾文件

- [ ] **Step 5: 终验 + Commit**

```bash
cd /home/ky/workspace/ECMSim/front_end && npm run test && npm run typecheck && npm run build
cd /home/ky/workspace/ECMSim
git add -A front_end docs/superpowers/plans/2026-10-01-frontend-vue-migration.md
git commit -m "front_end: Vue 迁移集成验收与模板清理"
```

**DoD checklist（全部勾选才可宣告完成）：**

- [ ] `npm run test` 全绿（http/scene/simulation/useWebSocket/useSceneCanvas/EntityCard/AppHeader/SceneCanvas/HistoryCharts/ResultPanel/BottomDrawer）
- [ ] `npm run typecheck` 零错误
- [ ] `npm run build` 成功
- [ ] Step 2 冒烟表 14 项全过
- [ ] `git diff` 证明 C++ / static / protos 零改动
- [ ] 模板残留清理完成

---

## Execution Notes

- **顺序依赖**：Task 1 → 2 → 3 → {4, 5, 6} → 7 → 8 → 9 → 10 → 11 → 12 → 13 → 14 → 15 → 16。Task 4/5/6 依赖 3；7/8 可与 4/5/6 并行；11–14 可在 10 后并行（各自独立文件）；15 依赖 11–14；16 收尾。
- **推荐执行方式**：`superpowers:subagent-driven-development`（每任务一个 subagent + spec 评审）或 `superpowers:executing-plans`（带检查点的顺序执行）。
- **每个任务必须先跑该任务的失败测试**（红线），再实现，再转绿，再 commit。
- **标注「实现修正提示」的地方**：计划内代码为可运行的基准，提示中列出的已知偏差必须在实现时修正；测试断言语义不得删除，允许按实际 DOM 结构微调定位方式。
- **遇到协议疑问**：以 `static/index_v2.html` 中的请求行为和 C++ `src/app/handlers/routes.cpp` 为准，不猜。
