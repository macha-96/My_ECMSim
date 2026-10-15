set(PROTOC_EXEC ${GRPC_INSTALL}/bin/protoc)
set(GRPC_CPP_PLUGIN ${GRPC_INSTALL}/bin/grpc_cpp_plugin)
set(GRPC_GEN_DIR ${CMAKE_BINARY_DIR}/grpc_gen)
file(MAKE_DIRECTORY ${GRPC_GEN_DIR})

set(AGENT_PROTO_FILE ${PROJECT_SOURCE_DIR}/protos/agent_service.proto)

file(GLOB_RECURSE CONTAINERD_TYPE_PROTOS
    ${PROJECT_SOURCE_DIR}/protos/containerd/api/types/*.proto
)

set(CONTAINERD_SERVICE_PROTOS
    ${PROJECT_SOURCE_DIR}/protos/containerd/api/services/version/v1/version.proto
    ${PROJECT_SOURCE_DIR}/protos/containerd/api/services/containers/v1/containers.proto
    ${PROJECT_SOURCE_DIR}/protos/containerd/api/services/tasks/v1/tasks.proto
    ${PROJECT_SOURCE_DIR}/protos/containerd/api/services/images/v1/images.proto
    ${PROJECT_SOURCE_DIR}/protos/containerd/api/services/namespaces/v1/namespace.proto
    ${PROJECT_SOURCE_DIR}/protos/containerd/api/services/introspection/v1/introspection.proto
)

set(ALL_PROTO_FILES ${AGENT_PROTO_FILE} ${CONTAINERD_SERVICE_PROTOS})

set(GOOGLEAPIS_PROTO_ROOT ${PROJECT_SOURCE_DIR}/third_party/grpc/grpc/third_party/googleapis)
set(GOOGLEAPIS_EXTRA_PROTOS ${GOOGLEAPIS_PROTO_ROOT}/google/rpc/status.proto)

set(GENERATED_PROTO_SRCS)

foreach(proto ${ALL_PROTO_FILES})
    file(RELATIVE_PATH rel_proto ${PROJECT_SOURCE_DIR}/protos ${proto})
    get_filename_component(proto_name ${rel_proto} NAME_WE)
    get_filename_component(proto_dir ${rel_proto} DIRECTORY)
    set(base "${GRPC_GEN_DIR}/${proto_dir}/${proto_name}")
    list(APPEND GENERATED_PROTO_SRCS
        "${base}.pb.cc" "${base}.pb.h"
        "${base}.grpc.pb.cc" "${base}.grpc.pb.h"
    )
endforeach()

foreach(proto ${GOOGLEAPIS_EXTRA_PROTOS})
    file(RELATIVE_PATH rel_proto ${GOOGLEAPIS_PROTO_ROOT} ${proto})
    get_filename_component(proto_name ${rel_proto} NAME_WE)
    get_filename_component(proto_dir ${rel_proto} DIRECTORY)
    set(base "${GRPC_GEN_DIR}/${proto_dir}/${proto_name}")
    list(APPEND GENERATED_PROTO_SRCS "${base}.pb.cc" "${base}.pb.h")
endforeach()

foreach(proto ${CONTAINERD_TYPE_PROTOS})
    file(RELATIVE_PATH rel_proto ${PROJECT_SOURCE_DIR}/protos/containerd/api ${proto})
    get_filename_component(proto_name ${rel_proto} NAME_WE)
    get_filename_component(proto_dir ${rel_proto} DIRECTORY)
    set(base "${GRPC_GEN_DIR}/${proto_dir}/${proto_name}")
    list(APPEND GENERATED_PROTO_SRCS "${base}.pb.cc" "${base}.pb.h")
endforeach()

add_custom_command(
    OUTPUT ${GENERATED_PROTO_SRCS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GRPC_GEN_DIR}
    COMMAND ${PROTOC_EXEC}
        -I ${PROJECT_SOURCE_DIR}/protos
        -I ${PROJECT_SOURCE_DIR}/protos/containerd/api
        -I ${GOOGLEAPIS_PROTO_ROOT}
        --cpp_out=${GRPC_GEN_DIR}
        ${ALL_PROTO_FILES} google/rpc/status.proto
    COMMAND ${PROTOC_EXEC}
        -I ${PROJECT_SOURCE_DIR}/protos
        -I ${PROJECT_SOURCE_DIR}/protos/containerd/api
        -I ${GOOGLEAPIS_PROTO_ROOT}
        --grpc_out=${GRPC_GEN_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
        ${ALL_PROTO_FILES}
    COMMAND ${PROTOC_EXEC}
        -I ${PROJECT_SOURCE_DIR}/protos/containerd/api
        --cpp_out=${GRPC_GEN_DIR}
        ${CONTAINERD_TYPE_PROTOS}
    DEPENDS ${ALL_PROTO_FILES} ${GOOGLEAPIS_EXTRA_PROTOS} ${CONTAINERD_TYPE_PROTOS}
    COMMENT "Generating gRPC protobuf sources..."
)
