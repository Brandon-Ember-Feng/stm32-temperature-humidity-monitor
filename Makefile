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

TARGET     := temp-monitor
BUILD_DIR  := build

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

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

# 每个 .c 编译前先过一遍架构校验，违规直接中断构建
$(BUILD_DIR)/%.o: %.c tools/check_layering.py | $(BUILD_DIR)
	@$(PYTHON) tools/check_layering.py > /dev/null || \
	  (echo ">> 分层架构校验未通过，请先运行 make check-layering 查看详情"; exit 1)
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

# 单元测试在事项 2 落地；此处先保证架构校验通过
test: check-layering
	@echo "[test] 单元测试尚未接入（事项 2）。当前仅执行架构校验。"

flash: all
	@echo ">> 烧录 $(BUILD_DIR)/$(TARGET).hex"
	$(PROGRAMMER) -c "port=SWD mode=UR reset=HWrst freq=1000" \
	              -w $(BUILD_DIR)/$(TARGET).hex -v
	@echo ">> 烧录后再显式复位一次（否则芯片停在 halt，串口一条数据都没有）"
	$(PROGRAMMER) -c "port=SWD mode=UR reset=HWrst freq=1000"

clean:
	@rm -rf $(BUILD_DIR)
	@echo ">> 已清理 $(BUILD_DIR)/"
