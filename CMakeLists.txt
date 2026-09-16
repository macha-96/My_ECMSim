cmake_minimum_required(VERSION 3.16)

project(ECMSim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译警告，对齐原Makefile -Wall
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -g)
endif()

# 输出目录，和原Makefile对齐 build/ bin/
set(CMAKE_BINARY_DIR ${PROJECT_SOURCE_DIR}/build)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/lib)

# ===================== Boost 局部安装（读取环境变量 LIBBOOST_HOME） =====================
if(DEFINED ENV{LIBBOOST_HOME})
    set(BOOST_ROOT $ENV{LIBBOOST_HOME})
    message(STATUS "Using local Boost from ENV LIBBOOST_HOME = ${BOOST_ROOT}")
else()
    message(FATAL_ERROR "Environment variable LIBBOOST_HOME is not set! export LIBBOOST_HOME=/home/ky/soft/boost")
endif()

# 强制只在BOOST_ROOT查找，不扫描系统boost
set(Boost_NO_SYSTEM_PATHS ON)
# 你用到beast，属于header-only；如果后续需要thread/system等库，在这里追加组件，例如 COMPONENTS thread system
find_package(Boost REQUIRED)

# ===================== 头文件路径 =====================
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/build          # proto生成代码输出目录
    ${Boost_INCLUDE_DIRS}                # 追加Boost头文件
)

# ===================== 第三方：本地预编译grpc =====================
set(GRPC_INSTALL ${PROJECT_SOURCE_DIR}/third_party/grpc/install)
set(GRPC_INCLUDE ${GRPC_INSTALL}/include)
set(GRPC_LIB_DIR ${GRPC_INSTALL}/lib)

include_directories(${GRPC_INCLUDE})
link_directories(${GRPC_LIB_DIR})

# protoc & grpc_cpp_plugin
set(PROTOC_EXEC ${GRPC_INSTALL}/bin/protoc)
set(GRPC_CPP_PLUGIN ${GRPC_INSTALL}/bin/grpc_cpp_plugin)

# proto文件
set(PROTO_FILE ${PROJECT_SOURCE_DIR}/protos/agent_service.proto)
set(GRPC_GEN_DIR ${CMAKE_BINARY_DIR}/grpc_gen)
file(MAKE_DIRECTORY ${GRPC_GEN_DIR})

set(GENERATED_PROTO_SRCS
    ${GRPC_GEN_DIR}/agent_service.pb.cc
    ${GRPC_GEN_DIR}/agent_service.grpc.pb.cc
    ${GRPC_GEN_DIR}/agent_service.pb.h
    ${GRPC_GEN_DIR}/agent_service.grpc.pb.h
)

# 自定义命令生成protobuf&grpc代码
add_custom_command(
    OUTPUT ${GENERATED_PROTO_SRCS}
    COMMAND ${PROTOC_EXEC}
        -I ${PROJECT_SOURCE_DIR}/protos
        --cpp_out=${GRPC_GEN_DIR}
        ${PROTO_FILE}
    COMMAND ${PROTOC_EXEC}
        -I ${PROJECT_SOURCE_DIR}/protos
        --grpc_out=${GRPC_GEN_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating gRPC protobuf sources..."
)

# ===================== 收集项目C++源码（不含app下main，先做公共库） =====================
file(GLOB ALGO_SRCS     ${PROJECT_SOURCE_DIR}/src/algo/*.cpp)
file(GLOB ENTITY_SRCS   ${PROJECT_SOURCE_DIR}/src/entity/*.cpp)
file(GLOB SCENE_SRCS    ${PROJECT_SOURCE_DIR}/src/sence/*.cpp)
file(GLOB JSONCPP_SRCS  ${PROJECT_SOURCE_DIR}/src/jsoncpp/*.cpp)

set(COMMON_SRCS
    ${ALGO_SRCS}
    ${ENTITY_SRCS}
    ${SCENE_SRCS}
    ${JSONCPP_SRCS}
    ${GENERATED_PROTO_SRCS}
)

# 公共静态库：sim_core，业务逻辑+proto生成代码
add_library(sim_core STATIC ${COMMON_SRCS})
target_include_directories(sim_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${GRPC_INCLUDE}
    ${CMAKE_BINARY_DIR}
    ${Boost_INCLUDE_DIRS}
)

# 链接系统库
target_link_libraries(sim_core PUBLIC
    pthread
    dl
    rt
)

# ===================== 引入外部项目 my_simple_webserver(boost.beast) =====================
set(MY_WEBSERVER_PATH ${PROJECT_SOURCE_DIR}/../cppprojects/my_simple_webserver/my_simple_webserver)
# 把Boost路径传递给子CMake，保证子项目也使用同一个局部Boost
set(BOOST_ROOT ${BOOST_ROOT} CACHE PATH "Boost root for subproject" FORCE)
add_subdirectory(${MY_WEBSERVER_PATH} ${CMAKE_BINARY_DIR}/my_simple_webserver_build)

# ===================== 可执行文件：src/app 下main函数 =====================
# src/app 下面两个文件 http_server_fast.cpp / websocket_server_fast.cpp，你可以选其中一个作为main，也可以两个都生成
set(APP_HTTP_FAST ${PROJECT_SOURCE_DIR}/src/app/http_server_fast.cpp)
set(APP_WS_FAST   ${PROJECT_SOURCE_DIR}/src/app/websocket_server_fast.cpp)

# 可执行1：http_server_fast
add_executable(ecmsim_http ${APP_HTTP_FAST})
target_link_libraries(ecmsim_http
    PRIVATE
        sim_core
        my_simple_webserver_core
        ${PROJECT_SOURCE_DIR}/lib/libreactor.so
        grpc++
        protobuf
        ${Boost_LIBRARIES}
)

# rpath，运行时找libreactor.so，和原Makefile $ORIGIN/../lib一致
set_target_properties(ecmsim_http PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib"
    BUILD_RPATH "$ORIGIN/../lib"
)

# 可执行2：websocket_server_fast
add_executable(ecmsim_ws ${APP_WS_FAST})
target_link_libraries(ecmsim_ws
    PRIVATE
        sim_core
        my_simple_webserver_core
        ${PROJECT_SOURCE_DIR}/lib/libreactor.so
        grpc++
        protobuf
        ${Boost_LIBRARIES}
)
set_target_properties(ecmsim_ws PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib"
    BUILD_RPATH "$ORIGIN/../lib"
)

# ===================== 可选：test目录，如果你后续想把test也纳入cmake，这里暂时不处理，继续用Makefile也可以 =====================

message(STATUS "======================================")
message(STATUS "ECMSim CMake configure done")
message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost_VERSION: ${Boost_VERSION}")
message(STATUS "Build: mkdir build && cd build && cmake .. && make -j$(nproc)")
message(STATUS "Output binary: ${PROJECT_SOURCE_DIR}/bin/")
message(STATUS "======================================")
