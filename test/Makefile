# test/Makefile
CC = g++
CXX = g++
CXXFLAGS = -std=c++17 -Wall -g
# 头文件路径，必须配置才能找到include下所有头文件
INCS = -I../include

# 项目根目录
ROOT = ..

# 源码目录定义
SRC_ALGO    = $(ROOT)/src/algo
SRC_ENTITY  = $(ROOT)/src/entity
SRC_SCENE   = $(ROOT)/src/sence
# jsoncpp.cpp 直接放在src根目录，无jsoncpp子文件夹
SRC_JSON    = $(ROOT)/src/jsoncpp

# 收集各模块源码
SRCS_ALGO   := $(wildcard $(SRC_ALGO)/*.cpp)
SRCS_ENTITY := $(wildcard $(SRC_ENTITY)/*.cpp)
SRCS_SCENE  := $(wildcard $(SRC_SCENE)/*.cpp)
SRCS_JSON   :=  $(wildcard $(SRC_JSON)/*.cpp)
TEST_MAIN   := main_test.cpp

# 全部cpp源码汇总
ALL_SRCS := $(SRCS_ALGO) $(SRCS_ENTITY) $(SRCS_SCENE) $(SRCS_JSON) $(TEST_MAIN)

# 输出目录：中间文件build，可执行文件bin
BUILD_DIR = $(ROOT)/build
BIN_DIR   = $(ROOT)/bin
TARGET    = $(BIN_DIR)/sim_test

# 路径替换：../xxx/file.cpp  -> ../build/xxx/file.o
OBJS := $(patsubst $(ROOT)/%.cpp, $(BUILD_DIR)/%.o, $(ALL_SRCS))

# 链接所有obj生成可执行程序
$(TARGET): $(OBJS)
	mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCS) $(OBJS) -o $(TARGET)

# ========== 唯一通用编译规则，匹配所有带ROOT前缀的cpp ==========
# 自动创建build下对应层级目录，不用分algo/entity多条规则
$(BUILD_DIR)/%.o: $(ROOT)/%.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCS) -c $< -o $@

# 伪目标声明
.PHONY: all run clean

# 最终链接目标
all: $(TARGET)

# 编译后一键运行，根目录执行bin下程序
run: all
	cd $(ROOT) && ./bin/sim_test config/sence_config.json

# ========== Web Server ==========
# Filter out the test main source so we keep only library .o files
LIB_OBJS := $(filter-out main_test.cpp, $(OBJS))

WEB_SRC := web_server.cpp
WEB_TARGET := $(BIN_DIR)/web_serv

# Extra include path for the C HTTP library headers (my_web_app_v1.h, TcpServer.h, etc.)
# WEB_INCS := $(INCS) -I../include/my_http_lib
# Link the pre-built libreactor.so; use $ORIGIN rpath so the binary finds it at runtime
WEB_LIBS := -L../lib -lreactor -Wl,-rpath,'$$ORIGIN/../lib'

$(WEB_TARGET): $(LIB_OBJS) $(WEB_SRC)
	mkdir -p $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCS) $(LIB_OBJS) $(WEB_SRC) -o $@ $(WEB_LIBS)

.PHONY: web web-run
web: $(WEB_TARGET)

web-run: web
	cd $(ROOT) && ./bin/web_serv config/sence_config.json static

# 清理编译产物
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)