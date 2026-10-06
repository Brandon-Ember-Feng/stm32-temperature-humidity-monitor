# =============================================================================
# STM32F103C8T6 温湿度监测仪 —— 顶层构建脚本
#
# 常用目标：
#   make                 构建 firmware（elf / hex / bin）
#   make size            显示 Flash / RAM 占用
#   make flash           烧录（SWD，烧完自动复位）
#   make check-layering  校验分层架构约束（寄存器是否被隔离在 BSP 层）
#   make test            PC 端单元测试（随事项 2 落地）
#   make clean           清理
#
# 设计说明：本工程**不依赖 IDE**。CubeIDE 工程文件仍保留，方便你用 IDE 调试，
# 但命令行构建走这个 Makefile —— 这样才能接进 CI（见事项 2）。
# =============================================================================

# ---- shell ------------------------------------------------------------------
# Windows 上 make 会自己到 PATH 里找一个 sh.exe 当 shell。如果 PATH 前面存在
# "假的" shell shim（沙箱 / 工具链会塞壳脚本进去，本项目环境里就有），make 会拿它
# 去启动进程，CreateProcessW 直接失败、报 "make (e=87): 参数错误" —— 而且每次
# 失败的文件还不一样，极具迷惑性。这里显式指向 Git for Windows 自带的 sh 绕开探测。
# 非 Windows 平台不进入这个分支，保持用系统默认的 /bin/sh。
ifeq ($(OS),Windows_NT)
  _GIT_SH := $(firstword \
                $(wildcard C:/Program Files/Git/usr/bin/sh.exe) \
                $(wildcard C:/Program Files/Git/bin/sh.exe) \
                $(wildcard C:/Program Files/Git/usr/bin/bash.exe))
  ifneq ($(_GIT_SH),)
    SHELL := $(_GIT_SH)
  endif
endif

TARGET     := temp-monitor
BUILD_DIR  := build

# ---- 宿主编译器（只给 PC 端单元测试用）--------------------------------------
# 默认 cc：Linux / macOS 自带，GitHub Actions 的 ubuntu runner 上也是它。
# Windows 上通常没有 cc，退一步用 zig —— `zig cc` 完全兼容 gcc 的命令行参数。
HOSTCC ?= cc
ifeq ($(OS),Windows_NT)
  EXE := .exe
  ifeq ($(shell command -v cc 2>/dev/null),)
    ifneq ($(shell command -v zig 2>/dev/null),)
      HOSTCC := zig cc
    endif
  endif
else
  EXE :=
endif

# ---- 工具链 -----------------------------------------------------------------
# 默认用本机 STM32CubeIDE 自带的 arm-none-eabi 工具链；也可直接把工具链加进
# PATH 后执行 `make TOOLCHAIN=` （留空即使用 PATH 中的 arm-none-eabi-*）。
TOOLCHAIN ?= C:/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin
PREFIX    := $(TOOLCHAIN)/arm-none-eabi-

CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
SIZE    := $(PREFIX)size

# 烧录工具（STM32CubeProgrammer），不在 PATH 时可覆盖：
#   make flash PROGRAMMER="C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"
PROGRAMMER ?= STM32_Programmer_CLI

# 分层校验用（可用 PYTHON= 覆盖）
PYTHON ?= python

# ---- 源文件 -----------------------------------------------------------------
LDSCRIPT    := STM32F103C8TX_FLASH.ld
INCLUDES    := -IBSP -IUtil -IDrivers -ICore

C_SOURCES   := $(wildcard BSP/*.c) $(wildcard Util/*.c) \
               $(wildcard Drivers/*.c) $(wildcard Core/*.c)
ASM_SOURCES := Startup/startup_stm32f103c8tx.s

# ---- 编译选项 ---------------------------------------------------------------
CPU     := -mcpu=cortex-m3
DEFS    := -DSTM32F103xB

# 注意：-Wall -Wextra 且要求零警告 —— 这是本工程的硬性门槛，CI 也会检查
CFLAGS  := $(CPU) -mthumb -std=c11 -Wall -Wextra -O1 -g \
           -ffunction-sections -fdata-sections $(DEFS) $(INCLUDES)

LDFLAGS := $(CPU) -mthumb -T $(LDSCRIPT) \
           -Wl,-Map=$(BUILD_DIR)/$(TARGET).map -Wl,--gc-sections
LDLIBS  := -specs=nano.specs -specs=nosys.specs

OBJECTS := $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o))) \
           $(BUILD_DIR)/startup_stm32f103c8tx.o
vpath %.c $(sort $(dir $(C_SOURCES)))

# =============================================================================

.PHONY: all clean flash size check-layering test

all: check-layering $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

# 架构校验做成 order-only 依赖：保证它在任何编译动作之前跑完，违规即中断；
# 但它的时间戳不参与"这个 .o 要不要重建"的判断，所以不会因为校验而全量重编。
# （原先是塞在 recipe 里用 > /dev/null 重定向，那种写法依赖具体 shell，已弃用。）
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR) check-layering
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/startup_stm32f103c8tx.o: $(ASM_SOURCES) | $(BUILD_DIR)
	$(CC) -c $(CPU) -mthumb -x assembler-with-cpp $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@
	@echo
	@$(SIZE) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O ihex   $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf
	$(OBJCOPY) -O binary -S $< $@

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

size: $(BUILD_DIR)/$(TARGET).elf
	@$(SIZE) -A $<

check-layering:
	@$(PYTHON) tools/check_layering.py

# ---- PC 端单元测试 -----------------------------------------------------------
# 只编译「纯逻辑」那几个文件，并且**只给 -IUtil -IDrivers，不给 -IBSP** ——
# 万一被测代码偷偷依赖了硬件头，这里会立刻编译失败。等于用构建配置又守了一遍分层。
TEST_DIR    := test
TEST_BIN    := $(BUILD_DIR)/test_runner$(EXE)
TEST_SRC    := $(wildcard $(TEST_DIR)/*.c)
# 被测的「纯逻辑」源码，逐个列出（不用通配符，避免误把带硬件依赖的文件拉进来）
TEST_UNDER  := Util/filter.c Util/fixed_str.c \
               Drivers/dht11_frame.c Drivers/hist_index.c Drivers/esp_parse.c
TEST_CFLAGS := -std=c11 -Wall -Wextra -O1 -g -IUtil -IDrivers -I$(TEST_DIR)

# 宿主编译器可用性预检：早期报错，避免落到 make 那句看不懂的 "Error -1"
ifeq ($(shell command -v $(firstword $(HOSTCC)) 2>/dev/null),)
  _HOSTCC_MISSING := 1
endif

test: check-layering
ifeq ($(_HOSTCC_MISSING),1)
	@echo "!! 未找到宿主编译器 '$(firstword $(HOSTCC))'，无法编译 PC 端单元测试。"
	@echo "   Linux / macOS       : 装 gcc 即可（cc 是它自带的）"
	@echo "   Windows (推荐 zig)  : pip install ziglang，然后把 site-packages/ziglang 加进 PATH"
	@echo "   已有编译器           : make test HOSTCC=gcc     （或 HOSTCC=\"zig cc\"）"
	@exit 1
else
	@echo ">> 编译 PC 端单元测试（宿主编译器：$(HOSTCC)）"
	@mkdir -p $(BUILD_DIR)
	$(HOSTCC) $(TEST_CFLAGS) $(TEST_SRC) $(TEST_UNDER) -o $(TEST_BIN)
	@$(TEST_BIN)
endif

flash: all
	@echo ">> 烧录 $(BUILD_DIR)/$(TARGET).hex"
	$(PROGRAMMER) -c "port=SWD mode=UR reset=HWrst freq=1000" \
	              -w $(BUILD_DIR)/$(TARGET).hex -v
	@echo ">> 烧录后再显式复位一次（否则芯片停在 halt，串口一条数据都没有）"
	$(PROGRAMMER) -c "port=SWD mode=UR reset=HWrst freq=1000"

clean:
	@rm -rf $(BUILD_DIR)
	@echo ">> 已清理 $(BUILD_DIR)/"
