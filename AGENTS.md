# AGENTS.md

## Project overview

ECMSim: single-binary C++17 electronic-countermeasures simulator. Calculates radar/jammer SINR from a JSON scene config.

## Build & run

All commands execute from the **`test/` directory** (the Makefile lives there):

```bash
make       # build bin/sim_test
make run   # build + run with config/sence_config.json
make clean # delete build/ and bin/

Output is written to the project root:
- build/ — intermediate .o files (preserves source tree layout)
- bin/sim_test — the final binary
- result_output.json — generated after each run
The binary accepts an optional config path as first argument (defaults to config/sence_config.json).

Repo structure
config/            — scene JSON configs
include/
  algo/            — ECMAlgo namespace: physics constants + radar equations
  entity/          — ECMSim namespace: Radar, Jammer classes
  sence/           — ECMSim namespace: SimScene + SceneManager
  grpc_gen/        — generated gRPC stub code
  my_http_lib/     — C HTTP library headers (libreactor)
  jsoncpp/json/    — bundled jsoncpp headers
src/               — .cpp mirrors of include/ layout
  sence/           — sim_sence.cpp + scene_manager.cpp
test/              — Makefile + main_test.cpp + web_server_v2.cpp
agents/dqn/        — Python DQN agent + gRPC client
protos/            — gRPC protobuf definitions
third_party/grpc/  — prebuilt gRPC (install/include + install/lib)

## Architecture

- Two namespaces: ECMAlgo (physics/math, no state) and ECMSim (entities + simulation).
- Include style: all paths relative to include/, e.g. <entity/radar.h>.
- Unit convention: all physical quantities in linear SI (W, Hz, m) internally.
- dB helpers (db2lin, lin2db) are inline in include/algo/math_const.h.
- jsoncpp is statically compiled via src/jsoncpp/jsoncpp.cpp.
- Radar constructor takes Pt_dBm (dBm, not dBW) — converts internally to linear Watts.

## V2 Architecture (HTTP + gRPC + SceneManager)

### New components

| File | Description |
|------|-------------|
| `include/sence/scene_manager.h` | SceneManager: session hash map + red-black tree entity storage |
| `src/sence/scene_manager.cpp` | CRUD, simulation, DQN state/action, JSON serialization |
| `protos/agent_service.proto` | gRPC service (GetState/ExecuteAction/StepSimulation) |
| `include/grpc_gen/` | protoc-generated C++ gRPC stubs |
| `test/web_server_v2.cpp` | Combined HTTP (:8080) + gRPC (:50051) server |
| `agents/dqn/client.py` | Python gRPC client for DQN agent |
| `agents/dqn/pyproject.toml` | uv-managed dependencies |

### API Specification (V2 HTTP)

All requests are POST with JSON body containing `action` and `session_id`:

| Route | Action | Description |
|-------|--------|-------------|
| `/api/scene` | `create` | Create session, returns `session_id` |
| `/api/scene` | `config` | Get full scene JSON |
| `/api/scene` | `delete` | Delete session |
| `/api/scene` | `simulate` | Run simulation, return results |
| `/api/scene` | `dqn_state` | Get state vector for a jammer |
| `/api/scene` | `dqn_action` | Execute jammer action (power_dbm, jam_freq) |
| `/api/radar` | `add`/`update`/`del` | Radar CRUD |
| `/api/jammer` | `add`/`update`/`del` | Jammer CRUD |

### gRPC Service (port 50051)

```protobuf
service AgentService {
  rpc GetState(StateRequest) returns (StateResponse);
  rpc ExecuteAction(ActionRequest) returns (ActionResponse);
  rpc StepSimulation(StepRequest) returns (StepResponse);
}
```

### Build commands

```bash
make proto        # generate gRPC stubs from protos/
make web-v2       # build web_server_v2.cpp + gRPC + HTTP
make web-v2-run   # start server (HTTP :8080 + gRPC :50051)
```

### SceneManager Architecture

```
SceneManager
├── std::unordered_map<string, Session>   // session hash table
│   └── Session
│       ├── std::map<int, Radar>           // red-black tree, O(log n)
│       └── std::map<int, Jammer>          // red-black tree
└── std::mutex                             // thread safety
```

HTTP and gRPC servers share one SceneManager instance via mutex.

### DQN Agent (agents/dqn/)

```
agents/dqn/
├── core/
│   ├── buffer.py          -- ReplayBuffer
│   └── dqn_agent.py       -- DQNJammerAgent (ε-greedy, target network)
├── model/
│   └── q_network.py       -- QNetwork (PyTorch)
├── script/
│   └── config.yaml        -- hyperparameters
├── train/
│   └── trainer.py         -- training loop
├── utils/
│   └── common.py          -- utilities
├── client.py              -- gRPC client (connects to C++ server)
├── agent_service_pb2.py   -- generated protobuf
├── agent_service_pb2_grpc.py -- generated gRPC stub
├── pyproject.toml         -- uv dependencies (grpcio, protobuf, numpy, torch)
```

The DQN agent communicates with the C++ server via gRPC:
1. `GetState` — receives normalized state vector (positions, frequencies, distances)
2. `ExecuteAction` — sets jammer power (dBm) and frequency (Hz)
3. `StepSimulation` — runs one simulation step, returns SINR/detection results

Reward calculation is done entirely in Python (not on the C++ server).