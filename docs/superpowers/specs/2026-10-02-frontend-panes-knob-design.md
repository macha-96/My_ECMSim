# 四窗格可拖拽布局 + 旋钮控件 + 坐标负值 Design

**Date:** 2026-10-02
**Status:** brainstorm 批准（§1–§4 逐节确认）
**Scope:** 仅 `front_end/`；C++ 服务器、`static/`、`protos/` 零改动

## 1. 背景

Vue 迁移已完成（分支 `enhance_ui`，96 测试全绿）。本次在迁移成果上做四项界面改造：
用户确认：布局 A（通用分隔条渐进改造）、旋钮=圆周旋转式、坐标=±20000m、数字可键入、分隔=最小尺寸+记忆。

## 2. 布局与分隔（§1）

```
┌─ AppHeader ──────────────────────┐
│ 态势图          │ 参数面板        │  ← SplitBar① 水平（左右拖）
│ (SceneCanvas)   │ (ControlPanel)  │
├─────────────────┴────────────────┤
│ 仿真结果        │ 历史图表        │  ← SplitBar③ 水平（左右拖）
│ (ResultPanel)   │ (HistoryCharts) │
└─────────────────┴────────────────┘
         ↑ SplitBar② 垂直（上下拖）
              StatusBar
```

- 新建通用 `src/components/SplitBar.vue`：props `direction: 'horizontal' | 'vertical'`（horizontal=左右分栏，拖动改变左右宽；vertical=上下分栏，拖动改变上下高）、`minA/minB: number`（像素最小值）、`storageKey: string`（localStorage 记忆键）。pointer 捕获拖拽；超出 min 钳制。
- SplitBar② 复用 BottomDrawer 现有抽屉高度逻辑：默认 260、最小 140、最大 60% 视口高、`ecmsim.drawerH` 键名不变（老用户记忆不丢）。BottomDrawer 外壳（把手+高度）保留，内部标签页改为左右两栏常驻；`drawerTab`/`ecmsim.drawerTab` 逻辑退役（旧键残留无害，不迁移）。
- SplitBar①：参数面板宽默认 300、最小 240、最大 480，键 `ecmsim.sideW`；画布侧最小 320。
- SplitBar③：结果/历史默认 1:1、各最小 280，键 `ecmsim.resultSplit`（存 0~1 比例）。
- App.vue 改用嵌套分隔：`.main` 内 ①；`.main` 与抽屉之间 ②（BottomDrawer 把手即 ② 的拖拽柄，或把 ② 并入 BottomDrawer 顶部把手——实现时二选一，行为一致）；抽屉内 ③。

## 3. 旋钮控件（§2）

- 新建 `src/components/KnobControl.vue`，替换 EntityCard 全部数值滑杆（雷达 8 字段、干扰机 5 字段）；`jam_type` 下拉保留。
- 表盘：SVG，角度域 -135°~+135°（270°，底部缺口）。pointer 按下后以旋钮中心为圆心计算角度→线性映射 min/max：`v = (angle+135)/270*(max-min)+min`，按 step 取整。
- 中央读数显示显示值（经 toDisplay 换算，如 GHz）；点击读数切换为数字输入框，回车/失焦提交（toStorage 后调 `updateField`）；滚轮按 step 微调；`tabindex=0`，方向键按 step 调整。
- 同步保护复用现有机制：旋转中每次变化调 `updateField`（pending + 300ms 防抖）；输入框 focus/blur 调 `setFocusedField`；pending 时表盘描边 `--accent` + `~` 徽标（沿用 EntityCard 样式语言）。
- 范围沿用各字段现有 min/max/step（坐标见 §4）。

## 4. 坐标负值（§3）

- EntityCard 雷达/干扰机 `x`、`y` 字段定义：`{min:0, max:20000}` → `{min:-20000, max:20000}`（step 100 不变）。仅此两处数值变化。
- 无需改动：`updateField`/`flushSync`/`applyRemoteScene` 透传数值；画布视口以原点为中心，负坐标渲染天然正确；后端与协议零改动。
- 冒烟必查：负坐标全链路（画布拖拽→卡片读数→300ms 同步→仿真正常）。

## 5. 测试与验收（§4）

- Vitest（同目录）：`SplitBar.spec`（拖拽改比例、最小值钳制、localStorage 读写）；`KnobControl.spec`（角度边界 -135°/0°/+135° 映射、step 取整、键入提交、滚轮步进）；EntityCard 坐标用例扩展负值；BottomDrawer 两栏用例（结果/历史同时渲染）。
- 门禁：`npm run test` + `npm run typecheck` + `npm run build`，每提交必绿。
- 手动冒烟（浏览器+C++ 服务器，人执行）：四条分隔线手感+记忆恢复；旋钮到顶/到底+键入越界钳制；负坐标全链路；~900px 窄窗四栏可用。
- 回归红线：`git diff` 不得含 `src/`（C++）、`static/`、`protos/` 改动；WS 同步/防抖语义不变。

## 6. 非目标

- 不引入第三方旋钮/分隔库；不改 WS/HTTP 协议；不做移动端专项适配；不迁移旧 `drawerTab` 记忆键。
