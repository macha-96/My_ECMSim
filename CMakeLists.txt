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
    message(STATUS "Boost from ENV: ${BOOST_ROOT}")
else()
    message(FATAL_ERROR "LIBBOOST_HOME not set! export LIBBOOST_HOME=/path/to/boost")
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

list(APPEND CMAKE_PREFIX_PATH ${GRPC_INSTALL})
find_package(protobuf REQUIRED CONFIG)
find_package(gRPC REQUIRED CONFIG)
find_package(absl REQUIRED CONFIG)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(bcrypt)
include(protobuf)

file(GLOB ALGO_SRCS    ${PROJECT_SOURCE_DIR}/src/algo/*.cpp)
file(GLOB ENTITY_SRCS  ${PROJECT_SOURCE_DIR}/src/entity/*.cpp)
file(GLOB SCENE_SRCS   ${PROJECT_SOURCE_DIR}/src/sence/*.cpp)
file(GLOB JSONCPP_SRCS ${PROJECT_SOURCE_DIR}/src/jsoncpp/*.cpp)

set(COMMON_SRCS
    ${ALGO_SRCS} ${ENTITY_SRCS} ${SCENE_SRCS}
    ${JSONCPP_SRCS} ${GENERATED_PROTO_SRCS}
)

add_library(sim_core STATIC ${COMMON_SRCS})
target_include_directories(sim_core PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/bcrypt
    ${GRPC_INCLUDE}
    ${CMAKE_BINARY_DIR}
    ${GRPC_GEN_DIR}
    ${GRPC_GEN_DIR}/containerd/api
    ${GOOGLEAPIS_PROTO_ROOT}
    ${Boost_INCLUDE_DIRS}
    ${absl_INCLUDE_DIRS}
)
target_link_libraries(sim_core PUBLIC pthread dl rt spdlog::spdlog_header_only)

set(APP_HTTP_BEAST_SRCS
    ${PROJECT_SOURCE_DIR}/src/app/web_server_beast.cpp
    ${PROJECT_SOURCE_DIR}/src/app/framework/router.cpp
    ${PROJECT_SOURCE_DIR}/src/app/framework/http_session.cpp
    ${PROJECT_SOURCE_DIR}/src/app/framework/ws_session.cpp
    ${PROJECT_SOURCE_DIR}/src/app/framework/static_resource.cpp
    ${PROJECT_SOURCE_DIR}/src/app/framework/common.cpp
    ${PROJECT_SOURCE_DIR}/src/app/service/web_service.cpp
    ${PROJECT_SOURCE_DIR}/src/app/service/grpc_service.cpp
)

add_executable(ecmsim_http_beast ${APP_HTTP_BEAST_SRCS})
target_link_libraries(ecmsim_http_beast PRIVATE sim_core bcrypt gRPC::grpc++)

include(mysql_conncpp)

target_link_libraries(ecmsim_http_beast PRIVATE
    ${Boost_LIBRARIES}
    pthread dl rt ssl crypto z
)

target_include_directories(ecmsim_http_beast PRIVATE
    ${GRPC_INCLUDE}
    ${Boost_INCLUDE_DIRS}
    ${absl_INCLUDE_DIRS}
    ${GRPC_GEN_DIR}
    ${GRPC_GEN_DIR}/containerd/api
    ${PROJECT_SOURCE_DIR}/include/bcrypt
)

set_target_properties(ecmsim_http_beast PROPERTIES
    INSTALL_RPATH "$ORIGIN/../lib"
    BUILD_RPATH "$ORIGIN/../lib"
)

message(STATUS "======================================")
message(STATUS "ECMSim configured. Build: cd build && make -j")
message(STATUS "======================================")
