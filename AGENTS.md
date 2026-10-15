# AGENTS.md

## Project Overview

ECMSim is a single-binary C++17 electronic-countermeasures simulator that calculates radar/jammer SINR from a JSON scene config. It supports an HTTP + gRPC + WebSocket triple-service architecture and integrates a Python DQN (Deep Q-Network) agent for intelligent jamming decisions.

**What it does:**
- Simulates radar/jammer signal-to-interference-plus-noise ratio (SINR) using physics-based radar equations
- Supports two jamming types: noise suppression (NOISE_JAM) and range deception (RANGE_DECEPT)
- Provides frequency-domain matching models (rectangular window + Gaussian smoothing) for effective jamming power calculation
- Manages multi-entity scenarios (arbitrary radars + jammers) with thread-safe session management
- Integrates a DQN-based intelligent agent for autonomous jamming strategy optimization

**Architecture at a glance:**
- **Service Layer**: HTTP (:8080) + gRPC (:50051) + WebSocket, all sharing one SceneManager
- **Business Layer**: SceneManager (thread-safe sessions) + 14 RESTful API handlers + gRPC AgentService + AuthManager
- **DAO Layer**: MysqlPool (连接池) + UserDao + SessionDao，对接 MySQL (`ecmsim_db`)
- **Engine Layer**: ECMAlgo (stateless physics/math) + ECMSim (stateful entities + simulation)
- **DQN Agent**: Python PyTorch agents connecting via gRPC for intelligent decision-making
- **Auth**: 用户注册/登录 (bcrypt + Session Cookie)，`/api/*` 全部需认证，场景按用户隔离

See README.md for full architecture diagrams and API documentation.

## Build & Run

### Prerequisites

- **Boost**: `export LIBBOOST_HOME=/path/to/boost`（cmake 必须）
- **MySQL**: 服务启动前必须有可访问的 MySQL（默认 docker-compose 配置：`127.0.0.1:3306`, `my_user`/`my_password`, `ecmsim_db`）。可用 `docker-compose up -d mysql_server` 启动。连接失败时进程 fail-fast 退出。
- **MySQL Connector/C++**: 默认 `/home/ky/soft/mysql-conncpp`，可用 `MYSQL_CONNCXX_HOME` 环境变量覆盖。
- 数据库连接可用环境变量覆盖：`ECMSIM_DB_HOST/ECMSIM_DB_PORT/ECMSIM_DB_USER/ECMSIM_DB_PASSWORD/ECMSIM_DB_NAME`。

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
cmake/                   — CMake 模块（bcrypt.cmake, protobuf.cmake, mysql_conncpp.cmake）
doc/                     — 设计文档（auth-design.md 等）
include/
  algo/                  — ECMAlgo namespace: physics constants + radar equations
  bcrypt/                — libbcrypt 头文件（bcrypt_gensalt/hashpw/checkpw）
  entity/                — ECMSim namespace: Radar, Jammer classes
  sence/                 — ECMSim namespace: SimScene + SceneManager
  app/
    framework/           — 基础设施层（通用，无 ECMSim 业务依赖）
      router.h           — RouteTrie 纯字典树路由 + RouteResponse
      http_session.h     — http_session + http_server
      ws_session.h       — websocket_session + ws_broadcaster
      common.h           — 全局状态、工具函数、cleanup_controller
      static_resource.h  — 静态资源服务
    service/             — 业务层（ECMSim 专属）
      web_service.h      — initRoutes + 10 个场景 API handler 声明
      auth_service.h     — AuthManager + 4 个 /auth/* handler + requireAuth 中间件
      grpc_service.h     — gRPC AgentService
    dao/                 — 数据访问层（MySQL）
      mysql_pool.h       — MysqlPool 连接池 + ConnGuard RAII
      user_dao.h         — users 表 CRUD
      session_dao.h      — 登录会话表 CRUD
  grpc_gen/              — generated gRPC stub code (build/grpc_gen/ during cmake)
  jsoncpp/json/          — bundled jsoncpp headers
src/                     — .cpp mirrors of include/ layout
  bcrypt/                — libbcrypt 源码（bcrypt.c, crypt_blowfish.c, x86.S 等）
  algo/                  — radar equations, math_const
  entity/                — radar.cpp, jammer.cpp
  sence/                 — sim_sence.cpp + scene_manager.cpp
  jsoncpp/               — jsoncpp static lib
  app/
    framework/           — 基础设施层实现
      router.cpp         — RouteTrie 实现
      http_session.cpp   — HTTP 会话 + 服务器实现
      ws_session.cpp     — WebSocket 会话 + 广播器实现
      common.cpp         — 工具函数 + cleanup_controller 实现
      static_resource.cpp — 静态资源实现
    service/             — 业务层实现
      web_service.cpp    — initRoutes + 场景 API handler 实现
      auth_service.cpp   — AuthManager + 认证 handler 实现
      grpc_service.cpp   — gRPC 服务实现
    dao/                 — 数据访问层实现
      mysql_pool.cpp     — 连接池实现
      user_dao.cpp       — 用户表 DAO
      session_dao.cpp    — 登录会话表 DAO
    web_server_beast.cpp — main() 入口 + 启动逻辑（含认证系统初始化）
build/                   — cmake build output (sim_core.a, grpc_gen/)
bin/                     — compiled binaries
lib/                     — 运行时动态库（libmysqlcppconn.so 部署于此）
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
- Include style: all paths relative to include/, e.g. `<entity/radar.h>`, `<app/framework/router.h>`, `<app/service/web_service.h>`, `<app/dao/user_dao.h>`.
- Unit convention: all physical quantities in linear SI (W, Hz, m) internally.
- dB helpers (`db2lin`, `lin2db`) are inline in `include/algo/math_const.h`.
- jsoncpp is statically compiled via `src/jsoncpp/jsoncpp.cpp`.
- Radar constructor takes `Pt_dBm` (dBm, not dBW) — converts internally to linear Watts.
- **前端框架**：前端基于vue-ts + vite搭建，所有前端的代码放在front_end目录下，如果需要修改前端请切换到front_end目录下编辑，原本的static目录下的内容已经弃用
- **日志框架**: spdlog (header-only)，通过 `#include <spdlog/spdlog.h>` 引入。python智能体部分的日志使用logging框架
- **日志 API**: `spdlog::info(...)`, `spdlog::error(...)`, `spdlog::warn(...)`, `spdlog::debug(...)`, `spdlog::critical(...)`
- **日志格式**: `spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v")`，在 `main()` 初始化。
- **所有新代码必须使用 spdlog**，不再使用 `MySimpleServerLog.h` 的 `LOG_INFO`/`LOG_ERROR` 宏。
- **fmtlib 格式**: 用 `{}` 占位符，不要用 printf 的 `%s`/`%d`/`%zu`。示例：`spdlog::info("count: {}", n)`。

## V3 Architecture (HTTP + gRPC + WebSocket)

### Server Modules

| Layer | Module | Header | Source | Description |
|-------|--------|--------|--------|-------------|
| Framework | Router | `include/app/framework/router.h` | `src/app/framework/router.cpp` | RouteTrie 纯字典树路由 + RouteResponse |
| Framework | HTTP Session | `include/app/framework/http_session.h` | `src/app/framework/http_session.cpp` | HTTP 会话类 + 服务器类 |
| Framework | WebSocket | `include/app/framework/ws_session.h` | `src/app/framework/ws_session.cpp` | WebSocket 会话 + 广播器 |
| Framework | Common | `include/app/framework/common.h` | `src/app/framework/common.cpp` | 全局状态、工具函数、cleanup_controller |
| Service | Web | `include/app/service/web_service.h` | `src/app/service/web_service.cpp` | initRoutes + 10 个场景 API 处理函数 |
| Service | Auth | `include/app/service/auth_service.h` | `src/app/service/auth_service.cpp` | AuthManager + 4 个认证 handler + requireAuth |
| Service | gRPC | `include/app/service/grpc_service.h` | `src/app/service/grpc_service.cpp` | gRPC AgentService |
| DAO | MysqlPool | `include/app/dao/mysql_pool.h` | `src/app/dao/mysql_pool.cpp` | MySQL 连接池 + ConnGuard RAII |
| DAO | UserDao | `include/app/dao/user_dao.h` | `src/app/dao/user_dao.cpp` | users 表 CRUD |
| DAO | SessionDao | `include/app/dao/session_dao.h` | `src/app/dao/session_dao.cpp` | 登录会话表 CRUD |
| Entry | — | — | `src/app/web_server_beast.cpp` | main() 入口 + 启动逻辑（含认证初始化） |

### CMake Build Target

```
ecmsim_http_beast  →  src/app/framework/*.cpp + src/app/service/*.cpp + src/app/dao/*.cpp + sim_core.a + bcrypt + MySQL Connector/C++ + gRPC::grpc++ + boost beast
sim_core           →  src/algo + src/entity + src/sence + src/jsoncpp + grpc_gen
bcrypt (static)    →  src/bcrypt/*.c + x86.S（由 cmake/bcrypt.cmake 定义）
```

### Linking Dependencies

- gRPC/protobuf/abseil: `find_package(gRPC/protobuf/absl REQUIRED CONFIG)` + 链接 `gRPC::grpc++`（由 cmake/protobuf.cmake 生成 proto 源码）
- bcrypt: 静态库 `bcrypt`（cmake/bcrypt.cmake）
- MySQL Connector/C++: `libmysqlcppconn.so`（cmake/mysql_conncpp.cmake，POST_BUILD 部署到 `lib/`）
- Boost: header-only beast + `system thread context coroutine filesystem`

### HTTP RESTful API

**认证规则**：`/auth/*` 与静态资源无需登录；**所有 `/api/*` 必须携带 Cookie 中的 `session_id`**（登录后由服务端 Set-Cookie 下发），否则返回 401。

| Route | Method | Auth | Description |
|-------|--------|------|-------------|
| `/` | GET | 否 | Serve `index_v2.html` (WebSocket-enabled frontend) |
| `/auth/register` | POST | 否 | `{username, password}` → `{success, user_id}` |
| `/auth/login` | POST | 否 | `{username, password}` → `{success, session_id}` + Set-Cookie |
| `/auth/logout` | POST | 否 | 清除登录会话（Set-Cookie Max-Age=0） |
| `/auth/status` | GET | 否 | `{authenticated, user_id?}` 当前登录状态 |
| `/api/scene` | POST | 是 | Create scene (body: `{}`) / Get (body: `{session_id}`) / Delete |
| `/api/radars` | GET/POST | 是 | List all radars / Add radar |
| `/api/radar` | GET/PUT/DELETE | 是 | Get/Update/Delete single radar |
| `/api/jammers` | GET/POST | 是 | List all jammers / Add jammer |
| `/api/jammer` | GET/PUT/DELETE | 是 | Get/Update/Delete single jammer |
| `/api/simulate` | POST | 是 | Run simulation, return SINR + detection results |
| `/api/dqn/state` | GET | 是 | Get normalized state vector for DQN agent |
| `/api/dqn/action` | POST | 是 | Execute DQN action (power_dbm, jam_freq) |
| `/api/scene/list` | GET | 是 | List current user's scenes with metadata |
| `/api/scene/stats` | GET | 是 | Get server statistics |

### 认证系统

```
Frontend ──POST /auth/login──▶ AuthManager.login()
                                   │ bcrypt_checkpw 验证密码
                                   │ SessionDao.createSession(user_id) → UUID v4
                                   ▼
                          Set-Cookie: session_id=<uuid>; HttpOnly; Path=/; SameSite=Strict
                                   │
后续 /api/* 请求 ──Cookie──▶ requireAuth 中间件（包装 handler）
                                   │ AuthManager.getUserIdFromCookie()
                                   │ SessionDao.validateSession() → user_id (MySQL)
                                   ▼
                          注入 thread_local g_thread_user_id → 原 handler 执行
```

- **存储**: MySQL `ecmsim_db` 的 `users`（bcrypt hash 60 字符，work factor 12）+ `sessions`（UUID v4 主键，24h TTL，外键级联删除）表，`CREATE TABLE IF NOT EXISTS` 启动时幂等建表。
- **连接池**: `MysqlPool` 单例（4 连接预建），`ConnGuard` RAII 自动归还；`PreparedStatement` 防注入。
- **场景隔离**: `SessionData.user_id` 记录归属（-1 = 旧版未认证场景，迁移期放行）；`/api/scene` 创建时绑定当前用户，`/api/scene/list` 只返回自己的场景；各 handler 通过 `sessionAccessible()` 校验归属。
- **中间件实现**: `requireAuth(handler)` 在 `initRoutes` 注册时包装，属业务层（framework 不依赖 ECMSim 业务）。
- **过期清理**: `cleanup_controller` 每 5 分钟调用 `AuthManager.cleanupExpiredSessions()`。
- **环境变量**: `ECMSIM_DB_HOST/ECMSIM_DB_PORT/ECMSIM_DB_USER/ECMSIM_DB_PASSWORD/ECMSIM_DB_NAME`（默认 `127.0.0.1:3306/my_user/my_password/ecmsim_db`）。
- **设计文档**: `doc/auth-design.md`

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
│       ├── uint64_t last_access
│       └── int user_id                        // 归属用户（-1 = 旧版未认证场景）
└── std::mutex                                 // thread safety
```

HTTP, gRPC, and WebSocket servers share one SceneManager instance via mutex. Every mutation calls `broadcastScene()` to push updates to WebSocket clients.

**SessionData 与 SimScene 合并**：`SessionData` 直接持有 `SimScene scene`，`runSimulation()` 直接调用 `scene.runOneStep()`，无需数据拷贝。所有 CRUD 操作（addRadar/removeRadar 等）委托给 `SimScene`。

**多用户场景隔离**：`createSession(int user_id)` 重载创建归属场景（无参版本传 -1 保持兼容）；`getSessionsByUser(uid)` 供场景列表过滤；`getSessionUser(sid)` + `sessionAccessible(sid)` 供各 handler 校验操作归属。

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