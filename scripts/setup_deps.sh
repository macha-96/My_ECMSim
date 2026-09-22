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
