# ECMSim 前端 Vue 迁移与界面重设计 — 设计文档

- 日期：2026-10-01
- 状态：已评审（分节确认）
- 范围：将 `static/index_v2.html`（912 行单文件）迁移至 `front_end/` Vue 3 + TS + Vite 模板工程，并对界面与交互逻辑适度优化

## 1. 背景与决策摘要

旧前端是单文件 HTML（内联 CSS + 原生 JS + 手写 Canvas），由 C++ 服务器启动时加载并在 `/` 返回（`web_server_beast.cpp:59`）。新前端在 `front_end/` 模板工程（Vue 3.5 + TypeScript + Vite 8）中重建。

| 决策项 | 结论 |
|--------|------|
| 构建集成 | **仅开发模式**：vite dev + proxy 转发 `/api`、`/ws` 到 `localhost:8080`；C++ 服务器与 `static/index_v2.html` 不动 |
| 整体布局 | **方案 C**：画布主导 + 底部标签抽屉（顶栏工具条 / 近全屏画布 / 可折叠抽屉） |
| 视觉风格 | **方案 S2**：玻璃拟态 HUD — 深蓝渐变底 + 半透明模糊面板 + 胶囊按钮 |
| 结果与图表摆放 | **方案 E1**：结果表 + 双历史图表 + 明细全部收进抽屉标签页，画布零遮挡 |
| 依赖 | 全家桶：Element Plus（组件）+ ECharts（图表）+ Pinia（状态）+ Vitest（测试） |
| 交互优化范围 | 错误提示与反馈、状态可视化（loading/同步）、画布拖拽体验升级、响应式与面板布局 — 四项全做 |
| 协议 | 完全不变：`/api/*` REST + `WS /ws?session_id=` |

## 2. 架构与数据流

### 2.1 目录结构

```
front_end/src/
├── types/api.ts              # Radar、Jammer、SimResult、JammerDetail、API 响应接口
├── api/http.ts               # fetch 封装：JSON 序列化、失败统一抛 ApiError
├── api/scene.ts              # createSession/getScene/deleteScene/addRadar/…/simulate
├── stores/
│   ├── scene.ts              # 会话、实体 CRUD、参数防抖同步、过滤器
│   └── simulation.ts         # 仿真结果、历史、运行态、自动仿真定时器
├── composables/
│   ├── useWebSocket.ts       # 连接/指数退避重连/消息分发/连接状态
│   └── useSceneCanvas.ts     # 视口换算、命中检测、拖拽状态机（纯函数）
├── components/
│   ├── AppHeader.vue         # 标题、WS 徽章、会话 ID、同步状态徽章
│   ├── SceneCanvas.vue       # 态势画布（手写 canvas，薄壳）
│   ├── BottomDrawer.vue      # 底部抽屉（拖拽调高/折叠/标签页）
│   ├── ControlPanel.vue      # 参数页（过滤 + 实体卡片列表）
│   ├── EntityCard.vue        # 单个雷达/干扰机滑杆表单
│   ├── ResultPanel.vue       # 结果页（运行条 + 结果表 + 图表）
│   ├── HistoryCharts.vue     # ECharts 双图（SINR / ζ）
│   ├── JammerDetailTable.vue # 频域明细表
│   └── StatusBar.vue         # 底部状态栏
├── App.vue                   # 布局骨架
├── style.css                 # 设计令牌 + Element Plus 深色覆写
└── main.ts                   # 挂载 Element Plus、Pinia
```

### 2.2 数据流

```
用户操作（滑杆/拖拽/按钮）
  → 立即更新本地 reactive 状态（乐观）
  → 300ms 防抖 → api/* → POST /api/radar|jammer → 成功后清除 pending

WS 广播（任一端改动触发）
  → useWebSocket 收 scene JSON
  → store.applyRemoteScene()：ID 集合变 → 整体替换重建；未变 → 字段合并
  → 合并时跳过「在途编辑」字段（pending 或输入框聚焦中）

仿真：POST /api/simulate → simulation 存结果 → history 追加（上限 30）
```

**双写冲突解决**：字段级 pending 集合 `pendingEdits: Set<string>`（键形如 `'Pt_dBm_1'`、`'x_j2'`）。本地写入即标记；WS 合并命中 pending 的字段跳过；上送成功后移除。替代旧版 DOM id 拼接的 `markD`/`setVal` 双轨逻辑。

### 2.3 Vite 开发配置

```ts
server: {
  proxy: {
    '/api': 'http://localhost:8080',
    '/ws':  { target: 'ws://localhost:8080', ws: true },
  }
}
```

## 3. 状态管理（Pinia）

### 3.1 `stores/scene.ts`

```ts
state: {
  sessionId: string | null
  radars: Radar[]
  jammers: Jammer[]
  filter: 'all' | 'radar' | 'jammer'
  pendingEdits: Set<string>     // 在途编辑字段
  syncing: boolean
}
getters: { dirtyCount, radarCount, jammerCount }
actions: {
  initSession()      // POST /api/scene → sid → 触发 WS 连接
  fetchScene()       // POST /api/scene {action:config}
  addRadar()/addJammer()/removeRadar(id)/removeJammer(id)
  updateField(kind: 'radar'|'jammer', id: number, prop: string, value: number)
      // 唯一写入口：更新本地 → pending.add(`${prop}_${kind==='jammer'?'j':''}${id}`)
      // → debounce 300ms → flushSync()
  flushSync()        // 按实体分组上送；成功清 pending；失败保留 + ElMessage.error
  applyRemoteScene(msg)  // WS 合并（pending 字段跳过）
}
```

滑杆、数字输入、画布拖拽全部经 `updateField` 这一唯一写入口。

### 3.2 `stores/simulation.ts`

```ts
state: {
  results: SimResult[] | null
  history: { labels:number[]; sinr:number[]; zeta:number[] }[]   // 上限 30
  running: boolean
  statusMsg: string
  auto: { enabled: boolean; interval: number; remaining: number }
}
actions: {
  run()        // running=true → flushSync → POST /api/simulate → 存结果
               // → 追加 history → try/finally 解锁
  reset()      // 停自动仿真 → 删会话 → 新会话 → 清结果与历史
  startAuto()/stopAuto()   // 倒计时驱动 run()；每次 tick 读最新 interval
}
```

**修正旧版隐患**：
1. 旧版 `sim()` 失败路径手动解锁，异常抛出时按钮死锁 → 新版 `try/finally`
2. 旧版自动仿真启动后修改间隔输入不生效 → 新版每 tick 重读 `auto.interval`；`reset()` 必停

### 3.3 `useWebSocket` 状态机

```
connecting → connected → (onclose) → reconnecting → connecting …
指数退避 1s→2s→4s→…→30s 封顶；sessionId 变更时重置
```

连接状态暴露给 `AppHeader` 徽章（绿/黄/红）。

## 4. 界面布局与视觉设计

### 4.1 布局骨架

```
┌─────────────────────────────────────────────────────┐
│ AppHeader: ⚡ ECMSim · WS徽章 · 会话ID · 同步徽章     │
├─────────────────────────────────────────────────────┤
│ SceneCanvas（弹性占满主视口）                          │
├─────────────────────────────────────────────────────┤
│ ▼ BottomDrawer（拖拽调高/一键收起，默认 ~32vh）         │
│  [结果] [参数] [频域明细]                              │
│   结果页 = 运行/重置/自动仿真条 + 结果表 + 双 ECharts    │
└─────────────────────────────────────────────────────┘
│ StatusBar: 步骤数 · 待同步计数 · 最近事件               │
```

- 抽屉把手拖拽调高（上限 ~70vh）、点击折叠/展开；切标签页不丢状态
- < 900px：抽屉默认收起、图表纵排单列、工具条换行
- 触摸设备可拖实体（Pointer Events 统一）

### 4.2 视觉系统（玻璃拟态）

```css
--bg-base:      linear-gradient(135deg, #1a1040 0%, #0d1b2a 50%, #0a1120 100%);
--glass:        rgba(255,255,255,.07) + backdrop-filter: blur(8px);
--glass-border: rgba(255,255,255,.12);
--accent:       #e94560 → 渐变 #e94560/#ff7a59（主操作胶囊）
--cyan:         #00e5ff（次强调：激活标签、数据高亮）
--ok: #69f0ae;  --danger: #ff8a80;
```

- Element Plus `dark` 主题 + `--el-*` 变量覆写融合玻璃风
- 语义色保留：雷达绿、干扰玫红、检测 ✅/❌
- 正文系统字体；**表格/坐标/ID 数据用等宽字体**
- 主操作 = 渐变胶囊；次操作 = 描边/幽灵按钮

## 5. 画布与图表

### 5.1 SceneCanvas（手写 Canvas）

**渲染**（沿用旧版绘制逻辑，重构为纯函数 + 薄壳组件）：
- 视口自适应（实体包围盒 + 15% padding，含原点）、网格、轴标签、雷达-干扰机连线、雷达圆点/干扰机叉号
- HiDPI（devicePixelRatio）保留；`ResizeObserver` 响应容器尺寸变化
- 渲染触发：watch scene + 拖拽帧 rAF 节流重绘

**拖拽升级**：

| 旧版 | 新版 |
|------|------|
| 仅 mousedown/mousemove | Pointer Events 统一鼠标+触摸，`setPointerCapture` 防丢帧 |
| 无 hover 反馈 | hover 高亮 + grab/grabbing 光标 |
| 拖拽无提示 | 跟随坐标气泡 `(x, y) m`，松手淡出 |
| 命中半径固定 18px | 按实体视觉半径命中，触摸热区放大 ~28px |
| 拖完静默同步 | 松手后对应卡片短暂高亮 + 待同步徽章联动 |

**纯函数抽取（可测）**：
```ts
computeViewport(entities, w, h) → { xM, xX, yM, yX, scale }
worldToScreen(p, vp) / screenToWorld(p, vp)
hitTest(screenPt, scene, vp, isTouch) → { kind, id } | null
```

### 5.2 HistoryCharts（ECharts）

- 双实例（SINR / ζ），watch `history`（上限 30）重渲染
- 保留语义：多雷达各一条线（固定调色板）、`connectNulls: false` 断线、系列名图例
- 升级：tooltip、dataZoom、图例开关、ResizeObserver 自适应、暗色玻璃底融合
- 实例生命周期 `onMounted/onUnmounted` 管理

## 6. 错误反馈 · 状态可视化 · 响应式

### 6.1 错误处理

```ts
// api/http.ts — 唯一出口
class ApiError extends Error { code: string; detail?: unknown }
// !resp.ok || !json.success → throw ApiError
```

- 调用点统一 try/catch → `ElMessage.error`；附带后端 `error` 字段
- 全局兜底：`app.config.errorHandler` + `window.onunhandledrejection` → ElMessage
- WS：首连失败红色 message；重连中徽章变黄（不刷屏）；恢复变绿
- 业务失败（session 过期）→ `ElMessageBox.confirm` 引导重建会话

### 6.2 状态可视化

| 状态 | 呈现 |
|------|------|
| 仿真运行中 | 按钮 loading + 禁点；`try/finally` 解锁 |
| 参数待同步 | Header「⚡ n 项待同步」→ 同步中转圈 → ✓ |
| 在途编辑 | 滑杆保持用户值，WS 回写不覆盖（pending + focus 双保护） |
| 自动仿真 | 倒计时进度 + 剩余秒数；间隔即改即生效 |
| WS | Header 圆点徽章三态 |
| 空状态 | 画布中央引导「＋添加雷达/干扰机」；结果页空状态 |

### 6.3 响应式与抽屉

- 断点：≥1200px 抽屉 32vh；900–1200px 28vh；<900px 默认收起 + 图表纵排单列
- 把手 Pointer 拖拽调高（clamp 15vh–70vh），双击收起/展开，高度存 localStorage
- 表格横向滚动 + 固定首列
- 不用 Element Plus 栅格 — 单列弹性结构，纯 flex/grid + CSS 变量

## 7. 测试与验收

### 7.1 Vitest 单测（vitest + @vue/test-utils，jsdom）

| 对象 | 关键断言 |
|------|----------|
| `api/http.ts` | 成功透传 / `success:false` 抛 ApiError / 非 2xx 抛错 / body 序列化 |
| `stores/scene.ts` | CRUD 标记 pending；flushSync 分组上送、成败清留；**applyRemoteScene 合并语义**（整体替换 / 字段合并 / pending 不被覆盖） |
| `stores/simulation.ts` | run() 失败解锁（finally）；history 上限 30；自动仿真倒计时与 reset 停止 |
| `useSceneCanvas` | 坐标正逆变换、包围盒视口、hitTest 命中/未命中/触摸热区 |
| `useWebSocket` | mock WebSocket + fake timers 验证状态机与指数退避 |

- store 测试 `setActivePinia(createPinia())`；`api/*` 用 `vi.mock` 隔离网络
- 不测：ECharts 渲染细节、Element Plus 内部、像素级绘制

### 7.2 验收（Definition of Done）

```bash
npm run test        # vitest run 全绿
npm run typecheck   # vue-tsc -b（模板严格配置）
npm run build       # vite build 通过
npm run dev         # + C++ 服务器手测冒烟：
                    #   建会话 → 实体 CRUD → 滑杆(待同步徽章) → 拖拽(坐标气泡)
                    #   → 运行(结果+图表) → 自动仿真 → 重置 → 断 C++ 看 WS 重连
```

`package.json` 新增脚本：`test`、`test:watch`、`typecheck`。

## 8. 范围外

- C++ 服务器改动（静态目录、入口文件名、协议变更）
- `static/index_v2.html` 的删除或修改
- 生产构建部署流程（后续单独决策）
- DQN 面板（本次不迁移旧版没有的功能）
