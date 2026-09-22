# ECMSim — 电子对抗仿真器

单二进制 C++17 电子对抗仿真器，支持 HTTP + gRPC + WebSocket 三合一架构。

## 快速开始

### 1. 安装系统依赖

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install -y build-essential cmake git wget \
    libssl-dev zlib1g-dev libc-ares-dev \
    python3 python3-pip python3-venv

# CentOS/RHEL
sudo yum groupinstall -y "Development Tools"
sudo yum install -y cmake git wget \
    openssl-devel zlib-devel c-ares-devel \
    python3 python3-pip
```

### 2. 安装第三方库

```bash
# 在项目根目录执行
bash scripts/setup_deps.sh
```

### 3. 构建项目

```bash
export LIBBOOST_HOME=$HOME/soft/boost
mkdir -p build && cd build && cmake .. && make -j$(nproc)
```

### 4. 运行

```bash
./bin/ecmsim_http_beast
# 默认监听 HTTP :8080 + gRPC :50051 + WebSocket
```

---

## 依赖详细安装指南

### 依赖总览

| 依赖 | 版本 | 安装方式 | 用途 |
|------|------|----------|------|
| CMake | >= 3.16 | 系统包管理器 | 构建系统 |
| GCC/Clang | C++17 支持 | 系统包管理器 | 编译器 |
| Boost.Beast | 1.92.0 | 源码编译 | HTTP/WebSocket 服务器 |
| gRPC + Protobuf | - | 源码编译 | RPC 通信 |
| Abseil (absl) | - | gRPC 依赖 | gRPC 内部库 |
| spdlog | 1.17.0 | git clone | 日志框架 |
| jsoncpp | 1.9.8 | 已内置 | JSON 解析 |
| OpenSSL | - | 系统包管理器 | TLS 支持 |
| zlib | - | 系统包管理器 | 压缩 |
| c-ares | - | 系统包管理器 | 异步 DNS |

---

### 1. Boost (1.92.0)

Boost.Beast 是 header-only 库，但需要编译部分 Boost 组件（thread, system, context, coroutine, filesystem）。

```bash
# 创建安装目录
mkdir -p $HOME/soft

# 下载 Boost 1.92.0
cd /tmp
wget https://archives.boost.io/release/1.92.0/source/boost_1_92_0.tar.gz
tar xzf boost_1_92_0.tar.gz
cd boost_1_92_0

# 编译安装（只编译项目需要的组件）
./bootstrap.sh --prefix=$HOME/soft/boost
./b2 install --prefix=$HOME/soft/boost \
    --with-thread --with-system --with-context \
    --with-coroutine --with-filesystem \
    -j$(nproc)

# 验证安装
ls $HOME/soft/boost/include/boost/beast/
ls $HOME/soft/boost/lib/libboost_*.so
```

**环境变量**（添加到 `~/.bashrc`）：
```bash
export LIBBOOST_HOME=$HOME/soft/boost
export PATH=$LIBBOOST_HOME/bin:$PATH
export LD_LIBRARY_PATH=$LIBBOOST_HOME/lib:$LD_LIBRARY_PATH
```

---

### 2. gRPC + Protobuf + Abseil

gRPC 是本项目最复杂的依赖，需要从源码编译。本项目使用**静态链接**方式。

```bash
# 创建工作目录
mkdir -p $HOME/build && cd $HOME/build

# 克隆 gRPC（包含子模块）
git clone --recursive -b v1.65.0 https://github.com/grpc/grpc.git
cd grpc

# 或者分步克隆子模块
# git clone https://github.com/grpc/grpc.git
# cd grpc
# git submodule update --init --recursive --jobs 8

# 创建构建目录
mkdir -p cmake/build && cd cmake/build

# 配置（关键选项）
cmake ../.. \
    -DCMAKE_INSTALL_PREFIX=$PROJECT_ROOT/third_party/grpc/install \
    -DCMAKE_BUILD_TYPE=Release \
    -DgRPC_INSTALL=ON \
    -DgRPC_BUILD_TESTS=OFF \
    -DgRPC_BUILD_CODEGEN=ON \
    -DgRPC_BUILD_CSHARP_EXT=OFF \
    -DgRPC_BUILD_GRPC_CPP_PLUGIN=ON \
    -DABSL_BUILD_TESTING=OFF \
    -DABSL_USE_GOOGLETEST_HEAD=OFF \
    -DBUILD_SHARED_LIBS=OFF \
    -Dprotobuf_BUILD_TESTS=OFF \
    -Dprotobuf_ABSL_PROVIDER=module \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON

# 编译（耗时较长，约 10-30 分钟）
make -j$(nproc)

# 安装到 third_party/grpc/install/
make install
```

**编译完成后目录结构**：
```
third_party/grpc/install/
├── bin/
│   ├── protoc                  # protobuf 编译器
│   ├── grpc_cpp_plugin         # gRPC C++ 代码生成插件
│   └── ...
├── include/
│   ├── grpc++/                 # gRPC C++ 头文件
│   ├── google/protobuf/        # Protobuf 头文件
│   └── absl/                   # Abseil 头文件
└── lib/
    ├── libgrpc++.a             # gRPC C++ 静态库
    ├── libgrpc.a               # gRPC 核心静态库
    ├── libprotobuf.a           # Protobuf 静态库
    ├── libabsl_*.a             # Abseil 系列静态库
    ├── libre2.a                # RE2 正则库
    └── cmake/                  # CMake 配置文件
```

**验证安装**：
```bash
ls $PROJECT_ROOT/third_party/grpc/install/bin/protoc
ls $PROJECT_ROOT/third_party/grpc/install/lib/libgrpc++.a
$PROJECT_ROOT/third_party/grpc/install/bin/protoc --version
```

---

### 3. spdlog (1.17.0)

spdlog 是 header-only 日志库，通过 git clone 集成。

```bash
cd $PROJECT_ROOT/third_party

# 克隆 spdlog
git clone --branch v1.17.0 https://github.com/gabime/spdlog.git

# 验证
ls spdlog/include/spdlog/spdlog.h
```

**CMake 自动集成**：`CMakeLists.txt` 中已配置 `add_subdirectory(third_party/spdlog)`，无需额外操作。

---

### 4. jsoncpp (1.9.8)

jsoncpp 已内置在项目中，无需额外安装。

```
include/jsoncpp/json/json.h    # 头文件
src/jsoncpp/jsoncpp.cpp        # 实现（自动编入 sim_core）
```

---

### 5. Python 虚拟环境（可选，用于 DQN 智能体）

```bash
cd $PROJECT_ROOT

# 创建虚拟环境
python3 -m venv .venv
source .venv/bin/activate

# 安装依赖
pip install -r agents/dqn/requirements.txt
# 或者使用 uv
pip install uv
uv pip install -r agents/dqn/requirements.txt
```

---

## 一键安装脚本

将以下内容保存为 `scripts/setup_deps.sh`：

```bash
#!/bin/bash
set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
echo "项目根目录: $PROJECT_ROOT"

# ========== 1. 系统依赖 ==========
echo "=== 安装系统依赖 ==="
if command -v apt &> /dev/null; then
    sudo apt update
    sudo apt install -y build-essential cmake git wget \
        libssl-dev zlib1g-dev libc-ares-dev
elif command -v yum &> /dev/null; then
    sudo yum groupinstall -y "Development Tools"
    sudo yum install -y cmake git wget \
        openssl-devel zlib-devel c-ares-devel
fi

# ========== 2. Boost ==========
echo "=== 安装 Boost ==="
BOOST_DIR="$HOME/soft/boost"
if [ ! -d "$BOOST_DIR" ]; then
    mkdir -p $HOME/soft
    cd /tmp
    wget -q https://archives.boost.io/release/1.92.0/source/boost_1_92_0.tar.gz
    tar xzf boost_1_92_0.tar.gz
    cd boost_1_92_0
    ./bootstrap.sh --prefix=$BOOST_DIR
    ./b2 install --prefix=$BOOST_DIR \
        --with-thread --with-system --with-context \
        --with-coroutine --with-filesystem -j$(nproc)
    rm -rf /tmp/boost_1_92_0*
    echo "Boost 安装完成: $BOOST_DIR"
else
    echo "Boost 已存在: $BOOST_DIR"
fi

# ========== 3. gRPC ==========
echo "=== 安装 gRPC ==="
GRPC_INSTALL="$PROJECT_ROOT/third_party/grpc/install"
if [ ! -f "$GRPC_INSTALL/bin/protoc" ]; then
    mkdir -p $HOME/build && cd $HOME/build
    git clone --recursive -b v1.65.0 https://github.com/grpc/grpc.git
    cd grpc
    mkdir -p cmake/build && cd cmake/build
    cmake ../.. \
        -DCMAKE_INSTALL_PREFIX=$GRPC_INSTALL \
        -DCMAKE_BUILD_TYPE=Release \
        -DgRPC_INSTALL=ON \
        -DgRPC_BUILD_TESTS=OFF \
        -DABSL_BUILD_TESTING=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -Dprotobuf_BUILD_TESTS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    make -j$(nproc)
    make install
    rm -rf $HOME/build/grpc
    echo "gRPC 安装完成: $GRPC_INSTALL"
else
    echo "gRPC 已存在: $GRPC_INSTALL"
fi

# ========== 4. spdlog ==========
echo "=== 安装 spdlog ==="
SPDLOG_DIR="$PROJECT_ROOT/third_party/spdlog"
if [ ! -d "$SPDLOG_DIR" ]; then
    cd "$PROJECT_ROOT/third_party"
    git clone --branch v1.17.0 https://github.com/gabime/spdlog.git
    echo "spdlog 安装完成: $SPDLOG_DIR"
else
    echo "spdlog 已存在: $SPDLOG_DIR"
fi

# ========== 5. 设置环境变量 ==========
echo "=== 设置环境变量 ==="
echo "export LIBBOOST_HOME=$BOOST_DIR" >> ~/.bashrc
echo "export PATH=$BOOST_DIR/bin:\$PATH" >> ~/.bashrc
echo "export LD_LIBRARY_PATH=$BOOST_DIR/lib:\$LD_LIBRARY_PATH" >> ~/.bashrc

echo ""
echo "=== 安装完成 ==="
echo "请执行以下命令使环境变量生效："
echo "  source ~/.bashrc"
echo ""
echo "然后构建项目："
echo "  export LIBBOOST_HOME=$BOOST_DIR"
echo "  mkdir -p build && cd build && cmake .. && make -j\$(nproc)"
```

**使用方式**：
```bash
chmod +x scripts/setup_deps.sh
bash scripts/setup_deps.sh
source ~/.bashrc
```

---

## 构建与运行

```bash
# 设置环境变量
export LIBBOOST_HOME=$HOME/soft/boost

# 构建
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# 运行
cd ..
./bin/ecmsim_http_beast

# 或指定静态文件目录
./bin/ecmsim_http_beast static
```

---

## 常见问题

### Q: cmake 找不到 Boost
```bash
# 确保设置了 LIBBOOST_HOME 环境变量
export LIBBOOST_HOME=$HOME/soft/boost
echo $LIBBOOST_HOME  # 应输出 /home/xxx/soft/boost
```

### Q: gRPC 编译失败
```bash
# 确保子模块完整
cd $HOME/build/grpc
git submodule update --init --recursive --jobs 8

# 清理重新编译
rm -rf cmake/build
mkdir -p cmake/build && cd cmake/build
cmake ../.. [同上参数]
```

### Q: 链接时找不到库
```bash
# 检查库文件是否存在
ls $PROJECT_ROOT/third_party/grpc/lib/libgrpc++.a
ls $HOME/soft/boost/lib/libboost_thread.a

# 检查环境变量
echo $LIBBOOST_HOME
```

### Q: 运行时报错 "Cannot load static/index_v2.html"
```bash
# 确保在项目根目录运行，或指定正确的静态文件目录
cd $PROJECT_ROOT
./bin/ecmsim_http_beast static
```

---

## 目录结构

```
ECMSim/
├── CMakeLists.txt              # 构建配置
├── README.md                   # 本文档
├── scripts/
│   └── setup_deps.sh           # 依赖安装脚本
├── config/                     # 场景 JSON 配置
├── include/
│   ├── algo/                   # 物理常数 + 雷达方程
│   ├── entity/                 # Radar, Jammer 类
│   ├── sence/                  # SimScene + SceneManager
│   ├── jsoncpp/json/           # jsoncpp 头文件
│   └── my_http_lib/            # 旧日志头文件（已弃用）
├── src/
│   ├── algo/                   # 雷达方程实现
│   ├── entity/                 # radar.cpp, jammer.cpp
│   ├── sence/                  # sim_sence.cpp + scene_manager.cpp
│   ├── jsoncpp/                # jsoncpp 静态编译
│   └── app/
│       └── web_server_beast.cpp # 主服务器 (HTTP+gRPC+WebSocket)
├── build/                      # cmake 构建输出
├── bin/                        # 编译后的二进制文件
├── static/
│   └── index_v2.html           # 前端页面 (WebSocket + DQN)
├── test/                       # 单元测试
├── agents/dqn/                 # Python DQN 智能体
├── protos/                     # gRPC protobuf 定义
└── third_party/                # 第三方依赖（需手动安装）
    ├── grpc/                   # gRPC + Protobuf + Abseil
    │   └── install/            # 安装目录
    └── spdlog/                 # spdlog v1.17.0
```

---

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
