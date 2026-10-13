cmake_minimum_required(VERSION 3.16)
project(ECMSim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wall -g)
endif()

set(CMAKE_BINARY_DIR ${PROJECT_SOURCE_DIR}/build)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/build/lib)

if(DEFINED ENV{LIBBOOST_HOME})
    set(BOOST_ROOT $ENV{LIBBOOST_HOME})
    message(STATUS "Using local Boost from ENV LIBBOOST_HOME = ${BOOST_ROOT}")
else()
    message(FATAL_ERROR "Environment variable LIBBOOST_HOME is not set! export LIBBOOST_HOME=/home/ky/soft/boost")
endif()

set(Boost_NO_SYSTEM_PATHS ON)
find_package(Boost REQUIRED)

# ===================== spdlog (header-only) =====================
set(SPDLOG_BUILD_SHARED OFF)
set(SPDLOG_BUILD_EXAMPLE OFF)
set(SPDLOG_BUILD_TESTS OFF)
set(SPDLOG_FMT_EXTERNAL OFF)
add_subdirectory(third_party/spdlog)

set(GRPC_INSTALL ${PROJECT_SOURCE_DIR}/third_party/grpc/install)
set(GRPC_INCLUDE ${GRPC_INSTALL}/include)
set(GRPC_LIB_DIR ${GRPC_INSTALL}/lib)

include_directories(${GRPC_INCLUDE})
link_directories(${GRPC_LIB_DIR})

set(absl_DIR ${GRPC_INSTALL}/lib/cmake/absl)
find_package(absl REQUIRED)

include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/build
    ${Boost_INCLUDE_DIRS}
    ${absl_INCLUDE_DIRS}
)

set(PROTOC_EXEC ${GRPC_INSTALL}/bin/protoc)
set(GRPC_CPP_PLUGIN ${GRPC_INSTALL}/bin/grpc_cpp_plugin)

set(PROTO_FILE ${PROJECT_SOURCE_DIR}/protos/agent_service.proto)
set(GRPC_GEN_DIR ${CMAKE_BINARY_DIR}/grpc_gen)
file(MAKE_DIRECTORY ${GRPC_GEN_DIR})

# ========== 1. 收集所有proto文件 ==========
# agent 自有proto
set(AGENT_PROTO_FILE ${PROJECT_SOURCE_DIR}/protos/agent_service.proto)

# containerd 类型依赖proto（只被 import，但生成的 .pb.h 会被 service 生成代码 include，必须生成）
# 覆盖 service proto 传递依赖的全部 types（descriptor/metrics/mount/platform/task 等）
file(GLOB_RECURSE CONTAINERD_TYPE_PROTOS
    ${PROJECT_SOURCE_DIR}/protos/containerd/api/types/*.proto
)

# containerd 需要调用的服务proto（只选service，types/events不要放这里！）
set(CONTAINERD_SERVICE_PROTOS
    ${PROJECT_SOURCE_DIR}/protos/containerd/api/services/version/v1/version.proto
    ${PROJECT_SOURCE_DIR}/protos/containerd/api/services/containers/v1/containers.proto
    ${PROJECT_SOURCE_DIR}/protos/containerd/api/services/tasks/v1/tasks.proto
    ${PROJECT_SOURCE_DIR}/protos/containerd/api/services/images/v1/images.proto
    ${PROJECT_SOURCE_DIR}/protos/containerd/api/services/namespaces/v1/namespace.proto
    ${PROJECT_SOURCE_DIR}/protos/containerd/api/services/introspection/v1/introspection.proto
)

# 合并全部proto列表（protoc 的 cpp_out/grpc_out 输入，只含 agent + services）
set(ALL_PROTO_FILES
    ${AGENT_PROTO_FILE}
    ${CONTAINERD_SERVICE_PROTOS}
)

# googleapis 源码目录
set(GOOGLEAPIS_PROTO_ROOT ${PROJECT_SOURCE_DIR}/third_party/grpc/grpc/third_party/googleapis)
# 被 import、且需要生成 pb 源码的 googleapis proto
set(GOOGLEAPIS_EXTRA_PROTOS
    ${GOOGLEAPIS_PROTO_ROOT}/google/rpc/status.proto
)

# ========== 2. 生成对应的pb/grpc.pb文件名列表 ==========
# 输出生成文件变量
set(GENERATED_PROTO_SRCS)

# 1. 收集service/agent生成文件
foreach(proto ${ALL_PROTO_FILES})
    file(RELATIVE_PATH rel_proto ${PROJECT_SOURCE_DIR}/protos ${proto})
    get_filename_component(proto_name ${rel_proto} NAME_WE)
    get_filename_component(proto_dir ${rel_proto} DIRECTORY)
    set(base "${GRPC_GEN_DIR}/${proto_dir}/${proto_name}")
    list(APPEND GENERATED_PROTO_SRCS
        "${base}.pb.cc"
        "${base}.pb.h"
        "${base}.grpc.pb.cc"
        "${base}.grpc.pb.h"
    )
endforeach()

# 1.1 googleapis 生成文件（相对 googleapis 根目录布局：google/rpc/status.pb.h）
foreach(proto ${GOOGLEAPIS_EXTRA_PROTOS})
    file(RELATIVE_PATH rel_proto ${GOOGLEAPIS_PROTO_ROOT} ${proto})
    get_filename_component(proto_name ${rel_proto} NAME_WE)
    get_filename_component(proto_dir ${rel_proto} DIRECTORY)
    set(base "${GRPC_GEN_DIR}/${proto_dir}/${proto_name}")
    list(APPEND GENERATED_PROTO_SRCS
        "${base}.pb.cc"
        "${base}.pb.h"
    )
endforeach()

# 2. 收集types依赖proto生成文件（types没有grpc stub，只有pb.h/pb.cc！！！）
# 输出路径相对 protos/containerd/api，即 grpc_gen/types/*.pb.{h,cc}
foreach(proto ${CONTAINERD_TYPE_PROTOS})
    file(RELATIVE_PATH rel_proto ${PROJECT_SOURCE_DIR}/protos/containerd/api ${proto})
    get_filename_component(proto_name ${rel_proto} NAME_WE)
    get_filename_component(proto_dir ${rel_proto} DIRECTORY)
    set(base "${GRPC_GEN_DIR}/${proto_dir}/${proto_name}")
    list(APPEND GENERATED_PROTO_SRCS
        "${base}.pb.cc"
        "${base}.pb.h"
        # types没有grpc服务，不要加 .grpc.pb.cc/.h
    )
endforeach()

# ========== 3. 批量protoc生成命令 ==========
# 注意：types 必须用独立的 protoc 调用。
# 同一物理文件若同时以 "containerd/api/types/x.proto"（-I protos）和
# "types/x.proto"（-I protos/containerd/api）两个名字注册进 descriptor pool，
# 会触发 "already defined" 重复定义错误，因此两次调用的 -I 根互不重叠。
add_custom_command(
    OUTPUT ${GENERATED_PROTO_SRCS}
    # protoc 不会创建顶层输出目录（file(MAKE_DIRECTORY) 只在 configure 期执行一次）
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GRPC_GEN_DIR}
    # 3.1 agent + containerd services + googleapis：输出 grpc_gen/containerd/api/... 与 grpc_gen/google/...
    COMMAND ${PROTOC_EXEC}
        -I ${PROJECT_SOURCE_DIR}/protos
        -I ${PROJECT_SOURCE_DIR}/protos/containerd/api
        -I ${GOOGLEAPIS_PROTO_ROOT}
        --cpp_out=${GRPC_GEN_DIR}
        ${ALL_PROTO_FILES}
        google/rpc/status.proto
    COMMAND ${PROTOC_EXEC}
        -I ${PROJECT_SOURCE_DIR}/protos
        -I ${PROJECT_SOURCE_DIR}/protos/containerd/api
        -I ${GOOGLEAPIS_PROTO_ROOT}
        --grpc_out=${GRPC_GEN_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
        ${ALL_PROTO_FILES}
    # 3.2 containerd types：唯一 -I 根为 protos/containerd/api，输出 grpc_gen/types/...
    #     （service 生成代码里 #include "types/xxx.pb.h" 由 -I grpc_gen 解析）
    COMMAND ${PROTOC_EXEC}
        -I ${PROJECT_SOURCE_DIR}/protos/containerd/api
        --cpp_out=${GRPC_GEN_DIR}
        ${CONTAINERD_TYPE_PROTOS}
    DEPENDS
        ${ALL_PROTO_FILES}
        ${GOOGLEAPIS_EXTRA_PROTOS}
        ${CONTAINERD_TYPE_PROTOS} # 关键！types proto作为依赖，proto文件改动会触发重生成
    COMMENT "Generating gRPC protobuf sources (agent + containerd api)..."
)

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

# 定义googleapis源码目录
set(GOOGLEAPIS_PROTO_ROOT ${PROJECT_SOURCE_DIR}/third_party/grpc/grpc/third_party/googleapis)

add_library(sim_core STATIC ${COMMON_SRCS})
target_include_directories(sim_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${GRPC_INCLUDE}
    ${CMAKE_BINARY_DIR}
    ${GRPC_GEN_DIR} # 新增：让代码能 #include "containerd/api/services/containers/v1/containers.pb.h"
    ${GRPC_GEN_DIR}/containerd/api
    ${GOOGLEAPIS_PROTO_ROOT}  # <<====新增，解决 google/rpc/status.pb.h
    ${Boost_INCLUDE_DIRS}
    ${absl_INCLUDE_DIRS}
)
target_link_libraries(sim_core PUBLIC
    pthread
    dl
    rt
    spdlog::spdlog_header_only
)

set(APP_HTTP_BEAST_SRCS
    ${PROJECT_SOURCE_DIR}/src/app/web_server_beast.cpp
    ${PROJECT_SOURCE_DIR}/src/app/framework/router.cpp
    ${PROJECT_SOURCE_DIR}/src/app/framework/http_session.cpp
    ${PROJECT_SOURCE_DIR}/src/app/framework/ws_session.cpp
    ${PROJECT_SOURCE_DIR}/src/app/handlers/common.cpp
    ${PROJECT_SOURCE_DIR}/src/app/handlers/routes.cpp
    ${PROJECT_SOURCE_DIR}/src/app/handlers/grpc_service.cpp
)

set(GRPC_LIBS
    ${GRPC_LIB_DIR}/libgrpc++.a
    ${GRPC_LIB_DIR}/libgrpc.a
    ${GRPC_LIB_DIR}/libgpr.a
    ${GRPC_LIB_DIR}/libaddress_sorting.a
    ${GRPC_LIB_DIR}/libupb_base_lib.a
    ${GRPC_LIB_DIR}/libupb_hash_lib.a
    ${GRPC_LIB_DIR}/libupb_json_lib.a
    ${GRPC_LIB_DIR}/libupb_lex_lib.a
    ${GRPC_LIB_DIR}/libupb_mem_lib.a
    ${GRPC_LIB_DIR}/libupb_message_lib.a
    ${GRPC_LIB_DIR}/libupb_mini_descriptor_lib.a
    ${GRPC_LIB_DIR}/libupb_mini_table_lib.a
    ${GRPC_LIB_DIR}/libupb_reflection_lib.a
    ${GRPC_LIB_DIR}/libupb_textformat_lib.a
    ${GRPC_LIB_DIR}/libupb_wire_lib.a
    ${GRPC_LIB_DIR}/libutf8_range_lib.a
    ${GRPC_LIB_DIR}/libutf8_validity.a
    ${GRPC_LIB_DIR}/libutf8_range.a
    ${GRPC_LIB_DIR}/libre2.a
    ${GRPC_LIB_DIR}/libssl.a
    ${GRPC_LIB_DIR}/libcrypto.a
    ${GRPC_LIB_DIR}/libz.a
    ${GRPC_LIB_DIR}/libcares.a
    ${GRPC_LIB_DIR}/libprotobuf.a
)

add_executable(ecmsim_http_beast ${APP_HTTP_BEAST_SRCS})
target_link_libraries(ecmsim_http_beast
    PRIVATE
        sim_core
        -Wl,--start-group
        ${GRPC_LIBS}
        ${Boost_LIBRARIES}
        absl::base
        absl::strings
        absl::cord
        absl::status
        absl::statusor
        absl::time
        absl::civil_time
        absl::strings_internal
        absl::str_format_internal
        absl::synchronization
        absl::graphcycles_internal
        absl::kernel_timeout_internal
        absl::stacktrace
        absl::symbolize
        absl::debugging_internal
        absl::demangle_internal
        absl::demangle_rust
        absl::decode_rust_punycode
        absl::malloc_internal
        absl::tracing_internal
        absl::raw_logging_internal
        absl::log_severity
        absl::spinlock_wait
        absl::throw_delegate
        absl::base_internal
        absl::int128
        absl::no_destructor
        absl::hashtablez_sampler
        absl::raw_hash_set
        absl::hash
        absl::city
        absl::low_level_hash
        absl::exponential_biased
        absl::cordz_handle
        absl::cordz_info
        absl::cordz_functions
        absl::cord_internal
        absl::crc32c
        absl::crc_cord_state
        absl::crc_internal
        absl::crc_cpu_detect
        absl::leak_check
        absl::strerror
        absl::log_internal_proto
        absl::log_internal_message
        absl::log_internal_check_op
        absl::log_internal_conditions
        absl::log_internal_format
        absl::log_internal_globals
        absl::log_internal_structured_proto
        absl::log_internal_log_sink_set
        absl::log_sink
        absl::log
        absl::log_initialize
        absl::log_globals
        absl::log_flags
        absl::flags_internal
        absl::flags_marshalling
        absl::flags_reflection
        absl::flags_private_handle_accessor
        absl::flags_commandlineflag
        absl::flags_commandlineflag_internal
        absl::flags_config
        absl::flags_program_name
        absl::flags_usage_internal
        absl::flags_usage
        absl::flags_parse
        absl::flags
        absl::random_random
        absl::random_distributions
        absl::random_seed_sequences
        absl::random_seed_gen_exception
        absl::random_internal_randen
        absl::random_internal_randen_hwaes
        absl::random_internal_randen_hwaes_impl
        absl::random_internal_randen_slow
        absl::random_internal_platform
        absl::random_internal_entropy_pool
        absl::random_internal_seed_material
        absl::random_internal_distribution_caller
        absl::random_internal_fast_uniform_bits
        absl::random_internal_pcg_engine
        absl::random_internal_iostream_state_saver
        -Wl,--end-group
        pthread
        dl
        rt
        ssl
        crypto
        z
        cares
        re2
)
target_include_directories(ecmsim_http_beast PRIVATE
    ${GRPC_INCLUDE}
    ${Boost_INCLUDE_DIRS}
    ${absl_INCLUDE_DIRS}
    ${GRPC_GEN_DIR} # 这里也要加上，方便引用containerd生成头文件
    ${GRPC_GEN_DIR}/containerd/api  # <<=====新增
)
set_target_properties(ecmsim_http_beast PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib"
    BUILD_RPATH "$ORIGIN/../lib"
)

message(STATUS "======================================")
message(STATUS "ECMSim CMake configure done")
message(STATUS "Boost_INCLUDE_DIRS: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost_VERSION: ${Boost_VERSION}")
message(STATUS "Build: mkdir build && cd build && cmake .. && make -j$(nproc)")
message(STATUS "Output binary: ${PROJECT_SOURCE_DIR}/bin/")
message(STATUS "Total proto files: ${ALL_PROTO_FILES}")
message(STATUS "======================================")
