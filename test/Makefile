# test/Makefile
CC = g++
CXX = g++
CXXFLAGS = -std=c++17 -Wall -g
INCS = -I../include

ROOT = ..

SRC_ALGO    = $(ROOT)/src/algo
SRC_ENTITY  = $(ROOT)/src/entity
SRC_SCENE   = $(ROOT)/src/sence
SRC_JSON    = $(ROOT)/src/jsoncpp

SRCS_ALGO   := $(wildcard $(SRC_ALGO)/*.cpp)
SRCS_ENTITY := $(wildcard $(SRC_ENTITY)/*.cpp)
SRCS_SCENE  := $(wildcard $(SRC_SCENE)/*.cpp)
SRCS_JSON   := $(wildcard $(SRC_JSON)/*.cpp)
TEST_MAIN   := main_test.cpp

ALL_SRCS := $(SRCS_ALGO) $(SRCS_ENTITY) $(SRCS_SCENE) $(SRCS_JSON) $(TEST_MAIN)

BUILD_DIR = $(ROOT)/build
BIN_DIR   = $(ROOT)/bin
TARGET    = $(BIN_DIR)/sim_test

OBJS := $(patsubst $(ROOT)/%.cpp, $(BUILD_DIR)/%.o, $(ALL_SRCS))

$(TARGET): $(OBJS)
	mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCS) $(OBJS) -o $(TARGET)

$(BUILD_DIR)/%.o: $(ROOT)/%.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCS) -c $< -o $@

.PHONY: all run clean
all: $(TARGET)
run: all
	cd $(ROOT) && ./bin/sim_test config/sence_config.json

# ========== Web Server V1 (HTTP only) ==========
LIB_OBJS := $(filter-out main_test.cpp, $(OBJS))

WEB_SRC := web_server.cpp
WEB_TARGET := $(BIN_DIR)/web_serv
WEB_LIBS := -L../lib -lreactor -Wl,-rpath,'$$ORIGIN/../lib'

$(WEB_TARGET): $(LIB_OBJS) $(WEB_SRC)
	mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCS) $(LIB_OBJS) $(WEB_SRC) -o $@ $(WEB_LIBS)

.PHONY: web web-run
web: $(WEB_TARGET)
web-run: web
	cd $(ROOT) && ./bin/web_serv config/sence_config.json static

# ========== gRPC Proto Generation ==========
GRPC_INSTALL := $(ROOT)/third_party/grpc/install
GRPC_GEN_DIR := $(ROOT)/build/grpc_gen
PROTOS_DIR   := $(ROOT)/protos
PROTOC       := $(GRPC_INSTALL)/bin/protoc
GRPC_CPP_PLUGIN := $(GRPC_INSTALL)/bin/grpc_cpp_plugin

.PHONY: proto
proto:
	mkdir -p $(GRPC_GEN_DIR)
	$(PROTOC) -I $(PROTOS_DIR) --grpc_out=$(GRPC_GEN_DIR) --plugin=protoc-gen-grpc=$(GRPC_CPP_PLUGIN) $(PROTOS_DIR)/agent_service.proto
	$(PROTOC) -I $(PROTOS_DIR) --cpp_out=$(GRPC_GEN_DIR) $(PROTOS_DIR)/agent_service.proto

# ========== gRPC build variables ==========
GRPC_HDR_DIR := $(GRPC_INSTALL)/include
PKG_CONFIG_PATH := $(GRPC_INSTALL)/lib/pkgconfig:/tmp
GRPC_CFLAGS := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags grpc++ protobuf 2>/dev/null)
GRPC_LIBS   := $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --static --libs grpc++ protobuf 2>/dev/null)
GRPC_PB_SRCS  := $(GRPC_GEN_DIR)/agent_service.pb.cc $(GRPC_GEN_DIR)/agent_service.grpc.pb.cc
GRPC_PB_OBJS  := $(GRPC_PB_SRCS:.cc=.o)

$(GRPC_GEN_DIR)/%.o: $(GRPC_GEN_DIR)/%.cc
	$(CXX) $(CXXFLAGS) $(GRPC_CFLAGS) -I$(ROOT)/include -c $< -o $@

# ========== Web Server V2 (HTTP + gRPC) ==========
V2_INCS := -I$(ROOT)/include -I$(GRPC_HDR_DIR) -I$(ROOT)/include/my_http_lib
V2_LIBS := -L$(ROOT)/lib -lreactor -Wl,-rpath,'$$ORIGIN/../lib'

WEB_V2_SRC    := web_server_v2.cpp
WEB_V2_TARGET := $(BIN_DIR)/web_serv_v2
$(WEB_V2_TARGET): $(LIB_OBJS) $(GRPC_PB_OBJS) $(WEB_V2_SRC)
	mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(V2_INCS) $(LIB_OBJS) $(GRPC_PB_OBJS) $(WEB_V2_SRC) -o $@ $(V2_LIBS) $(GRPC_LIBS) -lpthread -ldl -lrt
web-v2: proto $(WEB_V2_TARGET)
web-v2-run: web-v2
	cd $(ROOT) && ./bin/web_serv_v2 static

# ========== Web Server V3 (RESTful API + gRPC) ==========
WEB_V3_SRC    := web_server_v3.cpp
WEB_V3_TARGET := $(BIN_DIR)/web_serv_v3
$(WEB_V3_TARGET): $(LIB_OBJS) $(GRPC_PB_OBJS) $(WEB_V3_SRC)
	mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(V2_INCS) $(LIB_OBJS) $(GRPC_PB_OBJS) $(WEB_V3_SRC) -o $@ $(V2_LIBS) $(GRPC_LIBS) -lpthread -ldl -lrt
.PHONY: web-v3 web-v3-run
web-v3: proto $(WEB_V3_TARGET)
web-v3-run: web-v3
	cd $(ROOT) && ./bin/web_serv_v3 static

# ========== Clean ==========
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) $(GRPC_PB_OBJS)
