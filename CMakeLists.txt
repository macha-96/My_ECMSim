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

set(GENERATED_PROTO_SRCS
    ${GRPC_GEN_DIR}/agent_service.pb.cc
    ${GRPC_GEN_DIR}/agent_service.grpc.pb.cc
    ${GRPC_GEN_DIR}/agent_service.pb.h
    ${GRPC_GEN_DIR}/agent_service.grpc.pb.h
)

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

add_library(sim_core STATIC ${COMMON_SRCS})
target_include_directories(sim_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${GRPC_INCLUDE}
    ${CMAKE_BINARY_DIR}
    ${Boost_INCLUDE_DIRS}
    ${absl_INCLUDE_DIRS}
)
target_link_libraries(sim_core PUBLIC
    pthread
    dl
    rt
    spdlog::spdlog_header_only
)

set(APP_HTTP_BEAST ${PROJECT_SOURCE_DIR}/src/app/web_server_beast.cpp)

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

add_executable(ecmsim_http_beast ${APP_HTTP_BEAST})
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
message(STATUS "======================================")
