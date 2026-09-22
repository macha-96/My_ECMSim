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
  grpc_gen/              — generated gRPC stub code (build/grpc_gen/ during cmake)
  jsoncpp/json/          — bundled jsoncpp headers
src/                     — .cpp mirrors of include/ layout
  algo/                  — radar equations, math_const
  entity/                — radar.cpp, jammer.cpp
  sence/                 — sim_sence.cpp + scene_manager.cpp
  jsoncpp/               — jsoncpp static lib
  app/
    web_server_beast.cpp — V3 main server (HTTP+gRPC+WebSocket, ~985 lines)
    http_server_fast.cpp — alternative fast HTTP server
    websocket_server_fast.cpp — alternative fast WebSocket server
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
    dqn_agent.py         — DQNJammerAgent (ε-greedy, target network)
    buffer.py            — ReplayBuffer
  model/
    q_network.py         — QNetwork (PyTorch)
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
- Include style: all paths relative to include/, e.g. `<entity/radar.h>`.
- Unit convention: all physical quantities in linear SI (W, Hz, m) internally.
- dB helpers (`db2lin`, `lin2db`) are inline in `include/algo/math_const.h`.
- jsoncpp is statically compiled via `src/jsoncpp/jsoncpp.cpp`.
- Radar constructor takes `Pt_dBm` (dBm, not dBW) — converts internally to linear Watts.
- **日志框架**: spdlog (header-only)，通过 `#include <spdlog/spdlog.h>` 引入。
- **日志 API**: `spdlog::info(...)`, `spdlog::error(...)`, `spdlog::warn(...)`, `spdlog::debug(...)`, `spdlog::critical(...)`
- **日志格式**: `spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v")`，在 `main()` 初始化。
- **所有新代码必须使用 spdlog**，不再使用 `MySimpleServerLog.h` 的 `LOG_INFO`/`LOG_ERROR` 宏。
- **fmtlib 格式**: 用 `{}` 占位符，不要用 printf 的 `%s`/`%d`/`%zu`。示例：`spdlog::info("count: {}", n)`。

## V3 Architecture (HTTP + gRPC + WebSocket)

### Server Components

| Component | File | Port | Description |
|-----------|------|------|-------------|
| HTTP | `src/app/web_server_beast.cpp` | :8080 | REST API + static file serving |
| gRPC | `src/app/web_server_beast.cpp` | :50051 | Python DQN agent communication |
| WebSocket | `src/app/web_server_beast.cpp` | /ws?session_id=xxx | Real-time scene updates to frontend |

### CMake Build Target

```
ecmsim_http_beast  →  src/app/web_server_beast.cpp + sim_core.a + gRPC + boost beast
sim_core           →  src/algo + src/entity + src/sence + src/jsoncpp + grpc_gen
```

### Linking Dependencies

Boost: header-only beast + `system thread context coroutine filesystem`

gRPC static libs: `libgrpc++.a libgrpc.a libgpr.a libprotobuf.a libupb_*.a libutf8_range*.a libre2.a libssl.a libcrypto.a libz.a libcares.a` + ~50 absl static targets + `pthread dl rt ssl crypto z cares re2`

Circular dependency handling: `-Wl,--start-group` / `-Wl,--end-group` wrapping all gRPC/absl libs.

### HTTP REST API

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
├── std::unordered_map<string, Session>   // session hash table
│   └── Session
│       ├── std::map<int, Radar>           // red-black tree, O(log n)
│       └── std::map<int, Jammer>          // red-black tree
└── std::mutex                             // thread safety
```

HTTP, gRPC, and WebSocket servers share one SceneManager instance via mutex. Every mutation calls `broadcastScene()` to push updates to WebSocket clients.

### Data Flow

```
Frontend/Python → HTTP/gRPC → SceneManager modify data
                                    ↓
                               broadcastScene()
                                    ↓
                               WebSocket push to session's frontend
```

## DQN Agent

Python venv required before running any agent:
```bash
source .venv/bin/activate
uv pip install websocket-client   # for WS tests
```

### Training

```bash
python -m agents.dqn.script.train_agent --episodes=2000
# or:
python agents/dqn/script/train_agent.py --episodes=2000
```

Model saved to `agents/dqn/weights/` (e.g. `dqn_jammer_final.pth`).

### Inference

```bash
python agents/dqn/script/inference.py --session-id=<sid> --jammer-id=1 --model=weights/dqn_jammer_final.pth --steps=5
```

### Agent Communication Flow

1. `GetState` — receives normalized state vector (positions, frequencies, distances)
2. `ExecuteAction` — sets jammer power (dBm) and frequency (Hz)
3. `StepSimulation` — runs one simulation step, returns SINR/detection results

Reward calculation is done entirely in Python (not on the C++ server). The C++ server only provides state/action/simulation primitives.

### DQN Client (`client.py`)

`ECMSimClient` class provides:
- `create_session()`, `delete_session()`
- `add_radar()`, `update_radar()`, `delete_radar()`
- `add_jammer()`, `update_jammer()`, `delete_jammer()`
- `simulate()`, `get_state()`, `execute_action()`
- `reset()` — no-op (session persists)
