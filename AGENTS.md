# AGENTS.md

## Project Overview

ECMSim: single-binary C++17 electronic-countermeasures simulator. Calculates radar/jammer SINR from a JSON scene config. Supports HTTP + gRPC + WebSocket triple-service architecture.

## Build & Run

### CMake Build (Primary — V3 HTTP+gRPC+WebSocket)

```bash
# Build
cd build && cmake .. && make -j$(nproc)
# Or:
mkdir -p build && cd build && cmake .. && make -j$(nproc)

# Run server (HTTP :8080 + gRPC :50051 + WebSocket)
bin/ecmsim_http_beast [static_dir]
# Default static_dir = "static" (serves index_v2.html)
```

Output is written to:
- `build/` — intermediate .o files + sim_core.a (preserves source tree layout)
- `bin/ecmsim_http_beast` — the final binary

### Legacy Build (test/Makefile — sim_test only)

```bash
cd test && make       # build bin/sim_test
cd test && make run   # build + run with config/sence_config.json
cd test && make clean # delete build/ and bin/
```

The sim_test binary accepts an optional config path as first argument (defaults to config/sence_config.json).

## Repo Structure

```
config/                  — scene JSON configs
include/
  algo/                  — ECMAlgo namespace: physics constants + radar equations
  entity/                — ECMSim namespace: Radar, Jammer classes
  sence/                 — ECMSim namespace: SimScene + SceneManager
  app/
    framework/           — 基础设施层（通用，无 ECMSim 业务依赖）
      router.h           — RouteTrie 纯字典树路由
      http_session.h     — http_session + http_server
      ws_session.h       — websocket_session + ws_broadcaster
    handlers/            — 业务层（ECMSim 专属）
      common.h           — 全局状态、工具函数、cleanup_controller
      routes.h           — initRoutes + 10 个 API handler 声明
      grpc_service.h     — gRPC AgentService
  grpc_gen/              — generated gRPC stub code (build/grpc_gen/ during cmake)
  jsoncpp/json/          — bundled jsoncpp headers
src/                     — .cpp mirrors of include/ layout
  algo/                  — radar equations, math_const
  entity/                — radar.cpp, jammer.cpp
  sence/                 — sim_sence.cpp + scene_manager.cpp
  jsoncpp/               — jsoncpp static lib
  app/
    framework/           — 基础设施层实现
      router.cpp         — RouteTrie 实现
      http_session.cpp   — HTTP 会话 + 服务器实现
      ws_session.cpp     — WebSocket 会话 + 广播器实现
    handlers/            — 业务层实现
      common.cpp         — 工具函数 + cleanup_controller 实现
      routes.cpp         — initRoutes + 10 个 API handler 实现
      grpc_service.cpp   — gRPC 服务实现
    web_server_beast.cpp — main() 入口 + 启动逻辑
build/                   — cmake build output (sim_core.a, grpc_gen/)
bin/                     — compiled binaries
static/
  index.html             — V1 frontend
  index_v2.html          — V2/V3 frontend (WebSocket + DQN + ECM)
test/
  Makefile               — legacy sim_test build
  main_test.cpp          — unit tests
  web_server_v2.cpp      — V2 legacy server (deprecated, use web_server_beast.cpp)
agents/dqn/
  script/
    train_agent.py       — DQN training entry point
    inference.py         — DQN inference entry point
    config.yaml          — training hyperparameters + scene config
   core/
     dqn_agent.py         — DQNJammerAgent (parse_state + ε-greedy, target network)
     buffer.py            — ReplayBuffer
   model/
     q_network.py         — QNetwork (PyTorch, state_dim=9, action_dim=21)
  train/
    trainer.py           — DQNTrainer
  utils/
    common.py            — utilities
  client.py              — gRPC client (connects to C++ server)
  weights/               — trained model weights (.pth)
  protos/                — Python protobuf stubs (agent_service_pb2.py)
protos/                  — gRPC protobuf definitions
third_party/grpc/        — prebuilt gRPC (install/ with lib/, include/, bin/)
third_party/spdlog/      — spdlog v1.17.0 (header-only 日志库)
```

## Architecture

- Two namespaces: ECMAlgo (physics/math, no state) and ECMSim (entities + simulation).
- Include style: all paths relative to include/, e.g. `<entity/radar.h>`, `<app/framework/router.h>`, `<app/handlers/routes.h>`.
- Unit convention: all physical quantities in linear SI (W, Hz, m) internally.
- dB helpers (`db2lin`, `lin2db`) are inline in `include/algo/math_const.h`.
- jsoncpp is statically compiled via `src/jsoncpp/jsoncpp.cpp`.
- Radar constructor takes `Pt_dBm` (dBm, not dBW) — converts internally to linear Watts.
- **日志框架**: spdlog (header-only)，通过 `#include <spdlog/spdlog.h>` 引入。python智能体部分的日志使用logging框架
- **日志 API**: `spdlog::info(...)`, `spdlog::error(...)`, `spdlog::warn(...)`, `spdlog::debug(...)`, `spdlog::critical(...)`
- **日志格式**: `spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v")`，在 `main()` 初始化。
- **所有新代码必须使用 spdlog**，不再使用 `MySimpleServerLog.h` 的 `LOG_INFO`/`LOG_ERROR` 宏。
- **fmtlib 格式**: 用 `{}` 占位符，不要用 printf 的 `%s`/`%d`/`%zu`。示例：`spdlog::info("count: {}", n)`。

## V3 Architecture (HTTP + gRPC + WebSocket)

### Server Modules

| Layer | Module | Header | Source | Description |
|-------|--------|--------|--------|-------------|
| Framework | Router | `include/app/framework/router.h` | `src/app/framework/router.cpp` | RouteTrie 纯字典树路由 |
| Framework | HTTP Session | `include/app/framework/http_session.h` | `src/app/framework/http_session.cpp` | HTTP 会话类 + 服务器类 |
| Framework | WebSocket | `include/app/framework/ws_session.h` | `src/app/framework/ws_session.cpp` | WebSocket 会话 + 广播器 |
| Handlers | Common | `include/app/handlers/common.h` | `src/app/handlers/common.cpp` | 全局状态、工具函数、cleanup_controller |
| Handlers | Routes | `include/app/handlers/routes.h` | `src/app/handlers/routes.cpp` | initRoutes + 10 个 API 处理函数 |
| Handlers | gRPC | `include/app/handlers/grpc_service.h` | `src/app/handlers/grpc_service.cpp` | gRPC AgentService |
| Entry | — | — | `src/app/web_server_beast.cpp` | main() 入口 + 启动逻辑 |

### CMake Build Target

```
ecmsim_http_beast  →  src/app/framework/*.cpp + src/app/handlers/*.cpp + sim_core.a + gRPC + boost beast
sim_core           →  src/algo + src/entity + src/sence + src/jsoncpp + grpc_gen
```

### Linking Dependencies

Boost: header-only beast + `system thread context coroutine filesystem`

gRPC static libs: `libgrpc++.a libgrpc.a libgpr.a libprotobuf.a libupb_*.a libutf8_range*.a libre2.a libssl.a libcrypto.a libz.a libcares.a` + ~50 absl static targets + `pthread dl rt ssl crypto z cares re2`

Circular dependency handling: `-Wl,--start-group` / `-Wl,--end-group` wrapping all gRPC/absl libs.

### HTTP RESTful API

| Route | Method | Description |
|-------|--------|-------------|
| `/` | GET | Serve `index_v2.html` (WebSocket-enabled frontend) |
| `/api/scene` | POST | Create session (no body) / Get scene (body: `{session_id}`) / Delete scene |
| `/api/radars` | GET/POST | List all radars / Add radar |
| `/api/radar` | GET/PUT/DELETE | Get/Update/Delete single radar |
| `/api/jammers` | GET/POST | List all jammers / Add jammer |
| `/api/jammer` | GET/PUT/DELETE | Get/Update/Delete single jammer |
| `/api/simulate` | POST | Run simulation, return SINR + detection results |
| `/api/dqn/state` | GET | Get normalized state vector for DQN agent |
| `/api/dqn/action` | POST | Execute DQN action (power_dbm, jam_freq) |
| `/api/scene/list` | GET | List all sessions with metadata |
| `/api/scene/stats` | GET | Get server statistics |

### WebSocket Protocol

```
WS /ws?session_id=<sid>
```

- Server pushes full scene JSON to all connected clients after any mutation (gRPC/HTTP/DQN action).
- Frontend auto-reconnects on close.
- `ws_broadcaster` class manages per-session pub/sub with thread-safe mutex.
- `websocket_session` holds the original HTTP request for handshake (`ws_.async_accept(req_)`).

### gRPC Service (port 50051)

```protobuf
service AgentService {
  rpc GetState(StateRequest) returns (StateResponse);
  rpc ExecuteAction(ActionRequest) returns (ActionResponse);
  rpc StepSimulation(StepRequest) returns (StepResponse);
}
```

### SceneManager

```
SceneManager
├── std::unordered_map<string, SessionData>   // session hash table
│   └── SessionData
│       ├── SimScene scene                     // 持有雷达 + 干扰机（std::map 容器）
│       │   ├── std::map<int, Radar>
│       │   └── std::map<int, Jammer>
│       ├── uint64_t created_at
│       └── uint64_t last_access
└── std::mutex                                 // thread safety
```

HTTP, gRPC, and WebSocket servers share one SceneManager instance via mutex. Every mutation calls `broadcastScene()` to push updates to WebSocket clients.

**SessionData 与 SimScene 合并**：`SessionData` 直接持有 `SimScene scene`，`runSimulation()` 直接调用 `scene.runOneStep()`，无需数据拷贝。所有 CRUD 操作（addRadar/removeRadar 等）委托给 `SimScene`。

### Data Flow

```
Frontend/Python → HTTP/gRPC → SceneManager modify data
                                     ↓
                                broadcastScene()
                                     ↓
                                WebSocket push to session's frontend
```

### DQN Data Flow (Multi-Radar, Multi-Jammer)

```
Python agent → gRPC GetState → SceneManager.getStateForJammer()
                                    ↓
                            returns raw radar 2D table:
                            [radar_count, radar1[7], ..., jammer[2]]
                                    ↓
                          Python DQNJammerAgent.parse_state()
                                    ↓
                          Computes threat weights w_i = 1/(dist_i+1)
                                    ↓
                          Aggregates to 9-dim vector
                                    ↓
                          Trainer appends 4 coverage features → 13-dim
                                    ↓
                          QNetwork(state_dim=13) → action (power_dbm, jam_freq)
                                    ↓
                          Trainer selects reference radar (nearest uncovered)
                                    ↓
                          gRPC ExecuteAction → Jammer updated
                                    ↓
                          gRPC StepSimulation → SINR + detection results
                                    ↓
                          Python compute_reward() → training signal
```

HTTP, gRPC, and WebSocket servers share one SceneManager instance via mutex. Every mutation calls `broadcastScene()` to push updates to WebSocket clients.

## DQN Agent

Python venv required before running any agent:
```bash
source .venv/bin/activate
# uv pip install websocket-client   # for WS tests
```
你如果要给虚拟环境安装依赖的话，优先使用：
```bash
uv add <依赖包的名字>
```
如果不行，再试一下：
```bash
uv pip install <依赖包的名字>
```

### Training

```bash
# Train with default 2 radars, 2 jammers
python agents/dqn/script/train_agent.py --jammer-ids=1,2 --episodes=1000

# Train with variable radar/jammer counts
python agents/dqn/script/train_agent.py --jammer-ids=1,2,3 --num-radars=4 --num-jammers=3
```

Model saved to `agents/dqn/weights/` (e.g. `dqn_jammer_j1_final.pth`).

### Inference

```bash
python agents/dqn/script/inference.py --session-id=<sid> --jammer-id=1 --model=weights/dqn_jammer_j1_final.pth --steps=5
```

### Multi-Radar Multi-Jammer Architecture

**C++ 侧**（`scene_manager.cpp` / `sim_sence.cpp`）：`getStateForJammer` 返回原始雷达二维表（扁平化），不做任何聚合计算。

状态向量格式：`[radar_count, radar1[rx,ry,freq,bw,pt,dist,delta_f], ..., jammer[pj,freq]]`
- `radar_count`：场景中雷达数量
- 每个雷达 7 个归一化特征：位置(rx/20000, ry/20000)、频率(freq/20e9)、带宽(bw/10e6)、功率(pt/1000)、距离(dist/30000)、频差(delta_f/10e9)
- 干扰机 2 个特征：功率(pj/1000)、频率(freq/20e9)

**Python 侧**（`agents/dqn/core/dqn_agent.py` + `agents/dqn/train/trainer.py`）：

1. `parse_state()` 解析原始雷达表，计算威胁加权 → 9 维向量
2. `parse_state_with_coverage()` 追加 4 维覆盖特征 → 13 维状态
3. 覆盖特征：`[uncovered_count_norm, nearest_uncovered_dist, nearest_uncovered_freq, nearest_uncovered_delta_f]`
4. 动作映射使用动态参考雷达选择（最近未覆盖雷达）

**协调机制**：
- 每个干扰机由独立 DQN 智能体控制
- 覆盖感知状态让智能体自主学习分工策略
- 冗余惩罚避免多个干扰机扎堆同一雷达
- 支持任意数量的雷达和干扰机（状态维度固定为 13）

**优势**：QNetwork 的 `state_dim=13` 和 `action_dim=21` 保持不变，模型可跨场景复用。

### Agent Communication Flow

1. `GetState` — receives raw radar 2D table (radar_count + all radar features)
2. `parse_state()` + `parse_state_with_coverage()` — Python agent computes threat weights + coverage → 13-dim vector
3. `choose_action()` — DQN selects action (power, freq_shift)
4. `index_to_action_with_coverage()` — maps action to (power_dbm, jam_freq) using nearest uncovered radar
5. `ExecuteAction` — sets jammer power (dBm) and frequency (Hz)
6. `StepSimulation` — runs one simulation step, returns SINR/detection results

Reward calculation is done entirely in Python (not on the C++ server). The C++ server only provides raw scene data, action execution, and simulation primitives.

### DQN Client (`client.py`)

`ECMSimClient` class provides:
- `create_session()`, `delete_session()`
- `add_radar()`, `update_radar()`, `delete_radar()`
- `add_jammer()`, `update_jammer()`, `delete_jammer()`
- `simulate()`, `get_state()`, `execute_action()`
- `reset()` — no-op (session persists)

### others

1. 编写代码的时候可以适当添加一些注释，如果原来的代码有注释，除非变更相关代码，请不要删除随意删除这些注释
2. C++代码保持现有的大括号不换行的风格