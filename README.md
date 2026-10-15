# ECMSim — 电子对抗仿真器

ECMSim 是一个单二进制 C++17 电子对抗仿真系统，用于计算雷达/干扰机的信干噪比（SINR），支持 HTTP + gRPC + WebSocket 三合一服务架构，并集成了基于深度 Q 网络（DQN）的智能干扰决策模块。

## 核心功能

- **雷达方程仿真**：自由空间路径损耗、回波功率、热噪声、SINR 计算
- **干扰样式**：噪声压制干扰（NOISE_JAM）、距离欺骗干扰（RANGE_DECEPT）
- **频域匹配模型**：矩形窗（工程标准）和高斯平滑（智能训练用）两种频域匹配系数
- **干信比（JSR）**：有效干扰功率与信号功率的比值，含频域匹配衰减
- **多实体支持**：任意数量的雷达和干扰机，动态场景管理
- **智能决策**：基于 DQN 的干扰机自主决策，支持多干扰机协调分工
- **用户认证**：注册/登录（bcrypt 密码哈希 + Session Cookie），场景按用户隔离，`/api/*` 强制认证

## 架构设计

### 总体架构

```
┌─────────────────────────────────────────────────────────────┐
│                     客户端层                                 │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │  Web 前端     │  │  Python DQN  │  │  gRPC 客户端  │      │
│  │ (index_v2)   │  │   智能体      │  │              │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
└─────────┼──────────────────┼──────────────────┼──────────────┘
          │ HTTP/WS          │ gRPC              │ gRPC
          ▼                  ▼                   ▼
┌─────────────────────────────────────────────────────────────┐
│                   服务层 (C++ Boost.Beast)                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │ HTTP Server  │  │ WebSocket    │  │ gRPC Server  │      │
│  │  :8080       │  │ /ws          │  │  :50051      │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         │                  │                  │              │
│         └──────────────────┼──────────────────┘              │
│                            ▼                                 │
│                   ┌─────────────────┐                        │
│                   │   SceneManager  │                        │
│                   │  (线程安全会话)  │                        │
│                   └────────┬────────┘                        │
└────────────────────────────┼─────────────────────────────────┘
                             │
                             ▼
┌─────────────────────────────────────────────────────────────┐
│                    仿真引擎层                                │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │  ECMAlgo     │  │  ECMSim      │  │  jsoncpp     │      │
│  │  物理方程     │  │  实体/场景    │  │  JSON 解析    │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
└─────────────────────────────────────────────────────────────┘
```

### 分层设计

| 层次 | 模块 | 职责 |
|------|------|------|
| **客户端** | Web 前端 / Python DQN / gRPC 客户端 | 人机交互、智能决策、自动化测试 |
| **服务层** | HTTP / WebSocket / gRPC | 协议解析、路由分发、并发处理 |
| **业务层** | SceneManager / Web+Auth Service / gRPC Service | 会话管理、CRUD、认证授权、仿真调度 |
| **数据层** | MysqlPool / UserDao / SessionDao | MySQL 连接池、用户与登录会话持久化 |
| **引擎层** | ECMAlgo / ECMSim | 雷达方程、物理计算、状态管理 |

### 核心模块

#### 1. ECMAlgo — 物理算法层

命名空间 `ECMAlgo`，无状态纯函数，负责所有物理计算：

- **自由空间路径损耗**：`freeSpaceLoss(r, freq)`
- **雷达回波功率**：`radarEchoPower(Pt, Gt, Gr, sigma, r, freq)`
- **干扰到达功率**：`jammerReceivePower(Pj, Gj, Gr, r, freq)`
- **热噪声功率**：`thermalNoisePower(B)`
- **SINR 计算**：`calcSINR(signal_pow, jam_pow, noise_pow)`
- **频域匹配**：`freqMatchRect`（矩形窗）、`freqMatchGauss`（高斯平滑）
- **有效干扰功率**：`effectiveJamPower(Pj_space, delta_f, B, useGauss)`
- **干信比**：`calcJSR(jam_power, signal_power)`

#### 2. ECMSim — 实体与仿真层

命名空间 `ECMSim`，持有状态，负责场景管理和仿真执行：

- **Radar 类**：雷达实体，含位置、功率、增益、频率、带宽、RCS、检测门限
- **Jammer 类**：干扰机实体，含位置、功率、增益、干扰频率、干扰样式（噪声/欺骗）
- **SimScene 类**：场景容器，管理 `std::map<int, Radar>` 和 `std::map<int, Jammer>`
- **SceneManager 类**：多会话管理器，`std::unordered_map<string, SessionData>`，线程安全

#### 3. Framework — 基础设施层

通用基础设施，无 ECMSim 业务依赖：

- **RouteTrie**：纯字典树路由，支持 RESTful 路径参数
- **http_session / http_server**：基于 Boost.Beast 的 HTTP 会话和服务器
- **websocket_session / ws_broadcaster**：WebSocket 会话管理和广播器

#### 4. Service — 业务层

ECMSim 专属业务逻辑：

- **web_service**：`initRoutes` + 10 个场景 API 处理函数（`/api/*`，经 `requireAuth` 包装）
- **auth_service**：`AuthManager`（注册/登录/登出/会话验证）+ 4 个 `/auth/*` 处理函数 + `requireAuth` 中间件
- **grpc_service**：gRPC `AgentService` 实现
- **common**：全局状态、工具函数、`cleanup_controller`（资源清理，含过期登录会话）

#### 5. DAO — 数据访问层（MySQL）

- **MysqlPool**：单例连接池（4 连接预建），`ConnGuard` RAII 自动归还
- **UserDao**：`users` 表 CRUD（bcrypt hash 存储）
- **SessionDao**：登录会话表 CRUD（UUID v4，24h TTL）

### 数据流

```
前端/Python → HTTP/gRPC → SceneManager 修改数据
                                     ↓
                                broadcastScene()
                                     ↓
                                WebSocket 推送到会话的前端
```

### DQN 智能体数据流

```
Python 智能体 → gRPC GetState → SceneManager.getStateForJammer()
                                     ↓
                            返回原始雷达二维表:
                            [radar_count, radar1[7], ..., jammer[2]]
                                     ↓
                          Python DQNJammerAgent.parse_state()
                                     ↓
                          计算威胁权重 w_i = 1/(dist_i+1)
                          聚合为 9 维向量
                                     ↓
                          Trainer 追加 4 维覆盖特征 → 13 维
                                     ↓
                          QNetwork(state_dim=13) → 动作 (power_dbm, jam_freq)
                                     ↓
                          Trainer 选择参考雷达（最近未覆盖）
                                     ↓
                          gRPC ExecuteAction → 干扰机更新
                                     ↓
                          gRPC StepSimulation → SINR + 检测结果
                                     ↓
                          Python compute_reward() → 训练信号
```

## 快速开始

### 1. 安装依赖

```bash
# 系统依赖 (Ubuntu/Debian)
sudo apt install -y build-essential cmake git wget \
    libssl-dev zlib1g-dev libc-ares-dev \
    python3 python3-pip python3-venv

# 第三方库（Boost, gRPC, spdlog）
bash scripts/setup_deps.sh
```

### 2. 启动 MySQL

认证系统依赖 MySQL（默认 `127.0.0.1:3306`, `my_user`/`my_password`, `ecmsim_db`）：

```bash
docker-compose up -d mysql_server
# 连接信息可用环境变量覆盖：
# ECMSIM_DB_HOST / ECMSIM_DB_PORT / ECMSIM_DB_USER / ECMSIM_DB_PASSWORD / ECMSIM_DB_NAME
```

启动服务时若 MySQL 不可达，进程会 fail-fast 退出。

### 3. 构建

```bash
export LIBBOOST_HOME=$HOME/soft/boost
mkdir -p build && cd build && cmake .. && make -j$(nproc)
```

### 4. 运行

```bash
./bin/ecmsim_http_beast
# 默认监听 HTTP :8080 + gRPC :50051 + WebSocket
# 前端页面: http://localhost:8080
```

### 5. DQN 智能体（可选）

```bash
# 创建虚拟环境
python3 -m venv .venv && source .venv/bin/activate
uv pip install -r agents/dqn/requirements.txt

# 训练
python agents/dqn/script/train_agent.py --jammer-ids=1,2 --episodes=1000

# 推理
python agents/dqn/script/inference.py --session-id=<sid> --jammer-id=1 \
    --model=weights/dqn_jammer_j1_final.pth --steps=5
```

## HTTP API

**认证规则**：`/auth/*` 与静态资源无需登录；**所有 `/api/*` 必须携带 Cookie 中的 `session_id`**（登录后由服务端 Set-Cookie 下发），否则返回 401。

| 路由 | 方法 | 认证 | 描述 |
|------|------|------|------|
| `/` | GET | 否 | 前端页面 (WebSocket + DQN) |
| `/auth/register` | POST | 否 | 注册 `{username, password}` |
| `/auth/login` | POST | 否 | 登录 → Set-Cookie `session_id` |
| `/auth/logout` | POST | 否 | 登出（清除 Cookie） |
| `/auth/status` | GET | 否 | 当前登录状态 |
| `/api/scene` | POST | 是 | 创建/获取/删除会话 |
| `/api/radars` | GET/POST | 是 | 雷达列表/添加雷达 |
| `/api/radar` | GET/PUT/DELETE | 是 | 单个雷达 CRUD |
| `/api/jammers` | GET/POST | 是 | 干扰机列表/添加干扰机 |
| `/api/jammer` | GET/PUT/DELETE | 是 | 单个干扰机 CRUD |
| `/api/simulate` | POST | 是 | 运行仿真，返回 SINR + 检测结果 |
| `/api/dqn/state` | GET | 是 | 获取 DQN 状态向量 |
| `/api/dqn/action` | POST | 是 | 执行 DQN 动作 |
| `/api/scene/list` | GET | 是 | 当前用户的场景列表 |
| `/api/scene/stats` | GET | 是 | 服务器统计信息 |

## 用户认证

```
POST /auth/login → bcrypt 验证密码 → MySQL 生成登录会话 (UUID v4, 24h)
                → Set-Cookie: session_id=<uuid>; HttpOnly; Path=/
后续 /api/* 请求 → requireAuth 中间件 → SessionDao.validateSession() → 注入当前 user_id
```

- **密码存储**：bcrypt hash（work factor 12），密码策略 ≥ 8 字符
- **登录会话**：MySQL `sessions` 表，24h TTL，过期由清理线程定时回收
- **场景隔离**：场景记录归属 `user_id`，只能操作自己的场景；`/api/scene/list` 只返回本人场景
- **存储**：MySQL `ecmsim_db`，启动时 `CREATE TABLE IF NOT EXISTS` 幂等建表（`users` + `sessions`）

> **迁移说明**：`/api/*` 现在强制 Cookie 认证，旧版前端 `index_v2.html` 与未登录的 HTTP 客户端会收到 401（前端登录页待后续实现）。

## WebSocket 协议

```
WS /ws?session_id=<sid>
```

服务器在任何数据变更后自动推送完整场景 JSON 到所有连接的客户端。

## gRPC 服务 (端口 50051)

```protobuf
service AgentService {
  rpc GetState(StateRequest) returns (StateResponse);
  rpc ExecuteAction(ActionRequest) returns (ActionResponse);
  rpc StepSimulation(StepRequest) returns (StepResponse);
}
```

## 目录结构

```
ECMSim/
├── CMakeLists.txt              # 构建配置
├── README.md                   # 本文档
├── AGENTS.md                   # Agent 开发指南
├── docker-compose.yml          # MySQL (ecmsim_db) + rustfs
├── cmake/                      # CMake 模块 (bcrypt / protobuf / mysql_conncpp)
├── doc/                        # 设计文档（auth-design.md 等）
├── scripts/
│   └── setup_deps.sh           # 依赖安装脚本
├── config/                     # 场景 JSON 配置
├── include/
│   ├── algo/                   # ECMAlgo: 物理常数 + 雷达方程
│   ├── bcrypt/                 # libbcrypt 头文件
│   ├── entity/                 # ECMSim: Radar, Jammer 类
│   ├── sence/                  # ECMSim: SimScene + SceneManager
│   ├── app/
│   │   ├── framework/          # 基础设施层（通用）
│   │   │   ├── router.h        #   RouteTrie 字典树路由 + RouteResponse
│   │   │   ├── http_session.h  #   HTTP 会话 + 服务器
│   │   │   ├── ws_session.h    #   WebSocket 会话 + 广播器
│   │   │   ├── common.h        #   全局状态 + 工具函数
│   │   │   └── static_resource.h # 静态资源服务
│   │   ├── service/            # 业务层（ECMSim 专属）
│   │   │   ├── web_service.h   #   initRoutes + 10 个场景 API handlers
│   │   │   ├── auth_service.h  #   AuthManager + 认证 handlers + requireAuth
│   │   │   └── grpc_service.h  #   gRPC AgentService
│   │   └── dao/                # 数据访问层（MySQL）
│   │       ├── mysql_pool.h    #   连接池 + ConnGuard RAII
│   │       ├── user_dao.h      #   users 表 CRUD
│   │       └── session_dao.h   #   登录会话表 CRUD
│   ├── jsoncpp/json/           # jsoncpp 头文件
│   └── util/                   # 通用工具
├── src/                        # .cpp 镜像 include/ 布局
│   ├── algo/                   # 雷达方程实现
│   ├── bcrypt/                 # libbcrypt 源码
│   ├── entity/                 # radar.cpp, jammer.cpp
│   ├── sence/                  # sim_scene.cpp + scene_manager.cpp
│   ├── jsoncpp/                # jsoncpp 静态编译
│   └── app/
│       ├── framework/          # 基础设施层实现
│       ├── service/            # 业务层实现
│       ├── dao/                # 数据访问层实现
│       └── web_server_beast.cpp # main() 入口（含认证初始化）
├── build/                      # cmake 构建输出
├── bin/                        # 编译后的二进制
├── lib/                        # 运行时动态库 (libmysqlcppconn.so)
├── static/
│   └── index_v2.html           # 前端页面 (WebSocket + DQN)
├── test/                       # 单元测试
├── agents/dqn/                 # Python DQN 智能体
│   ├── script/                 # 训练/推理入口 + 配置
│   ├── core/                   # DQNJammerAgent + ReplayBuffer
│   ├── model/                  # QNetwork (PyTorch)
│   ├── train/                  # DQNTrainer
│   ├── utils/                  # 工具函数
│   ├── client.py               # gRPC 客户端
│   ├── weights/                # 训练好的模型权重
│   └── protos/                 # Python protobuf stubs
├── protos/                     # gRPC protobuf 定义
└── third_party/                # 第三方依赖
    ├── grpc/                   # gRPC + Protobuf + Abseil
    └── spdlog/                 # spdlog v1.17.0
```

## 日志规范

项目使用 **spdlog** 作为日志框架，所有新代码必须使用 spdlog。

```cpp
#include <spdlog/spdlog.h>

// 日志级别
spdlog::trace("追踪信息");
spdlog::debug("调试信息");
spdlog::info("一般信息: {}", value);
spdlog::warn("警告: {}", msg);
spdlog::error("错误: {}", ec.message());
spdlog::critical("严重错误");

// 格式化：使用 fmtlib 的 {} 占位符
spdlog::info("会话 {} 有 {} 个雷达", session_id, count);
```

**日志初始化**（在 `main()` 中）：
```cpp
spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
spdlog::set_level(spdlog::level::info);
```

## 常见问题

### Q: cmake 找不到 Boost
```bash
export LIBBOOST_HOME=$HOME/soft/boost
echo $LIBBOOST_HOME  # 应输出 /home/xxx/soft/boost
```

### Q: gRPC 编译失败
```bash
cd $HOME/build/grpc
git submodule update --init --recursive --jobs 8
rm -rf cmake/build && mkdir -p cmake/build && cd cmake/build
cmake ../.. [参数见 scripts/setup_deps.sh]
```

### Q: 运行时报错 MySQL 连接失败
```bash
docker-compose up -d mysql_server
# 检查连接信息（默认 my_user/my_password@127.0.0.1:3306/ecmsim_db）
# 可用 ECMSIM_DB_HOST / ECMSIM_DB_PORT / ECMSIM_DB_USER / ECMSIM_DB_PASSWORD / ECMSIM_DB_NAME 覆盖
```

### Q: /api/* 返回 401
需要先登录：`POST /auth/login` 获取 Cookie 后，携带 Cookie 调用 `/api/*`。

### Q: 运行时报错 "Cannot load static/index_v2.html"
```bash
cd $PROJECT_ROOT
./bin/ecmsim_http_beast static
```

## 许可证

本项目仅供学习和研究使用。
