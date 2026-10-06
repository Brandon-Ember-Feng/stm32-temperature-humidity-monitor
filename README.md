# 温湿度监测仪 v1.2

![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)
![Platform](https://img.shields.io/badge/MCU-STM32F103C8T6-blue.svg)
![HAL](https://img.shields.io/badge/HAL-none%20(register%20level)-orange.svg)
[![CI](https://github.com/Brandon-Ember-Feng/stm32-temperature-humidity-monitor/actions/workflows/ci.yml/badge.svg)](https://github.com/Brandon-Ember-Feng/stm32-temperature-humidity-monitor/actions/workflows/ci.yml)

基于 **STM32F103C8T6** 的温湿度监测系统：采集 → 显示 → 存储 → 报警 → 无线推送，全链路打通。
**纯寄存器裸机开发，不依赖 HAL 库**，所有外设（GPIO / 定时器 / 串口 / SPI / 软件 I2C）均由直接读写寄存器实现。

- 主控：STM32F103C8T6（Cortex-M3），主频 64 MHz
- 固件体积：Flash **18.2 KB**、RAM **1.9 KB**（`text 18680 / data 16 / bss 1920`）
- 代码规模：4 层 25 个 `.c` 文件，按 `Core / Drivers / BSP / Util` 分层（见第 6 节）
- 构建方式：`make` 一条命令，不依赖 IDE；架构约束在编译前自动校验
- 质量保障：**45 个单元测试用例 / 137 条断言**（PC 端运行，见 6.3），CI 每次提交自动跑「架构校验 + 交叉编译零警告 + 单元测试」

![实物运行照片](docs/images/hardware-03-run-status.png)

---

## 1. 这个项目解决什么问题

普通的温湿度计只能"看一眼当前读数"，而这个系统解决的是**四个更实际的痛点**：

| 痛点 | 本项目的做法 |
|---|---|
| 数值跳动，看不准 | 5 次采样做**滑动平均**滤波，读数不再来回抖 |
| 想知道"刚才一小时是不是一直这么热" | 每分钟把一条记录写进 **W25Q64 SPI Flash（8 MB）**，**掉电不丢**，可在 OLED 上翻页回看 |
| 人不在现场就不知道超没超限 | 温度 > 30.0 °C 或湿度 > 80.0 % 时，**LED 快闪 + 蜂鸣器鸣响**本地报警 |
| 要凑到屏幕前才能看 | ESP-01S **自建 WiFi 热点 + TCP 服务器**，手机连上就能每 2 秒收到一行数据，不用路由器、不用外网 |

**它不依赖任何云平台和路由器**——板子自己发热点，手机直连即可，适合宿舍、实验室、温室、机房等没有现成网络基础设施的场景。

---

## 2. 主要功能

| # | 功能 | 实现要点 |
|---|---|---|
| 1 | **DHT11 采集** | PB0 单总线，主机拉低 >18 ms 起始；40 位数据按"高电平时长"解码（26~28 µs = 0，70 µs = 1）；**校验和验证** |
| 2 | **软件滤波** | 最近 5 次有效采样滑动平均，抑制跳动 |
| 3 | **OLED 显示** | SSD1306 128×64，软件模拟 I2C（PB6=SCL，PB7=SDA），**5 个画面**按键切换 |
| 4 | **历史记录** | W25Q64（硬件 SPI1）掉电存储，**每分钟 1 条**，3 字节/条，双扇区乒乓写入 |
| 5 | **极值统计** | 记录开机以来的温度/湿度最大值与最小值 |
| 6 | **超限报警** | 超阈值时 LED 由 1 Hz 转 4 Hz 快闪，蜂鸣器断续鸣响 |
| 7 | **无线推送** | ESP-01S 自建热点 `ESP_TEMP`，TCP 服务器端口 8080，每 2 秒推一帧给手机 |
| 8 | **串口调试** | USART1 @115200 输出完整日志（原始数据、滤波值、报警状态、AT 指令往返） |
| 9 | **上电自检** | 蜂鸣器短鸣、I2C 总线扫描、Flash JEDEC 校验，故障通过 **LED 闪码**报出（没接串口也能判断） |
| 10 | **无 RTC 计时** | 无外接时钟芯片，用 **TIM2 硬件定时器 1 ms 中断**做时基，开机计时 00:00:00 |

### 五个显示画面（短按 K1 切换，长按 1 秒回画面 0）

| 画面 | 内容 |
|---|---|
| 0 温湿度 | `Temp` / `Humi` / `Status` / 采样统计 |
| 1 时间 | 开机计时 `时:分:秒` + 运行总秒数 |
| 2 极值 | `Tmax` / `Tmin` / `Hmax` / `Hmin` |
| 3 历史 | 从 Flash 读出，**每页 3 条**，短按翻页（此画面需长按才能退出） |
| 4 WiFi | 热点状态 / SSID / IP / 客户端数 / 已推送帧数 |

### LED 状态含义（不看屏幕也能判断运行状态）

| 现象 | 含义 |
|---|---|
| 1 Hz 慢闪 | 正常，未超限 |
| 4 Hz 快闪 | **报警**：温度或湿度超阈值 |
| 8 Hz 超快闪 | DHT11 读取失败 |
| 上电闪 2 次 | I2C SDA 自检通过 |
| 上电闪 1 次 | I2C SDA 自检失败（线没接上） |
| 上电闪 3 次 | W25Q64 识别成功（`JEDEC = 0xEF4017`） |

---

## 3. 安装方法

### 3.1 硬件清单

| 器件 | 规格 | 数量 |
|---|---|---|
| 主控板 | STM32F103C8T6 最小系统板（"蓝丸"） | 1 |
| 温湿度传感器 | DHT11 | 1 |
| 显示屏 | SSD1306 OLED 128×64，I2C 接口 | 1 |
| Flash | W25Q64 模块（8 MB / 64 Mbit） | 1 |
| WiFi 模块 | ESP-01S | 1 |
| 蜂鸣器 | 3 针**有源**蜂鸣器模块（本项目为低电平触发） | 1 |
| 按键 | 轻触按键 | 1 |
| 下载器 | ST-Link V2 | 1 |
| 串口模块 | CH340 USB 转串口（调试用） | 1 |

### 3.2 接线表

> ⚠️ **接线前务必逐脚核对丝印**。3 针器件的引脚顺序没有统一标准（有 `VCC-IO-GND`、`GND-VCC-IO`、`VCC-GND-IO` 多种排法）。
> **电源反接的模块通常不会烧毁，而是"不烧不响"的静默失效**，现象看起来极像软件故障，排查成本很高。
> 建议统一颜色约定：**VCC 用红线，GND 用黑/蓝线**。

| 器件 | 引脚 | 接到 STM32 | 说明 |
|---|---|---|---|
| **DHT11** | VCC / GND | 3.3 V / GND | |
| | DATA | **PB0** | 单总线 |
| **OLED** | VCC / GND | 3.3 V / GND | |
| | SCL / SDA | **PB6 / PB7** | 软件 I2C，地址 `0x78` |
| **W25Q64** | VCC / GND | 3.3 V / GND | **不耐 5 V** |
| | CS / CLK / DO / DI | **PA4 / PA5 / PA6 / PA7** | SPI1；DO→MISO，DI→MOSI |
| **蜂鸣器** | VCC / GND | 3.3 V / GND | 见上方反接警告 |
| | IO | **PB5** | 低电平触发（`BEEP_ACTIVE_HIGH = 0`） |
| **按键 K1** | 一端 / 另一端 | **PB1** / GND | 内部上拉，无需外接电阻 |
| **LED** | 板载 | **PC13** | 直接使用板上 LED |
| **CH340** | RXD / TXD | **PA9 / PA10** | USART1 @115200 |
| **ESP-01S** | VCC / GND | 3.3 V / GND | **峰值 300 mA+**，建议就近并联 470 µF 电容 |
| | CH_PD（即 EN） | **3.3 V** | **不接模块毫无反应** |
| | TXD / RXD | **PA3 / PA2** | USART2，**收发交叉** |
| | RST、GPIO0、GPIO2 | 悬空 | GPIO0 拉低会进下载模式 |

> ESP-01S 供电建议取自 STM32 板载 3.3 V（AMS1117，约 500–800 mA），**不要**用 ST-Link 的 3.3 V（仅约 100 mA，会反复重启）。
> 注意很多面包板的**正极轨在中间是断开的**，跨在断口两侧等于没供电。

### 3.3 编译

#### 方式 A：命令行 `make`（推荐，只需一条命令）

```bash
make                      # 编译 + 链接，产物在 build/
```

工具链不在 `PATH` 里也没关系，用变量传进去即可（CubeIDE 自带的工具链就能用）：

```bash
make TOOLCHAIN="C:/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins/\
com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740/tools/bin"
```

**注意：每个 `.c` 编译之前都会自动跑一次架构校验**（见 6.2），违反分层约束就编译不过。

| 目标 | 作用 |
|---|---|
| `make` / `make all` | 编译 + 链接 |
| `make test` | **PC 端单元测试**（在电脑上编译纯逻辑模块并跑 45 个用例，见 6.3） |
| `make check-layering` | 单独跑分层架构校验 |
| `make size` | 打印各段（.text/.rodata/.data/.bss…）占用明细 |
| `make flash` | 用 `STM32_Programmer_CLI` 烧录（需装 STM32CubeProgrammer） |
| `make clean` | 删除 `build/` |

> `make test` 需要电脑上有一个宿主 C 编译器。Linux / macOS 自带 `cc`（就是 gcc），直接可用；
> **Windows 上通常没有 `cc`，用 `zig cc` 代替**（`pip install ziglang`，然后把
> `site-packages/ziglang` 加进 `PATH`，它里面有 `zig.exe`）。若已装了别的编译器，
> 也可以 `make test HOSTCC=gcc` 直接指定。
> **注意这条命令跟 ARM 工具链无关** —— 它在你的电脑上跑，不需要开发板。

产物：

| 文件 | 用途 |
|---|---|
| `build/temp-monitor.elf` | 带调试信息，SWD 下载 / 调试用 |
| `build/temp-monitor.hex` | ST-Link 烧录用 |
| `build/temp-monitor.bin` | 裸二进制 |
| `build/temp-monitor.map` | 段与符号分布，排查体积用 |

#### 方式 B：STM32CubeIDE

1. `File → Import → Existing Projects into Workspace`
2. 选择本仓库目录，导入后直接 `Build`
3. 点击 `Run / Debug` 即可通过 ST-Link 烧录

> 仓库里的 `.cproject` 已经按四层配好源码目录与头文件路径
> （`BSP / Util / Drivers / Core`），导入后不需要手工补 include。

#### 方式 C：手工调用 arm-none-eabi-gcc（理解编译链接过程用）

需先安装 [GNU Arm Embedded Toolchain](https://developer.arm.com/downloads/-/gnu-rm)：

```bash
# 编译单个 .c（Cortex-M3、无 HAL、-Wall -Wextra 零警告）
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -Wall -Wextra -O1 -g \
  -ffunction-sections -fdata-sections -DSTM32F103xB \
  -IBSP -IUtil -IDrivers -ICore \
  -c Core/main.c -o main.o          # 其余 .c 同样处理

arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb \
  -c Startup/startup_stm32f103c8tx.s -o startup.o

# 链接
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb \
  -T STM32F103C8TX_FLASH.ld \
  -Wl,-Map=fw.map -Wl,--gc-sections \
  -specs=nano.specs -specs=nosys.specs \
  *.o -o fw.elf

# 生成烧录用 hex
arm-none-eabi-objcopy -O ihex fw.elf fw.hex
```

> 若使用 STM32CubeIDE 自带工具链，gcc 位于
> `<CubeIDE安装目录>/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin/`

### 3.4 烧录

```bash
# 方式一：make（内部就是下面两条命令）
make flash
# 若 STM32_Programmer_CLI 不在 PATH 上，直接给完整路径：
make flash PROGRAMMER="C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"

# 方式二：直接用 STM32CubeProgrammer CLI
STM32_Programmer_CLI.exe -c "port=SWD mode=UR reset=HWrst freq=1000" \
                         -w build/temp-monitor.hex -v

# ⚠️ 烧完必须再显式复位一次，否则芯片停在 halt 状态，串口一条数据都不会有
STM32_Programmer_CLI.exe -c "port=SWD mode=UR reset=HWrst freq=1000"
```

两个实测踩过的点：

- **必须用 `mode=UR`（under reset）**，不能用默认的 `mode=hotplug` —— hotplug 模式下无法擦除 Flash，会报 `failed to erase memory`。
- **烧录后要单独发一次复位命令**。实测：烧完直接开串口监听 = 收到 0 字节；补一条复位命令后 = 正常收到 400+ 字节的自检日志。

烧录器只接 **SWDIO / SWCLK / GND** 三根线即可（不要同时接 3.3 V，避免两个电源并联）。

---

## 4. 使用方法

### 4.1 上电

1. 按上表接好线，ST-Link 与 CH340 接好
2. 上电后蜂鸣器**短鸣 200 ms** 自检；板载 LED 长亮 1.5 s 后进入闪码自检
3. OLED 显示 `DHT11 Monitor v1.2 Ready`，随后进入画面 0

### 4.2 按键操作

| 操作 | 效果 |
|---|---|
| **短按 K1** | 切换画面（0→1→2→3→4→0）；在画面 3（历史）内则是**翻页** |
| **长按 K1（≥1 秒）** | 直接回到画面 0（从历史画面"脱身"的唯一方式） |

按键时 LED 会闪一下（短按闪 1 次、长按闪 2 次）作为反馈。

### 4.3 手机接收无线数据

1. 手机 WiFi 连接热点 **`ESP_TEMP`**，密码 **`12345678`**
   - 提示"无法访问互联网"时选择**仍然连接**（热点本就没有外网，正常）
2. 打开任意支持 **TCP Client** 的网络调试 APP
3. 目标地址 **`192.168.4.1`**，端口 **`8080`**，点连接
4. 每 2 秒收到一行数据：

```
T=31.6C H=45.0% #106 A=1
```

| 字段 | 含义 |
|---|---|
| `T=31.6C` | 滤波后温度（°C） |
| `H=45.0%` | 滤波后湿度（%） |
| `#106` | Flash 中已存的历史记录条数 |
| `A=1` | 报警标志：1=超限，0=正常 |

> ⚠️ 手机 APP 需要的是 **TCP Client（客户端）** 模式；若只有 Server（服务端/"监听中"）模式，两者都在等待对方，永远连不上。

### 4.4 演示流程

把手捂在 DHT11 上几秒，可同时观察到三个现象：

- OLED 温度上升、`Status` 变为 `ALARM`
- LED 由慢闪转**快闪**，蜂鸣器开始断续鸣响
- 手机收到的数据中 `A=0` 变为 `A=1`
- 按 K1 切到画面 2，可看到 `Tmax` / `Hmax` 被刷新（且不会回落）

---

## 5. 输入输出示例

### 5.1 输入

| 输入 | 来源 | 频率 |
|---|---|---|
| 温度 / 湿度 | DHT11（PB0 单总线） | 每 2 秒 |
| 按键 | K1（PB1） | 随时 |
| 客户端 TCP 连接 | 手机经 WiFi | 随时 |

### 5.2 输出一：串口日志（USART1，115200 8N1）

以下为**真机上电后实际抓取**的输出：

```
=== 温湿度监测仪 v1.2（DHT11 + OLED + W25Q64 + 蜂鸣器 + ESP8266 WiFi）===
CPU 主频: 64 MHz
[自检] 蜂鸣器短鸣 200ms（配置为低电平触发）：若此后一直长鸣不停，说明模块是另一种触发极性
[SDA自检] 通过（线上有上拉，接线正常）
[I2C总线] 空闲电平 SCL(PB6)=1 SDA(PB7)=1  (正常两个都应为 1)
[I2C总线] 扫描到 1 个器件: 0x78
[W25Q64] JEDEC ID = 0xEF4017
[W25Q64] 识别成功（Winbond，容量 4MB 量级）
[记录] 已有 106 条历史记录，本次从扇区 A 继续写入
[I2C复核] 进入 OLED_Init 前再扫一次: 1 个器件: 0x78
[OLED] 初始化成功，地址 0x78
[ESP] 开始配置：AP 模式 + 热点 + TCP 服务器
[ESP>] ATE0
[ESP<]   OK
[ESP>] AT+CWMODE=2
[ESP<]   OK
[ESP>] AT+CWSAP="ESP_TEMP","12345678",5,3
[ESP<]   OK
[ESP>] AT+CIPMUX=1
[ESP<]   OK
[ESP>] AT+CIPSERVER=1,8080
[ESP<] no change    OK
[ESP>] AT+CIFSR
[ESP<] +CIFSR:APIP,"192.168.4.1"  +CIFSR:APMAC,"xx:xx:xx:xx:xx:xx"    OK
[ESP] 就绪：热点 SSID=ESP_TEMP  密码=12345678
[ESP] 初始化成功：热点 ESP_TEMP 已建立
--- 开始采集：每 2 秒一次，每 1 分钟存一条历史 ---
--- 按 K1(PB1) 切换画面（共 5 个），历史画面内再按则翻页 ---
[DHT11] raw: 2D 00 1F 06 52 | Temp 31.6C  Humi 45.0% | 滤波后 31.6C 45.0% | [报警]
[DHT11] raw: 2D 00 1F 07 53 | Temp 31.7C  Humi 45.0% | 滤波后 31.6C 45.0% | [报警]
```

`raw: 2D 00 1F 06 52` 是 DHT11 的 5 字节原始帧：湿度整数 `0x2D`=45、温度整数 `0x1F`=31、温度小数 `0x06`=0.6；
校验和 `0x2D + 0x00 + 0x1F + 0x06 = 0x52` ✓ 与第 5 字节一致。

### 5.3 输出二：手机 TCP 收到的数据

```
T=31.6C H=45.0% #106 A=1
T=31.6C H=45.0% #106 A=1
T=31.7C H=45.0% #106 A=1
```

模块侧对应的 AT 握手（实测 12 次，`>` 提示符耗时稳定在 208~219 ms）：

```
AT+CIPSEND=0,25        ->  >
T=31.6C H=45.0% #106 A=1
                       ->  Recv 25 bytes / SEND OK
```

### 5.4 输出三：OLED 画面（128×64，每屏 4 行）

```
┌────────────────┐  ┌────────────────┐  ┌────────────────┐
│ Temp: 31.6°C   │  │ Time: 00:12:34 │  │ Tmax: 31.7°C   │
│ Humi: 45.0%    │  │ Uptime: 754s   │  │ Tmin: 29.1°C   │
│ Status: ALARM  │  │ Status: Normal │  │ Hmax: 52.0%    │
│ OK:123 F:0 M1/5│  │ OK:123 F:0 M2/5│  │ Hmin: 44.0%    │
└────────────────┘  └────────────────┘  └────────────────┘
   画面 0 温湿度        画面 1 时间           画面 2 极值

┌────────────────┐  ┌────────────────┐
│ H1/36 N=106    │  │ WiFi: AP OK    │
│ 104 31.5 45.0  │  │ SSID:ESP_TEMP  │
│ 105 31.6 45.0  │  │ IP: 192.168.4.1│
│ 106 31.6 45.0  │  │ Client:1 TX:106│
└────────────────┘  └────────────────┘
   画面 3 历史          画面 4 WiFi
```

画面 3 每行格式为 `序号 温度 湿度`；因每分钟存一条，**序号差即分钟差**（往前数 5 行 ≈ 5 分钟前）。
画面 4 的 `Client:1` 表示有手机已连接，`TX:` 是已成功推送的帧数。

### 5.5 输出四：蜂鸣器与 LED

| 条件 | 蜂鸣器 | LED |
|---|---|---|
| 上电自检 | 短鸣 200 ms | 长亮 1.5 s |
| 正常未超限 | 静默 | 1 Hz 慢闪 |
| 超限（>30.0 °C 或 >80.0 %） | 断续鸣响（响 200 ms / 停 800 ms） | 4 Hz 快闪 |
| DHT11 读取失败 | 静默 | 8 Hz 超快闪 |

### 5.6 实物照片

以下三张均为**真机实拍**（非示意图），OLED 上的文字可直接与 5.4 的画面定义对照：

| 上电自检 | 实时读数 | 运行状态 |
|---|---|---|
| ![上电自检](docs/images/hardware-01-power-on-selfcheck.png) | ![实时读数](docs/images/hardware-02-live-reading.png) | ![运行状态](docs/images/hardware-03-run-status.png) |
| `DHT11 Monitor` `v1.2 Ready` `Flash: OK` `Beep:Low-Trg` | `Temp: 27.8°C` `Humi: 58.8%` `Status: Normal` `OK:45 F:0 M2/5` | `Time: 00:02:39` `Uptime: 159s` `Status: Normal` `OK:64 F:0 M2/5` |

三张照片对应三种状态：

- **上电自检**：OLED 报出 `Flash: OK`（W25Q64 的 JEDEC 校验通过）与 `Beep:Low-Trg`（蜂鸣器按低电平触发配置）
- **实时读数**：滑动平均后的温湿度 + `Status: Normal`；末行 `OK:45 F:0 M2/5` 含义为「成功采样 45 次 / 失败 0 次 / 当前显示画面 2，共 5 个画面」
- **运行状态**：`Time` 为开机计时，`Uptime` 为最近一次成功采集距今的秒数

整机全部搭在面包板上：STM32F103C8T6 最小系统板、SSD1306 OLED、DHT11、W25Q64、3 针蜂鸣器、ESP-01S 与 CH340 共地供电，**未使用任何外部路由器**——热点由 ESP-01S 自行发出。

---

## 6. 项目结构

代码按 **`Core → Drivers → BSP → Util`** 四层组织，25 个 `.c` 文件。分层不是为了"好看"，
而是为了让**上层代码不依赖具体硬件** —— 这是后面能做单元测试、能换芯片的前提（见 6.2、6.3）。

```
温湿度监测仪/
├── README.md                     本文件
├── LICENSE                       MIT 许可证
├── Makefile                      命令行构建（all / test / size / flash / check-layering / clean）
├── STM32F103C8TX_FLASH.ld        链接脚本（Flash/RAM 布局）
├── .cproject / .project          STM32CubeIDE 工程（已指向四层目录，导入即可编译）
│
├── Core/                    ★ 第 4 层  应用逻辑，一行寄存器都不碰
│   ├── main.c                    上电整套自检流程 + 主循环调度
│   ├── app.c / app.h             数据模型：解析原始字节、滑动平均、极值、报警判定
│   ├── ui_pages.c / ui_pages.h   五个 OLED 画面 + 按键切换
│   └── syscalls.c / sysmem.c     newlib 桩函数
├── Drivers/                 ★ 第 3 层  器件驱动，只依赖 BSP 提供的物理层
│   ├── dht11.c / .h              DHT11 单总线协议、位解码（时序在 BSP/bsp_onewire.c）
│   ├── dht11_frame.c / .h        ① 纯逻辑：帧校验和、温湿度换算（可在 PC 上单测）
│   ├── ssd1306.c / .h            OLED 控制器指令与显存写入
│   ├── font8x16.c / .h           8×16 点阵字库
│   ├── w25q64.c / .h             SPI Flash 指令集
│   ├── hist_store.c / .h         历史记录：双扇区乒乓 + 顺序追加
│   ├── hist_index.c / .h         ② 纯逻辑：逻辑序号 → Flash 地址换算（可在 PC 上单测）
│   ├── esp8266_at.c / .h         AT 指令序列、断线重连
│   ├── esp_parse.c / .h          ③ 纯逻辑：AT 应答关键词匹配（可在 PC 上单测）
│   ├── key.c / .h                按键扫描：消抖 + 长短按
│   └── beep.c / .h               蜂鸣器
├── BSP/                     ★ 第 2 层  板级支持包，全工程唯一允许读写寄存器的地方
│   ├── bsp_reg.h                 全部寄存器地址与位定义（只有这一处）
│   ├── bsp_rcc.c / .h            时钟树：HSI /2 → PLL ×16 → 64 MHz
│   ├── bsp_time.c / .h           SysTick 微秒延时 + TIM2 1 ms 系统时基
│   ├── bsp_gpio.c / .h           GPIO 配置、LED 与按键引脚
│   ├── bsp_onewire.c / .h        单总线电平时序（DHT11 的物理层）
│   ├── bsp_soft_i2c.c / .h       软件 I2C（OLED 的物理层）
│   ├── bsp_spi.c / .h            SPI1（W25Q64 的物理层）
│   ├── bsp_uart.c / .h           USART1 调试口 + USART2 中断收发
│   └── bsp_cpu.h                 内核级原语：关 / 开全局中断
├── Util/                    ★ 第 1 层  纯算法，不包含任何硬件头文件
│   ├── fixed_str.c / .h          定点格式化（不用 sprintf，省 Flash）
│   └── filter.c / .h             滑动平均滤波器
│
├── test/                          PC 端单元测试（在电脑上跑，不需要开发板）
│   ├── framework.h                ~100 行自写断言框架（不引第三方依赖）
│   ├── host_test_main.c           框架实现 + main()，汇总 5 个 suite 的结果
│   ├── suites.h                   5 个 suite 的入口声明
│   ├── test_filter.c              滑动平均：8 个用例
│   ├── test_fixed_str.c           定点格式化：9 个用例
│   ├── test_dht11_frame.c         DHT11 帧解析：9 个用例（含负温、校验和边界）
│   ├── test_hist_index.c          历史地址换算：8 个用例（含双扇区满、越界）
│   ├── test_esp_parse.c           AT 应答匹配：11 个用例（含不越界读）
│   └── test_plan.md               ★ 测试计划：范围 / 45 项用例 / 未覆盖部分 / 有效性验证
│
├── Startup/
│   └── startup_stm32f103c8tx.s   启动文件（向量表 + 复位入口）
├── tools/
│   └── check_layering.py         分层架构自动校验（三条硬性约束）
├── .github/workflows/
│   └── ci.yml                    CI：架构校验 + 交叉编译零警告 + 单元测试
└── docs/
    └── images/                   实物照片（见 5.6 节）
```

> 带 ①②③ 标注的三个文件是**为了可测试性从既有代码里剥离出来的纯逻辑单元** ——
> 内容不是新写的，只是把原本混在硬件流程里的位运算/算术搬到了独立文件，
> 这样它们就能脱离硬件在 PC 上编译、被断言覆盖。详见 6.3。

### 6.1 模块与原章节的对应关系

最初的版本是**一个 3373 行的 `main.c`**，内部已经按 16 个章节分成段落 —— 也就是说
**模块边界在逻辑上早就存在**。这次重构做的是"把边界显式化"，不是重新设计，
所以每个模块的行为都能和原来逐段对上：

| 原 `main.c` 章节 | 内容 | 现在的位置 |
|---|---|---|
| 1–3 | 寄存器地址定义、系统时钟、SysTick 微秒延时 | `BSP/bsp_reg.h`、`bsp_rcc.c`、`bsp_time.c` |
| 4 | 软件 I2C 时序 | `BSP/bsp_soft_i2c.c` |
| 5 | SSD1306 驱动 + 8×16 字库 | `Drivers/ssd1306.c`、`font8x16.c` |
| 6 | TIM2 1 ms 时基 | `BSP/bsp_time.c` |
| 7 | 按键扫描（消抖 + 长短按） | `Drivers/key.c` |
| 8 | DHT11 单总线协议 | `Drivers/dht11.c`（线级时序在 `BSP/bsp_onewire.c`） |
| 9 | SPI1 | `BSP/bsp_spi.c` |
| 10 | W25Q64 驱动 | `Drivers/w25q64.c` |
| 11 | 历史记录（双扇区乒乓） | `Drivers/hist_store.c` |
| 12 | 蜂鸣器 | `Drivers/beep.c` |
| 13 | 五个显示画面 | `Core/ui_pages.c` |
| 14 | USART1 调试串口 | `BSP/bsp_uart.c` |
| 15 | ESP-01S 驱动（AT 指令） | `Drivers/esp8266_at.c`（收发在 `BSP/bsp_uart.c`） |
| 16 | 主函数与状态机 | `Core/main.c`、`Core/app.c` |
| — | 定点格式化、滑动平均（原散在各处） | `Util/fixed_str.c`、`Util/filter.c` |
| — | 帧解析 / 地址换算 / 应答匹配（原混在硬件流程里，为可测试性剥离） | `Drivers/dht11_frame.c`、`hist_index.c`、`esp_parse.c` |

> 重构的等价性做过工具校验：把访问函数按语义反向还原后逐函数比对，
> **106 个函数与重构前逐字一致**，仅 5 个函数因"接口收口"被有意改写，新增 17 个访问接口。

### 6.2 三条硬性约束（构建时自动强制）

| # | 约束 | 为什么 |
|---|---|---|
| **1** | **依赖单向**：`Core → Drivers → BSP → Util`，禁止反向 | 反向依赖会让改动"牵一发动全身"，也让模块无法单独测试 |
| **2** | **寄存器隔离**：所有寄存器读写只允许出现在 `BSP/` | 上层不含任何硬件细节，把逻辑搬到 PC 上编译、跑单元测试才有可能 |
| **3** | **接口收敛**：`BSP/Util/Drivers` 的内部状态一律 `static`，跨模块只走 `.h` 里的访问函数 | "谁能改这个状态"永远只有一个答案，出问题时排查面从全工程缩到一个文件 |

这三条不是写在文档里靠自觉，而是由 `tools/check_layering.py` **在每次编译前自动检查**：

```bash
$ make check-layering
======================================================================
分层架构校验   共检查 49 个文件
======================================================================
  规则1 依赖单向   Core -> Drivers -> BSP -> Util   ✔
  规则2 寄存器隔离 寄存器访问仅存在于 BSP/ 层        ✔
  规则3 接口收敛   BSP/Util/Drivers 内部状态全 static ✔

结论：架构约束全部满足 ✔
```

一旦有人（包括我自己）在 `Drivers/` 里直接写了 `GPIOA_CRL`，或者图省事加了个
非 `static` 的全局变量，**编译会直接失败**并指出文件名和行号。

> 由约束 3 带来的一处实际改进：原本 `Core/main.c` 会直接给驱动内部的
> `g_spi_ok` 赋值、给 `g_dht_ok_cnt` 做自增。现在这些动作全在驱动内部，
> 上层只能通过 `W25_IsOk()` / `DHT_OkCount()` 读结论 —— 驱动内部怎么记账，
> 上层既不需要知道，也没有能力改坏。

### 6.3 PC 端单元测试（`make test`）

嵌入式项目常见的困境是"每一行代码都要烧到板子上才知道对不对"。这个工程把
**不依赖硬件的逻辑抽出来，在电脑上直接编译运行**，一条命令就能验证：

```bash
$ make test
>> 编译 PC 端单元测试（宿主编译器：zig cc）
...
  ok    test_filter_partial_window_uses_actual_count
  ok    test_frame_negative_one_degree
  ok    test_locate_both_sectors_full
  ok    test_never_reads_past_length
  ...
==================================================
cases : 45 run, 0 failed
checks: 137 run, 0 failed
RESULT: PASS
==================================================
```

45 个用例、137 条断言，全部在 PC 上运行，**不需要开发板、不需要 ARM 工具链**。

#### 测什么 / 为什么不测别的

| 被测模块 | 覆盖内容 | 用例 |
|---|---|---|
| `Util/filter.c` | 滑动平均：窗口未满、除零边界、负值取整方向 | 8 |
| `Util/fixed_str.c` | 定点格式化：零值、负值、宽度/对齐、缓冲区截断 | 9 |
| `Drivers/dht11_frame.c` | 帧校验和、正负温换算、湿度换算、全零帧 | 9 |
| `Drivers/hist_index.c` | 逻辑序号 → Flash 地址：双扇区满、越界、掉电后序号不等 | 8 |
| `Drivers/esp_parse.c` | AT 应答关键词匹配：跨批次、部分匹配、不越界读 | 11 |

选这 5 个模块的标准只有一条：**"它能不能在 PC 上跑"**。剩下的（总线时序、中断、OLED 排版）
必须依赖真实硬件或精确时序，塞进单测只会得到一堆"因为环境不对而失败"的假信号 ——
它们的验证方式在 [`test/test_plan.md`](test/test_plan.md) 的 §4 里逐条列出了。

#### 三处纯逻辑剥离（可测试性的来源）

`Util/` 那两个文件本来就是纯算法；另外三个原本**混在硬件流程里**，
这次把其中的位运算/算术搬到了独立文件（**逻辑一行没改，只是换了位置**）：

| 原位置 | 剥离出的内容 | 现在的位置 |
|---|---|---|
| `dht11.c` 校验和判定 + `Core/app.c` 温湿度换算 | 40 位帧 → 温湿度整数（含负温、×10 定点） | `Drivers/dht11_frame.c` |
| `hist_store.c` 的 `W25_RecRead` 排序寻址 | 逻辑序号 → Flash 绝对地址（双扇区乒乓的地址推算） | `Drivers/hist_index.c` |
| `esp8266_at.c` 的 `static ESP_MatchAt` / `ESP_Has` | AT 应答关键词匹配（改成显式 `(buf, len)`，不依赖 `'\0'`） | `Drivers/esp_parse.c` |

> 边界处理是这次剥离的重点：原 `ESP_Has` 依赖缓冲区末尾的 `'\0'`，
> 但 USART2 是定长缓冲、可能刚好被填满而没有结束符。新接口把长度显式传进去，
> 并专门写了一条**用不含 `'\0'` 的定长数组**去撞它的用例。

#### 用构建配置再守一遍分层

```make
# Makefile
TEST_CFLAGS := -std=c11 -Wall -Wextra -O1 -g -IUtil -IDrivers -I$(TEST_DIR)
```

注意**只给 `-IUtil -IDrivers`，不给 `-IBSP`**。如果被测代码哪天偷偷 `#include "bsp_gpio.h"`，
这里会**立刻编译失败** —— 等于用构建配置第 3 次守住了分层边界，不需要额外检查规则。

#### 测试本身可靠吗（有效性验证）

"测试全绿"有可能是因为断言写得恒真。为此做了一次**反向验证**：
故意给 `Filter_Avg` 注入一个 bug（无论窗口是否填满都固定除以 `FILTER_N`），
重新运行 —— **4 个用例立刻失败、退出码 1**；改回正确实现后恢复全绿。
这说明断言确实能区分对错实现，而不是在骗自己。

#### CI 自动跑

`.github/workflows/ci.yml` 在每次 push / PR 时自动执行三步，任一失败即红：

| 步骤 | 门槛 |
|---|---|
| `python3 tools/check_layering.py` | 三条分层约束全部通过 |
| `make TOOLCHAIN=/usr/bin` | 交叉编译成功，**且 `-Wall -Wextra` 零警告**（`grep "warning:"` 命中即失败） |
| `make test` | 45 个用例全部通过，**且测试代码零编译警告** |

之所以把"零警告"也做成硬门槛，是因为嵌入式里的警告往往就是真 bug
（未初始化变量、隐式类型截断、有符号/无符号比较）。

---

## 7. 几个值得说明的技术决策

> 列出「代价」是有意的 —— 任何设计都是取舍，说不出代价的方案通常是因为还没想清楚。

| 决策 | 理由 | 代价 |
|---|---|---|
| **不用 HAL，直接写寄存器** | 每一行都对应参考手册的一个 bit，能看清外设真实工作方式 | 换芯片要重写 BSP 层；没有 HAL 提供的容错 |
| **代码分四层，寄存器只在 BSP** | 上层不含硬件细节，逻辑能在 PC 上编译并做单元测试；换芯片只改 BSP | 多了一层函数跳转（实测约 +1 KB Flash），写起来比"一个文件写完"啰嗦 |
| **把可测逻辑剥离成独立文件，再上 PC 单元测试** | 硬件的正确性只能靠实机验证，但**算法的正确性可以靠断言**。两者分开后，改一行换算逻辑不必烧板子就能知道对错 | 新增 3 个文件与一次数据拷贝（实测 +236 B Flash）；"什么该剥离"没有机械标准，依赖判断 |
| **用 HSI 内部时钟而非外部晶振** | 不依赖外部晶振是否起振，程序更"皮实" | 精度约 ±1%，不适合长时间计时（本项目只做相对计时，够用） |
| **时基用 TIM2 硬件中断，不用延时累加** | 软件延时的每次调用都有指令开销，累加一万次误差明显 | 占用一个定时器；中断里必须保持极短，否则影响单总线时序 |
| **Flash 用双扇区乒乓写入** | Flash 只能把 1 写成 0，改写前必须整扇区擦除。两扇区轮流用，寿命消耗极慢 | 有效容量减半，逻辑复杂度上升（要处理掉电、对账、跨扇区翻转） |
| **USART2 接收用中断而非轮询** | 115200 下字节间隔仅 87 µs，主循环 1 ms 才轮询一次，**慢十几倍**会溢出丢字节 | 多了一个中断服务函数；缓冲与游标的并发访问要小心（清缓冲时必须关中断） |
| **ESP 自建热点而非连路由器** | 不依赖外部网络和现场 WiFi，演示时随手就能连 | 热点无外网，手机/电脑会提示"无法访问互联网" |
| **不用 `sprintf` / 浮点，手写定点格式化** | Flash 只有 64 KB，`sprintf` 会拖进整个格式化引擎；浮点还要软件模拟 | 只能按固定格式输出，格式改了要改代码（体现为 `Util/fixed_str.c`） |

---

## 8. 可配置参数

分散在对应模块的头文件里，改完 `make` 重新编译即可：

| 宏 | 位置 | 默认值 | 含义 |
|---|---|---|---|
| `TEMP_ALARM_X10` | `Core/app.h` | `300` | 温度报警阈值 30.0 °C（×10 存储） |
| `HUMI_ALARM_X10` | `Core/app.h` | `800` | 湿度报警阈值 80.0 % |
| `FILTER_N` | `Util/filter.h` | `5` | 滑动平均的采样个数 |
| `BEEP_ACTIVE_HIGH` | `Drivers/beep.h` | `0` | 蜂鸣器触发极性：`0`=低电平触发，`1`=高电平触发 |
| `HIST_PER_PAGE` | `Core/ui_pages.h` | `3` | 历史画面每页显示条数 |
| `ESP_AP_SSID` / `ESP_AP_PWD` | `Drivers/esp8266_at.h` | `ESP_TEMP` / `12345678` | 热点名称与密码 |
| `ESP_TCP_PORT` | `Drivers/esp8266_at.h` | `8080` | TCP 服务器端口 |
| `ESP_LOG_FRAMES` | `Drivers/esp8266_at.h` | `0` | 改为 `1` 可在串口打印每帧 AT 往返（调试用，会很吵） |
| `I2C_SWAP_DIAG` | `BSP/bsp_gpio.h` | `0` | 改为 `1` 开启 SCL/SDA 接反判别（排障用） |

---

## 9. 常见故障速查

| 现象 | 最常见原因 |
|---|---|
| OLED 不亮 | SCL/SDA 接反；模块 VCC 未真正量到 3.3 V；杜邦线断线 |
| OLED 花屏/错位 | 行号换算错（每行字符占 **2 个 page**，只能用 0/2/4/6） |
| 串口全是乱码 | 波特率不是 115200；或 `BRR` 计算多乘了 16（正确公式：`BRR = fCK / baud`） |
| Flash 读出 `0x000000` / `0xFFFFFF` | DO/DI 接反；或 CS 未接 PA4 |
| ESP 完全无响应 | `CH_PD`(EN) 未接 3.3 V；TXD/RXD 未交叉；供电不足 |
| 手机连上热点但收不到数据 | APP 用了 **TCP Server** 模式，应改为 **TCP Client** |
| 蜂鸣器从来不响 | **VCC/GND 接反**（反接通常不烧，只是不响）；或 IO 未接到 PB5 |
| 蜂鸣器一直长鸣 | 触发极性配反，把 `BEEP_ACTIVE_HIGH` 改为 `1` |
| 按键卡在历史画面出不来 | 该画面短按是翻页，需**长按 1 秒**返回 |
| `make` 报"架构违规" | 在 `Drivers/` 或 `Core/` 里直接写了寄存器，或加了非 `static` 全局变量；按脚本输出的 `文件:行号` 改 |
| `make test` 报"未找到宿主编译器" | Windows 上没装 gcc。装 `ziglang` 后把 `site-packages/ziglang` 加进 `PATH`，或 `make test HOSTCC=你的编译器` |
| `make test` 编译报找不到 `bsp_xxx.h` | 被测代码偷偷依赖了硬件头。这正是 `TEST_CFLAGS` 不给 `-IBSP` 要拦的情况，应把该逻辑剥离成纯函数 |
| 编译报 `undefined reference to g_xxx` | 说明有模块直接引用了别的模块的内部变量 —— 应改为调用该模块 `.h` 里的访问函数 |

---

## 10. 已验证环境

| 项目 | 实测值 |
|---|---|
| 主控 | STM32F103C8T6，64 MHz |
| 编译器 | arm-none-eabi-gcc 14.3（`-Wall -Wextra` 零警告） |
| 固件体积 | `text 18680 / data 16 / bss 1920`（Flash 18.2 KB / RAM 1.9 KB） |
| 分层等价性校验 | 106 个函数与重构前逐字一致；5 处有意改写；新增 17 个访问接口 |
| 架构约束校验 | `make check-layering`：49 个文件，3 条规则全部通过 |
| PC 端单元测试 | `make test`：45 个用例 / 137 条断言，全部通过、零编译警告 |
| 宿主编译器 | Windows 用 `zig cc`（zig 0.13.0）；Linux / CI 用 gcc（`cc`） |
| ESP-01S 固件 | AT 1.1.0.0（May 11 2016）/ SDK 1.5.4，波特率 115200 |
| OLED | SSD1306，I2C 地址 `0x78` |
| Flash | W25Q64，`JEDEC = 0xEF4017` |
| 供电压测 | 200 次连续 AT 全部成功，零重启 |
| 推送时延 | `>` 提示符 208~219 ms（12 次采样，抖动 11 ms） |

---

## 11. 许可证

本项目采用 **MIT 许可证**，详见 [`LICENSE`](LICENSE)。

- 版权人：`Copyright (c) 2026 Brandon Feng`（如需改成别的署名，直接编辑 `LICENSE` 第一行）
- 允许商用、修改、再发布，**唯一要求**是保留版权声明与许可声明
- 代码按"原样"提供，不含任何明示或默示担保
