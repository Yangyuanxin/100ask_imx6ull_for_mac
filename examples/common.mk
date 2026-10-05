# ============================================================
# 100ask_imx6ull 例程通用编译规则
# 每个例程的 Makefile 只需:
#     include ../common.mk
# 可选变量: SRCS / TARGET / CFLAGS / LDLIBS / INC
# ============================================================
COMMON := $(abspath $(lastword $(MAKEFILE_LIST)))
EXDIR  := $(patsubst %/,%,$(dir $(COMMON)))
ROOT   := $(patsubst %/,%,$(dir $(EXDIR)))

# ---- 工具链 (musl, 静态链接, 不依赖板子上的 glibc 版本) ----
TC_DIR ?= $(ROOT)/tools/toolchain/armv7-unknown-linux-musleabihf/bin
CROSS  ?= $(TC_DIR)/armv7-unknown-linux-musleabihf-
CC     := $(CROSS)gcc
CXX    := $(CROSS)g++
AR     := $(CROSS)ar
STRIP  := $(CROSS)strip

# ---- i.MX6ULL: Cortex-A7, NEON, 硬浮点 ----
ARCHFLAGS := -march=armv7-a -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard

TARGET    ?= $(notdir $(CURDIR))
BUILD_DIR ?= $(ROOT)/build/$(TARGET)

INC    +=
SRCS   ?= $(wildcard *.c)
SRCS   += $(wildcard *.cpp)
OBJS   := $(addprefix $(BUILD_DIR)/,$(SRCS:.c=.o))
OBJS   := $(OBJS:.cpp=.o)
DEPS   := $(OBJS:.o=.d)

CFLAGS   += -std=gnu11  -O2 -Wall -Wextra $(ARCHFLAGS) $(addprefix -I,$(INC))
CXXFLAGS += -std=gnu++17 -O2 -Wall -Wextra $(ARCHFLAGS) $(addprefix -I,$(INC))
LDFLAGS  += -static
LDLIBS   +=

.PHONY: all size clean run hist

all: $(BUILD_DIR)/$(TARGET)
	@echo "[OK] $(BUILD_DIR)/$(TARGET)"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@ $(LDLIBS)
	$(STRIP) $@ 2>/dev/null || true
	@$(CROSS)size $@ 2>/dev/null || true

size:
	$(CROSS)size $(BUILD_DIR)/$(TARGET)

# 交叉编译 -> 通过串口 ZMODEM 下发 -> 在板子上运行
# RUNWAIT: 板上程序预计运行时间(秒), 越大越容易拿到完整输出
RUNWAIT ?= 6
run: all
	$(ROOT)/tools/imx.py run $(BUILD_DIR)/$(TARGET) -w $(RUNWAIT) $(if $(RUNARGS),-a "$(RUNARGS)",)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
