/**
 ******************************************************************************
 * @file    main.c
 * @brief   温湿度监测仪 v1.2：DHT11 + OLED + W25Q64 历史记录 + 蜂鸣器 + 串口 + 报警
 * @note    纯寄存器裸机开发，不依赖 HAL 库
 *
 * @author  Brandon Feng
 * @version v1.2
 * @date    2026-09-21
 *
 * @license MIT License
 *          Copyright (c) 2026 Brandon Feng
 *          完整许可条款见仓库根目录的 LICENSE 文件
 *
 *  【硬件接线】
 *      OLED   VCC -> 3.3V     OLED  GND -> GND
 *      OLED   SCL -> PB6      OLED  SDA -> PB7
 *      DHT11  VCC -> 3.3V     DHT11 GND -> GND     DHT11 DATA -> PB0
 *      LED    -> PC13（板载）
 *      KEY    -> PB1   按键另一端接 GND（上拉输入，按下读到低电平，无需外部电阻）
 *      CH340 模块 RXD -> PA9(STM32 的 TX)   模块 TXD -> PA10   GND <-> GND
 *
 *      W25Q64（SPI Flash，8MB = 64Mbit，SPI 模式 0）
 *          VCC -> 3.3V        GND -> GND
 *          CS  -> PA4（软件片选，低电平选中）
 *          CLK -> PA5
 *          DO  -> PA6   （Flash 的输出，接 STM32 的 MISO）
 *          DI  -> PA7   （Flash 的输入，接 STM32 的 MOSI）
 *          ※ W25Q64 是 3.3V 器件，绝不能接 5V；模块版一般自带电平转换
 *
 *      蜂鸣器（3 针有源模块：VCC / IO / GND，模块丝印标「低电触发」）
 *          VCC -> 3.3V        GND -> GND        IO -> PB5
 *          ※ 模块自带三极管驱动级，IO 只认逻辑电平，不消耗 IO 电流
 *          ※ 本模块是**低电平触发**（IO=0 响、IO=1 静音），固件已按此配置
 *            （BEEP_ACTIVE_HIGH = 0）；以后换高电平触发的模块，改这个宏即可
 *          ※ 有源蜂鸣器给电就响，不需要 PWM 方波；无源蜂鸣器必须用方波驱动
 *
 *      ESP-01S（WiFi 模块，2x4 共 8 针；丝印 CH_PD 就是 EN）
 *          VCC   -> 3.3V（面包板正极轨，就近并 470uF 电解电容）  GND -> GND
 *          CH_PD -> 3.3V    必须拉高，不接模块毫无反应
 *          RST   -> 悬空（内部有上拉）
 *          TXD   -> PA3     USART2_RX，**交叉**
 *          RXD   -> PA2     USART2_TX，**交叉**
 *          GPIO0 / GPIO2 -> 悬空（GPIO0 拉低会进下载模式）
 *          ※ 绝不能接 5V，它不是 5V 容忍器件；峰值电流 300mA+，供电要够
 *          ※ 自建热点（AP 模式）：手机连热点 ESP_TEMP（密码 12345678），
 *            再用 TCP Client 连 192.168.4.1:8080，就能看到数据每 2 秒一行
 *
 *  【功能总览】
 *      1. 每 2 秒读一次 DHT11（单总线，DATA 接 PB0），读 40 位数据并验校验和
 *      2. 最近 5 次有效采样做滑动平均（软件滤波），抑制数值来回跳动
 *      3. 记录温度/湿度的历史极值（最大值、最小值）
 *      4. 按键切换 OLED 画面：温湿度 / 时间 / 极值 / 历史记录，四个画面循环
 *      5. 串口 115200 输出：原始 5 字节 + 当前值 + 滤波后的值
 *      6. 温度 > 30.0°C 或 湿度 > 80.0% 时，LED 快闪 + 蜂鸣器响
 *      7. **每 1 分钟把一条温湿度记录写进 W25Q64**，掉电不丢；
 *         开机时自动读出所有历史记录，可在 OLED 第 4 个画面翻页查看
 *      8. **通过 ESP-01S 把温湿度无线发给手机**：模块自建 WiFi 热点 + TCP 服务器，
 *         每 2 秒把一行 "T=29.3C H=50.0% #12 A=0" 推给连上的手机客户端
 *
 *  【五个显示画面（短按 K1 换一个，循环；长按 1 秒直接回画面 0）】
 *      画面 0「温湿度」  Temp / Humi / Status / 采样统计
 *      画面 1「时间」    Time:时:分:秒（每秒刷新）/ 开机以来运行秒数
 *      画面 2「极值」    Tmax / Tmin / Hmax / Hmin 四条极值记录
 *      画面 3「历史」    从 W25Q64 读出的历史记录，每页 3 条（+1 行标题）
 *                        ※ 这一页短按是"往后翻页"而不是切画面，
 *                          所以必须用"长按 1 秒"才能退出来（见 5.5 节注释）
 *
 *      画面 4「WiFi」    热点状态 / SSID / IP / 客户端数 / 已推送帧数
 *
 *  【W25Q64 是怎么用的（这是 v1.2 的核心）】
 *      · 空间：8M 字节 = 8,388,608 B，分成 128 个 64KB 块(Block)，
 *              每块 16 个 4KB 扇区(Sector)，共 2048 个扇区
 *      · 【最关键的一条规则】Flash 只能把 1 写成 0，不能把 0 写回 1！
 *        所以改写任何数据前，必须先擦除（擦除 = 整块变成 0xFF）。
 *        擦除的最小单位是「扇区」(4KB)，不能只擦一个字节。
 *      · 因此本项目用「双扇区乒乓」写入策略，避免频繁擦除：
 *              Sector 0 存记录区 A，Sector 1 存记录区 B
 *        新记录顺序追加；当一个扇区写满，就擦掉另一个扇区、从它的 0 地址继续写。
 *        这样每个扇区只在"用满一次"时才擦一次，擦写寿命消耗极慢。
 *      · 写一条记录 = 3 字节（温度×10 两字节 + 湿度×10 一字节）
 *        4KB 扇区预留 4 字节文件头，其余 4092 字节 → 每条 3 字节 → 1364 条/扇区
 *      · 记录带"序号"（自增），读的时候按序号排序，就能还原正确的时间顺序
 *
 *  【时间是怎么来的（这里没有时钟芯片）】
 *      没有外接 RTC，时间从"开机那一刻"起算（开机 = 00:00:00）。
 *      时基由 **TIM2 定时器中断** 提供：每 1ms 进一次中断，中断里给一个
 *      32 位变量 +1。为什么不用 delay 累加？因为 delay 是"软件死等"，
 *      它的每次调用都有指令开销，累加一万次误差就明显了；而定时器中断由
 *      硬件计数触发，与程序在干什么无关，是真正的"时间基准"。
 *      （用硬件定时器中断做系统时基，比软件延时累加精确得多）
 *
 *  【DHT11 单总线协议要点】
 *      · 只有一根数据线，收发分时进行，所以叫"单总线/半双工"
 *      · 主机发起始信号：把线拉低 >18ms，然后释放
 *      · DHT11 应答：先拉低 80us，再拉高 80us
 *      · 之后连续 40 位，每一位 = 50us 低电平 + 高电平，
 *        其中高电平持续 26~28us 表示 0、70us 表示 1
 *        → 用"时间长短"编码，这是单总线最巧妙的地方
 *      · 40 位拼成 5 字节：湿度整数 湿度小数 温度整数 温度小数 校验和
 *      · 校验和 = 前 4 字节相加取最低 8 位
 *
 *  【上电自检的 LED 故障码（没接串口时靠闪灯读）】
 *      长亮 1.5 秒  = 程序在运行，时钟与延时正常（不依赖任何外设）
 *      闪 2 次      = SDA 自检通过
 *      闪 1 次      = SDA 自检失败（线没接上/断线/模块没供电）
 *      闪 3 次      = W25Q64 识别成功（读到了正确的 JEDEC ID 0xEF4017）
 *
 *  【主循环的 LED 状态（三档频率，一眼可分）】
 *      1Hz 慢闪（每 500ms 翻转） = 工作正常，温湿度未超阈值
 *      4Hz 快闪（每 125ms 翻转） = 报警：温度 > 30.0°C 或 湿度 > 80.0%
 *      8Hz 超快闪（每 62ms 翻转）= DHT11 读取失败（具体 err 编号看串口/OLED）
 *      另外：短按 LED 闪 1 次、长按闪 2 次，作为"按键已响应"的反馈，
 *            不用盯着屏幕也能确认自己那一下是短按还是长按。
 *
 *  【验收标准】OLED 上能看到实时温湿度数值，按键可切换四个画面，
 *             历史记录能写入 W25Q64 并掉电不丢，超阈值时 LED 闪 + 蜂鸣器响。
 ******************************************************************************
 */

#include <stdint.h>

/* ==========================================================================
 * 1. 寄存器地址定义
 *    地址来自《STM32F103 参考手册》存储器映射表。写裸机程序不靠库函数，
 *    靠的就是"基地址 + 偏移量"直接访问外设寄存器。
 * ========================================================================== */

/* RCC 复位与时钟控制，基地址 0x40021000 */
#define RCC_CR          (*(volatile uint32_t *)0x40021000U)   /* 时钟控制      +0x00 */
#define RCC_CFGR        (*(volatile uint32_t *)0x40021004U)   /* 时钟配置      +0x04 */
#define RCC_APB2ENR     (*(volatile uint32_t *)0x40021018U)   /* APB2 时钟使能 +0x18 */
#define RCC_APB1ENR     (*(volatile uint32_t *)0x4002101CU)   /* APB1 时钟使能 +0x1C */

/* FLASH 接口，基地址 0x40022000 */
#define FLASH_ACR       (*(volatile uint32_t *)0x40022000U)   /* 访问控制寄存器 +0x00 */

/* GPIOB，基地址 0x40010C00 */
#define GPIOB_CRL       (*(volatile uint32_t *)0x40010C00U)   /* 端口配置低（引脚0-7）  +0x00 */
#define GPIOB_IDR       (*(volatile uint32_t *)0x40010C08U)   /* 输入数据寄存器         +0x08 */
#define GPIOB_ODR       (*(volatile uint32_t *)0x40010C0CU)   /* 输出数据寄存器         +0x0C */
#define GPIOB_BSRR      (*(volatile uint32_t *)0x40010C10U)   /* 位设置/清除寄存器      +0x10 */

/* GPIOC，基地址 0x40011000 */
#define GPIOC_CRH       (*(volatile uint32_t *)0x40011004U)   /* 端口配置高（引脚8-15） +0x04 */
#define GPIOC_ODR       (*(volatile uint32_t *)0x4001100CU)   /* 输出数据寄存器         +0x0C */

/* GPIOA，基地址 0x40010800
 *   CRL 管引脚 0~7（SPI1 用 PA4/PA5/PA6/PA7，正好都在低 8 位里）
 *   CRH 管引脚 8~15（PA9/PA10 串口） */
#define GPIOA_CRL       (*(volatile uint32_t *)0x40010800U)   /* 端口配置低             +0x00 */
#define GPIOA_CRH       (*(volatile uint32_t *)0x40010804U)   /* 端口配置高             +0x04 */
#define GPIOA_IDR       (*(volatile uint32_t *)0x40010808U)   /* 输入数据寄存器         +0x08 */
#define GPIOA_ODR       (*(volatile uint32_t *)0x4001080CU)   /* 输出数据寄存器         +0x0C */
#define GPIOA_BSRR      (*(volatile uint32_t *)0x40010810U)   /* 位设置/清除寄存器      +0x10 */

/* SPI1，基地址 0x40013000（挂在 APB2 上）
 *   用途：驱动 W25Q64 这类 SPI Flash。只做主模式、只发不收或收发同步的 8 位传输。
 *   各寄存器含义：
 *     CR1  控制寄存器1：主/从、时钟极性相位、分频、使能
 *     SR   状态寄存器：TXE(发送空) RXNE(收到数据) BSY(忙)
 *     DR   数据寄存器：写进去=发一个字节；读出来=收到一个字节 */
#define SPI1_CR1        (*(volatile uint32_t *)0x40013000U)   /* 控制寄存器 1           +0x00 */
#define SPI1_SR         (*(volatile uint32_t *)0x40013008U)   /* 状态寄存器             +0x08 */
#define SPI1_DR         (*(volatile uint32_t *)0x4001300CU)   /* 数据寄存器             +0x0C */

/* USART1，基地址 0x40013800 */
#define USART1_SR       (*(volatile uint32_t *)0x40013800U)   /* 状态寄存器             +0x00 */
#define USART1_DR       (*(volatile uint32_t *)0x40013804U)   /* 数据寄存器             +0x04 */
#define USART1_BRR      (*(volatile uint32_t *)0x40013808U)   /* 波特率寄存器           +0x08 */
#define USART1_CR1      (*(volatile uint32_t *)0x4001380CU)   /* 控制寄存器 1           +0x0C */

/* USART2，基地址 0x40004400（注意：它挂在 **APB1** 上，USART1 在 APB2 上）
 * 用途：接 ESP-01S WiFi 模块，走 AT 指令。APB1 时钟是主频的一半（32MHz），
 *       所以算 BRR 时用的 fCK 是 32MHz 而不是 64MHz —— 这个区别搞错就全是乱码。 */
#define USART2_SR       (*(volatile uint32_t *)0x40004400U)   /* 状态寄存器             +0x00 */
#define USART2_DR       (*(volatile uint32_t *)0x40004404U)   /* 数据寄存器             +0x04 */
#define USART2_BRR      (*(volatile uint32_t *)0x40004408U)   /* 波特率寄存器           +0x08 */
#define USART2_CR1      (*(volatile uint32_t *)0x4000440CU)   /* 控制寄存器 1           +0x0C */

/* Cortex-M3 内核私有外设：SysTick 系统滴答定时器，基地址 0xE000E010 */
#define SYSTICK_CTRL    (*(volatile uint32_t *)0xE000E010U)   /* 控制与状态 +0x00 */
#define SYSTICK_LOAD    (*(volatile uint32_t *)0xE000E014U)   /* 重装载值   +0x04 */
#define SYSTICK_VAL     (*(volatile uint32_t *)0xE000E018U)   /* 当前计数值 +0x08 */

#define SYSTICK_ENABLE      (1U << 0)     /* 使能计数          */
#define SYSTICK_CLKSOURCE   (1U << 2)     /* 时钟源=内核时钟(不分频) */
#define SYSTICK_COUNTFLAG   (1U << 16)    /* 倒数到 0 时硬件置 1     */

/* TIM2 通用定时器，基地址 0x40000000（挂在 APB1 上）
 * 用途：产生 1ms 周期中断，作为"系统时基"，给软件时间显示计时 */
#define TIM2_CR1        (*(volatile uint32_t *)0x40000000U)   /* 控制寄存器 1   +0x00 */
#define TIM2_DIER       (*(volatile uint32_t *)0x4000000CU)   /* 中断使能       +0x0C */
#define TIM2_SR         (*(volatile uint32_t *)0x40000010U)   /* 状态寄存器     +0x10 */
#define TIM2_EGR        (*(volatile uint32_t *)0x40000014U)   /* 事件产生       +0x14 */
#define TIM2_CNT        (*(volatile uint32_t *)0x40000024U)   /* 当前计数值     +0x24 */
#define TIM2_PSC        (*(volatile uint32_t *)0x40000028U)   /* 预分频器       +0x28 */
#define TIM2_ARR        (*(volatile uint32_t *)0x4000002CU)   /* 自动重装值     +0x2C */

/* NVIC 中断控制器（Cortex-M3 内核私有外设，基地址 0xE000E100）
 * ISER = Interrupt Set-Enable Register，写 1 到对应位就打开某个中断
 * 编号 0~31  用 ISER0（偏移 0x00），编号 32~63 用 ISER1（偏移 0x04）
 *   TIM2   的编号是 28 -> ISER0 的 bit 28
 *   USART2 的编号是 38 -> ISER1 的 bit 6                              */
#define NVIC_ISER0      (*(volatile uint32_t *)0xE000E100U)
#define NVIC_ISER1      (*(volatile uint32_t *)0xE000E104U)

/* ==========================================================================
 * 2. 系统时钟：HSI(8MHz) / 2 = 4MHz  ->  PLL x16  =  64MHz
 *
 *    为什么不用外部晶振(HSE)？最小系统板虽然焊了 8MHz 晶振，但用 HSI+PLL
 *    不依赖外部器件是否起振，程序更"皮实"，64MHz 对延时精度已完全够用。
 *    注意：主频超过 48MHz 必须给 Flash 插入等待周期，否则取指会出错。
 * ========================================================================== */
static uint32_t g_cpu_mhz = 8;    /* 当前内核主频(MHz)，SysTick 延时按它换算 */

static void Clock_Init(void)
{
    /* Flash：使能预取(PRFTBE)，2 个等待周期(LATENCY=010) —— 必须在升频之前设置 */
    FLASH_ACR = (1U << 4) | 0x02U;

    /* 配置 PLL 和总线分频（此时系统时钟仍是 HSI，改配置是安全的） */
    RCC_CFGR = (0x0EU << 18)    /* PLLMUL = 1110 -> 4MHz x 16 = 64MHz        */
             | (0x04U << 8)     /* PPRE1  = 100  -> APB1 = 64/2 = 32MHz(限36MHz)*/
             | (0x00U << 4)     /* PPRE2  = 000  -> APB2 = 64MHz             */
             | (0x00U << 0);    /* SW     = 00   -> 系统时钟暂时仍为 HSI      */

    /* 打开 PLL，并等待 PLL 锁定（带超时，避免死等） */
    RCC_CR |= (1U << 24);       /* PLLON */
    for (volatile uint32_t t = 0; t < 200000U; t++)
    {
        if (RCC_CR & (1U << 25)) break;      /* PLLRDY = 1 表示锁定 */
    }

    if (RCC_CR & (1U << 25))
    {
        RCC_CFGR = (RCC_CFGR & ~0x03U) | 0x02U;      /* SW = 10 -> 选 PLL 作系统时钟 */
        for (volatile uint32_t t = 0; t < 200000U; t++)
        {
            if (((RCC_CFGR >> 2) & 0x03U) == 0x02U) break;   /* SWS 回读确认切换成功 */
        }
        if (((RCC_CFGR >> 2) & 0x03U) == 0x02U)
        {
            g_cpu_mhz = 64;
        }
    }
    /* 万一 PLL 没起来：保持 HSI 8MHz 继续跑，程序不会死，只是延时会偏长 */
}

/* ==========================================================================
 * 3. 延时：用内核自带的 SysTick 做 1us 基准，比"for 空循环"准得多
 * ========================================================================== */
static void SysTick_Init(void)
{
    SYSTICK_CTRL = SYSTICK_CLKSOURCE;      /* 时钟源选内核时钟，先不使能计数 */
    (void)SYSTICK_CTRL;                    /* 读一次，清除遗留的 COUNTFLAG   */
    SYSTICK_LOAD = 0xFFFFFFU;
    SYSTICK_VAL  = 0;
}

void delay_us(uint32_t us)
{
    uint32_t reload;

    if (us == 0U) return;

    reload = us * g_cpu_mhz;               /* 1us 需要 g_cpu_mhz 个内核时钟 */
    if (reload > 0xFFFFFFU) reload = 0xFFFFFFU;

    SYSTICK_LOAD = reload - 1U;
    SYSTICK_VAL  = 0;
    SYSTICK_CTRL = SYSTICK_CLKSOURCE | SYSTICK_ENABLE;

    while ((SYSTICK_CTRL & SYSTICK_COUNTFLAG) == 0U);   /* 等倒数到 0 */

    SYSTICK_CTRL = SYSTICK_CLKSOURCE;      /* 关闭计数 */
}

void delay_ms(uint32_t ms)
{
    while (ms--)
    {
        delay_us(1000U);
    }
}

/* ==========================================================================
 * 3.5 系统时基：TIM2 每 1ms 中断一次
 *
 *    为什么单独用一个定时器，而不继续用 SysTick？
 *      因为 SysTick 已经被 delay_us/delay_ms 借去当"秒表"了 —— 它每次都被
 *      重新装载、用完就关，属于"一次性"用法。要得到连续的"墙上时间"，
 *      需要一个一直在跑、从不被打扰的计数器，所以改用 TIM2。
 *
 *    【定时器时钟怎么算（STM32 一个容易漏掉的点）】
 *      APB 预分频系数不等于 1 时，定时器时钟 = PCLK x 2。
 *      本例 APB1 = 64MHz/2 = 32MHz，所以 TIM2 计数时钟 = 32MHz x 2 = 64MHz。
 *      再配 PSC=63（64 分频）得到 1MHz，即每个计数正好 1us；
 *      ARR=999（数满 1000 个计数溢出），于是每 1000us = 1ms 中断一次。
 * ========================================================================== */

/* 关/开全局中断。cpsid i / cpsie i 是 Cortex-M3 指令，直接改动 PRIMASK 寄存器。
 * 为什么需要它们？DHT11 靠"数时间长短"区分 0 和 1，一旦被中断打断，
 * 采样点就会被推迟，可能把 1 读成 0 —— 所以那几毫秒必须把中断关掉。 */
static inline void Irq_Disable(void) { __asm volatile ("cpsid i" ::: "memory"); }
static inline void Irq_Enable(void)  { __asm volatile ("cpsie i" ::: "memory"); }

static volatile uint32_t g_ms_tick;      /* 开机以来的毫秒数，只在中断里累加 */

static void TIM2_Init(void)
{
    RCC_APB1ENR |= (1U << 0);            /* 使能 TIM2 时钟 */

    TIM2_PSC = 63U;                      /* 64MHz / (63+1) = 1MHz -> 1 个计数 = 1us */
    TIM2_ARR = 999U;                     /* 数满 1000 次溢出 -> 每 1ms 中断一次     */

    TIM2_EGR = 1U;                       /* UG：立刻把 PSC/ARR 装入影子寄存器 */
    TIM2_SR  = 0U;                       /* 清掉 UG 顺带产生的更新标志        */

    TIM2_DIER = 1U;                      /* UIE：允许"更新事件"中断 */
    TIM2_CR1  = 1U;                      /* CEN：启动计数           */

    NVIC_ISER0 = (1U << 28);             /* 在 NVIC 里打开 TIM2 中断（编号 28） */
}

/* TIM2 中断服务函数。函数名必须和启动文件向量表里的一致，
 * 写对了就会自动覆盖启动文件里的弱定义（那个 WEAK 版本是个死循环）。 */
void TIM2_IRQHandler(void)
{
    TIM2_SR = 0U;                        /* 清中断标志，否则会一直重复进中断 */
    g_ms_tick++;
}

/* 把毫秒计数换算成 时/分/秒。全整数运算，开销很小 */
static void Time_Get(uint32_t *hh, uint32_t *mm, uint32_t *ss)
{
    uint32_t t = g_ms_tick / 1000U;      /* 先换算成"秒" */

    *ss = t % 60U;
    t  /= 60U;
    *mm = t % 60U;
    t  /= 60U;
    *hh = t % 24U;                       /* 满 24 小时回零（没有日历，只显示时:分:秒） */
}

/* ==========================================================================
 * 4. 串口调试输出（USART1，115200 波特率，8 数据位 无校验 1 停止位）
 *
 *    接线（最少只要两根线）：
 *        STM32 PA9(TX)  ->  CH340 的 RXD
 *        STM32 GND      ->  CH340 的 GND
 *    波特率由主频算出：BRR = 主频 / 波特率（定点小数，低 4 位是小数部分）
 *
 *    为什么第 1 天就加串口？因为排查硬件故障时，"打印出看到什么"
 *    比"数 LED 闪几次"可靠得多，后面第 2 天打印温湿度也要用它。
 * ========================================================================== */
static void UART_Init(void)
{
    RCC_APB2ENR |= (1U << 2) | (1U << 14);   /* 使能 GPIOA、USART1 时钟 */

    /* PA9 -> 复用推挽输出 50MHz
     * 每个引脚的 4 位配置域 = CNF[3:2] 在高位、MODE[1:0] 在低位，即 nibble = CNF<<2 | MODE
     *   复用推挽 50MHz：CNF=10, MODE=11 -> 10<<2|11 = 1011b = 0xB
     *   【踩过的坑】原先写的 0xE = 1110b 其实是"复用开漏 2MHz"：开漏推不出高电平，
     *   串口的空闲位/停止位全部浮空，CH340 收不到任何正确数据（现象：全 0x00）。 */
    GPIOA_CRH &= ~(0x0FU << 4);
    GPIOA_CRH |=  (0x0BU << 4);

    /* PA10 -> 浮空输入：CNF=01, MODE=00 -> 01<<2|00 = 0100b = 0x4 */
    GPIOA_CRH &= ~(0x0FU << 8);
    GPIOA_CRH |=  (0x04U << 8);

    /* 【BRR 到底怎么算 —— 这里踩过最大的一个坑】
     *
     * STM32F1 的 BRR 不是"把 fCK/波特率 的整数商写进去"就完了，
     * 它是一个 12 位整数 + 4 位小数的【定点小数】：
     *
     *     寄存器位 [15:4] = USARTDIV 的整数部分（Mantissa，12 位）
     *     寄存器位 [3:0]  = USARTDIV 的小数部分（Fraction，1/16 精度）
     *
     * 而波特率与 USARTDIV 的关系是：
     *
     *     波特率 = fCK / (16 * USARTDIV)
     *  => USARTDIV = fCK / (16 * 波特率)
     *  => BRR      = round(USARTDIV * 16) = round(fCK / 波特率)
     *
     * 注意最后一步化简：**BRR 在数值上就等于 fCK / 波特率**，
     * 那个 16 在"乘 16"和"除 16"里已经互相抵消掉了。
     *
     * 【原先的错误写法】
     *     USART1_BRR = (g_cpu_mhz * 1000000U * 16U) / 115200U;
     * 这里多乘了一个 16，算出 8888 = 0x22B8。
     * 按定点数拆开就是 USARTDIV = 555 + 8/16，代入公式得
     *     实际波特率 = 64MHz / 8888 = 7200 bps
     * 而目标是 115200 bps —— 差了整整 16 倍！
     *
     * 【症状特征（记住这个，很典型）】
     *   串口助手在 115200 下收到【满屏 0x00】。
     *   原因：发送端实际只有 7200 bps，接收端按 115200 采样，
     *   每个真实的数据位被当成 16 个位来读，位同步彻底错乱。
     *   而且它不是纯噪声 —— 它会呈现出与固件发送周期一致的"周期性突发"，
     *   这正是"线是通的、数据在发、但速率不对"的典型指纹。
     *
     * 【正确写法】先算整数商，再把余数折算成 1/16 的小数部分。
     *   64MHz / 115200 = 555 余 64000
     *   余数折算：(64000 * 16) / 115200 = 8.89 -> 取 8 或做四舍五入
     *   所以 BRR = (555 << 4) | 8 = 8888？不对！！
     *
     *   等一下，这里正是容易绕晕的地方：555.5 这个"整数+小数"是
     *   【已经乘过 16 之前】的 USARTDIV 吗？不是。
     *   USARTDIV = fCK / (16 * baud) = 64000000/(16*115200) = 34.7222
     *   BRR = round(34.7222 * 16) = round(555.5556) = 556 = 0x022C
     *       = (34 << 4) | 12  -> 整数 34、小数 12/16
     *
     * 所以最简单可靠的写法就是直接算 fCK / 波特率 并四舍五入：
     *     BRR = (fCK + 波特率/2) / 波特率
     * 这里 fCK = 64,000,000，波特率 = 115200：
     *     (64000000 + 57600) / 115200 = 556（整数除法自然截断）
     * 实际波特率 = 64MHz / 556 = 115107.9 bps，误差 -0.08%，非常理想。 */
    USART1_BRR = ((g_cpu_mhz * 1000000U) + (115200U / 2U)) / 115200U;

    USART1_CR1 = (1U << 13)      /* UE：串口使能      */
               | (1U << 3)       /* TE：发送使能      */
               | (1U << 2);      /* RE：接收使能      */
}

static void UART_PutC(char c)
{
    while ((USART1_SR & (1U << 7)) == 0U);   /* TXE=1 表示发送寄存器空了 */
    USART1_DR = (uint32_t)c;
}

static void UART_Puts(const char *s)
{
    while (*s != '\0')
    {
        UART_PutC(*s);
        s++;
    }
}

static void UART_Putu(uint32_t v)
{
    char    buf[11];
    uint8_t i = 0U;

    if (v == 0U)
    {
        UART_PutC('0');
        return;
    }
    while (v > 0U)
    {
        buf[i] = (char)('0' + (v % 10U));
        v /= 10U;
        i++;
    }
    while (i > 0U)
    {
        i--;
        UART_PutC(buf[i]);
    }
}

static void UART_PutHex(uint8_t v)
{
    const char *hex = "0123456789ABCDEF";

    UART_PutC(hex[(v >> 4) & 0x0FU]);
    UART_PutC(hex[v & 0x0FU]);
}

/* 打印"带一位小数的数值"。参数是放大 10 倍后的整数：256 表示 25.6
 * 为什么要放大 10 倍存？因为单片机里能省则省，用整数运算代替浮点，
 * 速度更快、也不用把浮点库链接进来（浮点库会让 Flash 占用大一圈）。 */
static void UART_PutFix1(int16_t v10)
{
    uint16_t a;

    if (v10 < 0) { UART_PutC('-'); a = (uint16_t)(-v10); }
    else         { a = (uint16_t)v10; }

    UART_Putu((uint32_t)(a / 10U));       /* 整数部分 */
    UART_PutC('.');
    UART_PutC((char)('0' + (a % 10U)));   /* 小数部分 */
}

/* ==========================================================================
 * 5. GPIO 初始化
 *
 *    STM32F1 每个引脚占 CRL/CRH 里的 4 个位，格式为（这是本工程踩过的最大的坑）：
 *        【位3位2 = CNF(输出形式)，位1位0 = MODE(模式/速度)】  —— 顺序绝不可记反
 *        即 nibble = CNF<<2 | MODE
 *        MODE：00 输入；01 输出10MHz；10 输出2MHz；11 输出50MHz
 *        CNF（输出模式）：00 通用推挽；01 通用开漏；10 复用推挽；11 复用开漏
 *        CNF（输入模式）：00 模拟；01 浮空；10 上拉/下拉；11 保留
 *        常用值速查：浮空输入=0x4  推挽输出2MHz=0x2  通用开漏50MHz=0x7
 *                    复用推挽50MHz=0xB  复用开漏50MHz=0xF
 *        （复位默认 0x44444444 = 全部浮空输入，可用来对照校验）
 * ========================================================================== */

/* ---------------------------------------------------------------------------
 * 【引脚角色开关】I2C_SWAP_DIAG
 *   0 = 正常映射：PB6 = SCL、PB7 = SDA（本项目默认）
 *   1 = 交换映射：PB7 = SCL、PB6 = SDA
 *
 *   用途：屏幕有供电、两线也都有上拉，却完全不应答时，用来验证
 *        "模块的 SCL/SDA 是不是被接反了"。
 *   ※ PB6/PB7 两个引脚在 CRL 里的配置是一样的（都是通用开漏 0x7），
 *     所以交换角色不需要动 GPIO 配置，只换"谁当时钟、谁当数据"。
 *   正常运行时必须是 0。
 * ------------------------------------------------------------------------- */
#define I2C_SWAP_DIAG   0U

#if (I2C_SWAP_DIAG != 0U)
  #define PIN_SCL         7U       /* 交换：PB7 当时钟 */
  #define PIN_SDA         6U       /* 交换：PB6 当数据 */
#else
  #define PIN_SCL         6U       /* 正常：PB6 = SCL */
  #define PIN_SDA         7U       /* 正常：PB7 = SDA */
#endif

/* I2C 两条线：空闲时都要为高，靠外部上拉电阻拉高，所以必须配成"开漏输出" */
#define SCL_HIGH()      (GPIOB_ODR |=  (1U << PIN_SCL))
#define SCL_LOW()       (GPIOB_ODR &= ~(1U << PIN_SCL))
#define SDA_HIGH()      (GPIOB_ODR |=  (1U << PIN_SDA))
#define SDA_LOW()       (GPIOB_ODR &= ~(1U << PIN_SDA))
#define SDA_READ()      ((GPIOB_IDR >> PIN_SDA) & 0x01U)   /* 读 SDA 实际电平 */

/* 板载 LED：PC13 低电平点亮（阳极接 3.3V）。若你的板子相反，把两句对调即可 */
#define LED_ON()        (GPIOC_ODR &= ~(1U << 13))
#define LED_OFF()       (GPIOC_ODR |=  (1U << 13))
#define LED_TOGGLE()    (GPIOC_ODR ^=  (1U << 13))

/* 按键 K1：一端接 PB1，另一端接 GND。
 * 配上拉输入后：松开时线上被内部上拉拉高 -> 读到 1；按下被接到 GND -> 读到 0 */
#define KEY_READ()      ((GPIOB_IDR >> 1) & 0x01U)   /* 0 = 按下，1 = 松开 */

static void GPIO_Init(void)
{
    RCC_APB2ENR |= (1U << 3) | (1U << 4);      /* 使能 GPIOB、GPIOC 时钟 */

    /* PB6(SCL)、PB7(SDA)：通用开漏输出 50MHz
     *   CNF=01(通用开漏), MODE=11(50MHz) -> 01<<2|11 = 0111b = 0x7，两个引脚 => 0x77
     *   【踩过的坑】原先写的 0xD = 1101b 其实是"复用开漏"：引脚输出交给片上 I2C1 外设控制，
     *   而 I2C1 的时钟从未使能（RCC_APB1ENR=0），软件写 ODR 完全不起作用 ——
     *   实测现象：ODR 写 1（释放）而 IDR 始终读到 0，OLED 必不可能亮。 */
    GPIOB_CRL &= ~(0xFFU << 24);
    GPIOB_CRL |=  (0x77U << 24);

    /* PC13(LED)：推挽输出 2MHz
     *   CNF=00(通用推挽), MODE=10(2MHz) -> 00<<2|10 = 0010b = 0x2
     * 注意：数据手册规定 PC13/PC14/PC15 最高只能 2MHz，不能配 50MHz
     *   【踩过的坑】原先写的 0x8 = 1000b 其实是"上拉/下拉输入"：
     *   引脚不主动驱动，LED 只能靠内部约 40k 的上/下拉通过微安级电流，
     *   表现为"能看出在闪、但远比正常暗"。 */
    GPIOC_CRH &= ~(0x0FU << 20);
    GPIOC_CRH |=  (0x02U << 20);

    /* PB0(DHT11 的 DATA 线)：上拉输入
     *   CNF=10(上拉/下拉输入), MODE=00(输入) -> 10<<2|00 = 1000b = 0x8
     *   输入模式下，是靠 ODR 这一位来决定"上拉"还是"下拉"：
     *       对应 ODR 位 = 1 -> 启用上拉（大约是 40kΩ 接到 3.3V）
     *       对应 ODR 位 = 0 -> 启用下拉
     *   平时让总线保持高电平（空闲态），读取时也靠它把线拉回去。
     *   注意：STM32F1 内部上拉只在"输入模式"下才有，输出模式没有。 */
    GPIOB_ODR |=  (1U << 0);
    GPIOB_CRL &= ~(0x0FU << 0);
    GPIOB_CRL |=  (0x08U << 0);

    /* PB1(按键 K1)：上拉输入
     *   CNF=10(上拉/下拉输入), MODE=00(输入) -> 10<<2|00 = 1000b = 0x8
     *   和 PB0 同理，输入模式下 ODR 的这一位决定上拉还是下拉：
     *       ODR 位 = 1 -> 上拉（约 40k 接 3.3V）  <- 按键要的就是这个
     *       ODR 位 = 0 -> 下拉
     *   所以按键可以直接一端接引脚、一端接 GND，不用焊外部电阻；
     *   没按的时候读 1，按下被拉到 GND 读 0。 */
    GPIOB_ODR |=  (1U << 1);
    GPIOB_CRL &= ~(0x0FU << 4);
    GPIOB_CRL |=  (0x08U << 4);

    SCL_HIGH();                                 /* I2C 总线空闲态：两线置高 */
    SDA_HIGH();
    LED_OFF();
}

/* ==========================================================================
 * 5.5 按键扫描（PB1，上拉输入，按下接地）
 *
 *    三个必须处理的细节：
 *      ① 消抖：机械按键的触点在闭合/断开的瞬间会有 5~10ms 的电平抖动。
 *         不处理的话，按一次会被识别成好几次，显示画面会一下子连跳几格。
 *      ② 检测"按下沿"而不是"按下状态"：只有"松开 -> 按下"这个瞬间算一次
 *         有效动作；一直按住也只算一次，不会连续触发。
 *      ③ 区分短按 / 长按：长按用来"从一个画面直接跳回主画面"。
 *         这是必需的 —— 历史记录画面上短按是翻页，如果只有短按，
 *         用户进了历史画面就再也出不来，只能按复位键，等于把功能做死了。
 *
 *    【为什么在"松手那一刻"才上报按键，而不是按下就上报】
 *      只有松手才知道这次按压持续了多久，才能区分短按和长按。
 *      如果按下瞬间先报一次短按、按住 1 秒后再报一次长按，
 *      同一个动作会触发两次：用户会看到"先翻了一页、又突然跳回主画面"。
 * ========================================================================== */

#define KEY_LONG_MS     1000U       /* 按住超过这个时长算长按 */

#define MODE_NUM        5U          /* 一共几个显示画面 */
static uint8_t g_mode;              /* 当前画面：0=温湿度 1=时间 2=极值 3=历史 4=WiFi */
/* 历史记录每页显示几条。
 * 【为什么是 3 而不是 4 —— 这是踩过坑的数字】
 *   OLED 是 128x64，纵向 64 像素被切成 8 个 page（每 page 8 像素）。
 *   我们的字库是 8x16 点阵，一个字符要占"上半 page + 下半 page"两格，
 *   所以整屏实际只有 4 行字符位置：page 0 / 2 / 4 / 6。
 *   历史画面需要 1 行标题（显示 H第几页/共几页），剩下的 3 行才是数据。
 *   → 每页 3 条。
 *   【曾经写成 4 条的后果】数据行从 page 1 起每两格排一行（1/3/5/7），
 *   结果是：① 第 1 条覆盖标题下半截；② 每条记录上下错开 8 像素、互相重叠；
 *   ③ 第 4 条落在 page 7，而 OLED_ShowChar 有 `page > 6 直接 return` 的保护，
 *   整行一个字都不画。整页糊成一团，看着"全是温度数字"。
 *   这个错误在纸面上很难发现，必须知道"字符占两页"才能推出来。 */
#define HIST_PER_PAGE   3U          /* 历史记录每页 3 条（+1 行标题 = 4 行占满屏幕） */
#define HIST_ROW_TOP    2U          /* 第 1 条数据画在第 2 行，即 page 2 */

/* 历史画面只允许翻看「最近 HIST_MAX 条」，而不是全部 2728 条。
 * 【为什么要设这个上限】两个原因：
 *   ① g_hist_page 是 uint8_t（省内存），而全部记录有 2728 条 = 910 页，
 *      910 > 255，用 uint8 会在第 255 页之后回绕，翻页直接乱掉。
 *   ② 从实用角度看，用户想看的就是"最近发生了什么"，
 *      翻几百页去找三天前的数据既不现实也没必要。
 * 200 条 = 67 页（每页 3 条），翻起来刚好，也远小于 255 这个上限。 */
#define HIST_MAX        200U
static uint8_t g_hist_page;         /* 历史记录画面：当前看第几页（每页 4 条） */

/* 按键扫描。返回值：0 = 没动作，1 = 短按，2 = 长按（按住超过 KEY_LONG_MS） */
static uint8_t Key_Scan(void)
{
    static uint8_t  last    = 1U;       /* 上次的电平，1 = 松开（上电初始态） */
    static uint32_t t_press = 0U;       /* 这次按下是第几毫秒，松手时用来算时长 */

    uint8_t now = KEY_READ();

    if ((last == 1U) && (now == 0U))    /* 1 -> 0 的下降沿 = 刚刚按下 */
    {
        delay_ms(15U);                  /* 等抖动过去，再确认一次 */
        if (KEY_READ() == 0U)
        {
            last    = 0U;
            t_press = g_ms_tick;        /* 记下按下时刻 */
        }
        return 0U;                      /* 按下这一刻先不报，等松手再定性 */
    }

    if ((last == 0U) && (now == 1U))    /* 0 -> 1 = 刚刚松手，这时才能算出按了多久 */
    {
        last = 1U;
        return ((g_ms_tick - t_press) >= KEY_LONG_MS) ? 2U : 1U;
    }

    return 0U;    /* 一直按住时 last 保持 0，所以不会重复上报 */
}

/* ==========================================================================
 * 6. 软件模拟 I2C（I2C 时序靠 GPIO 手动翻转实现）
 *
 *    协议要点：
 *      · SCL 全程由主机（STM32）驱动；SDA 双向，谁发数据谁驱动
 *      · 起始信号：SCL 为高时，SDA 由高变低
 *      · 停止信号：SCL 为高时，SDA 由低变高
 *      · 数据位：SCL 为低时改变 SDA，SCL 为高时数据有效
 *      · 第 9 个时钟是应答位 ACK：从机把 SDA 拉低表示"收到"
 * ========================================================================== */
#define I2C_DELAY()     delay_us(2)

static void I2C_Start(void)
{
    SDA_HIGH();
    SCL_HIGH();
    I2C_DELAY();
    SDA_LOW();          /* SCL 为高时 SDA 下降沿 —— 起始信号 */
    I2C_DELAY();
    SCL_LOW();
    I2C_DELAY();
}

static void I2C_Stop(void)
{
    SDA_LOW();
    SCL_HIGH();
    I2C_DELAY();
    SDA_HIGH();         /* SCL 为高时 SDA 上升沿 —— 停止信号 */
    I2C_DELAY();
}

/* 发送一个字节，高位在前；发完不处理应答，由调用者用 I2C_WaitAck 读 */
static void I2C_SendByte(uint8_t dat)
{
    for (uint8_t i = 0; i < 8U; i++)
    {
        SCL_LOW();
        I2C_DELAY();

        if (dat & 0x80U) SDA_HIGH();     /* 先放好数据位，再抬高 SCL 让从机采样 */
        else             SDA_LOW();
        dat <<= 1;

        I2C_DELAY();
        SCL_HIGH();
        I2C_DELAY();
    }
    SCL_LOW();
    I2C_DELAY();
}

/* 读应答：返回 0 = ACK(从机应答)，返回 1 = NACK(无人应答) */
static uint8_t I2C_WaitAck(void)
{
    uint8_t ack;

    SDA_HIGH();         /* 主机释放 SDA 线，交给从机控制 */
    I2C_DELAY();
    SCL_HIGH();         /* 第 9 个时钟 */
    I2C_DELAY();

    ack = (uint8_t)SDA_READ();      /* 从机拉低则为 0(ACK) */

    SCL_LOW();
    I2C_DELAY();
    return ack;
}

/* ==========================================================================
 * 6.5 总线诊断：读空闲电平 + 扫描所有地址
 *     OLED 不亮时，这两个函数是定位问题的关键：
 *       · 空闲电平读不到高 -> 上拉/接线/供电问题（跟程序无关）
 *       · 扫描到 0 个器件   -> 总线上什么都没接对
 *       · 扫到别的地址      -> 总线是通的，只是地址不是 0x78/0x7A
 * ========================================================================== */

/* 把 PB6/PB7 临时切成浮空输入，读总线空闲电平 */
static void I2C_BusIdleCheck(uint8_t *scl_lv, uint8_t *sda_lv)
{
    uint32_t save = GPIOB_CRL;

    GPIOB_CRL = (GPIOB_CRL & ~(0xFFU << 24)) | (0x44U << 24);   /* 0x4 = 浮空输入 */
    delay_us(200U);                                             /* 等电平稳定   */

    *scl_lv = (uint8_t)((GPIOB_IDR >> PIN_SCL) & 0x01U);
    *sda_lv = (uint8_t)((GPIOB_IDR >> PIN_SDA) & 0x01U);

    GPIOB_CRL = save;                                           /* 恢复开漏输出 */
}

/* SDA 回读自检（判断"线到底有没有接上"最有效的一招）
 *   开漏输出的特点是：写 1 = 释放总线，电平由外部上拉电阻决定。
 *   所以"写 1 后回读到 1"说明线上有上拉、接线正常；
 *   如果写 1 后回读还是 0，说明这根线悬空、断线，或者模块根本没有上拉电阻。
 *   返回 1 = 通过，返回 0 = 异常。
 *
 *   为什么需要这一招？因为 SDA 悬空时电平不确定，I2C 读应答会读到随机的 0，
 *   于是程序误以为"从机应答了"，实际什么都没收到 —— 正是"LED 慢闪但屏幕全黑"的成因。 */
static uint8_t I2C_SdaEchoTest(void)
{
    uint8_t ok = 1U;

    SDA_HIGH();                     /* 释放 */
    delay_us(200U);
    if (SDA_READ() != 1U) ok = 0U;  /* 释放后应为高电平 */

    SDA_LOW();                      /* 拉低 */
    delay_us(200U);
    if (SDA_READ() != 0U) ok = 0U;  /* 拉低后应为低电平，说明引脚可控 */

    SDA_HIGH();                     /* 恢复空闲态 */
    return ok;
}

/* ==========================================================================
 * 6.6 【接反判别】用任意两个引脚发一次 I2C 地址探测
 *
 *    为什么需要它？
 *      屏幕有供电、SCL/SDA 也都测到上拉（说明线确实插在模块上），
 *      但总线扫描一个器件都找不到 —— 这时最大的嫌疑就是
 *      "模块的 SCL 和 SDA 两根线接反了"。
 *      接反之所以难以察觉：两根线各自的电气特性完全正常
 *        （各自都有上拉、都能拉低、互不干扰），
 *        唯独时钟和数据角色互换，从机永远等不到合法的起始条件 -> 必然零应答。
 *
 *    判据：
 *      映射A(SCL=6,SDA=7) 无应答、映射B(SCL=7,SDA=6) 有应答
 *          -> 两根线接反了，对调即可
 *      两种映射都无应答
 *          -> 不是接反，去查模块 GND 是否虚接、模块本身是否损坏
 *
 *    返回 0 = 收到 ACK（找到了器件），返回 1 = 无应答
 * ========================================================================== */
static uint8_t I2C_ProbePin(uint8_t scl_bit, uint8_t sda_bit, uint8_t addr8)
{
    uint8_t i;
    uint8_t ack;

#define _SCL_H()   (GPIOB_BSRR = (1U << scl_bit))
#define _SCL_L()   (GPIOB_BSRR = (1U << (scl_bit + 16U)))
#define _SDA_H()   (GPIOB_BSRR = (1U << sda_bit))
#define _SDA_L()   (GPIOB_BSRR = (1U << (sda_bit + 16U)))
#define _SDA_R()   ((uint8_t)((GPIOB_IDR >> sda_bit) & 0x01U))

    /* 两脚在 GPIO_Init 里已配成通用开漏输出，此处只交换"角色" */

    _SDA_H();  _SCL_H();  delay_us(2);
    _SDA_L();  delay_us(2);          /* SCL 为高时 SDA 下降沿 = 起始信号 */
    _SCL_L();  delay_us(2);

    for (i = 0U; i < 8U; i++)        /* 高位在前，逐位送出 */
    {
        _SCL_L();  delay_us(2);
        if ((addr8 & 0x80U) != 0U) { _SDA_H(); } else { _SDA_L(); }
        addr8 = (uint8_t)(addr8 << 1);
        delay_us(2);
        _SCL_H();  delay_us(2);
    }

    _SCL_L();  delay_us(2);
    _SDA_H();  delay_us(2);          /* 主机释放 SDA，交给从机应答 */
    _SCL_H();  delay_us(2);
    ack = _SDA_R();                  /* 0 = 从机把它拉低 = ACK */
    _SCL_L();  delay_us(2);

    _SDA_L();  _SCL_H();  delay_us(2);  _SDA_H();  delay_us(2);   /* 停止信号，放开总线 */

#undef _SCL_H
#undef _SCL_L
#undef _SDA_H
#undef _SDA_L
#undef _SDA_R

    return ack;
}

/* 扫描 0x03~0x77 全部 7 位地址，有应答的按 8 位形式记入 found，返回器件个数 */
static uint8_t I2C_Scan(uint8_t *found, uint8_t max)
{
    uint8_t n = 0U;

    for (uint8_t addr = 0x03U; addr <= 0x77U; addr++)
    {
        I2C_Start();
        I2C_SendByte((uint8_t)(addr << 1));
        if (I2C_WaitAck() == 0U)                    /* 0 = 收到应答 */
        {
            if (n < max) found[n] = (uint8_t)(addr << 1);
            n++;
        }
        I2C_Stop();
    }
    return n;
}

/* ==========================================================================
 * 7. OLED 驱动（SSD1306 控制器，128x64 像素）
 *
 *    每个 I2C 事务的格式：[起始][从机地址][控制字节][数据...][停止]
 *      控制字节 0x00 -> 后面跟的是"命令"
 *      控制字节 0x40 -> 后面跟的是"显示数据"
 *    显存 GDDRAM 组织方式：分成 8 页(page0~7)，每页 8 行高、128 列宽，
 *      所以一个 8x16 的字符正好横跨相邻两页、占 8 列。
 * ========================================================================== */
#define OLED_ADDR_1     0x78U    /* 7 位地址 0x3C 左移一位，市面上最常见的 */
#define OLED_ADDR_2     0x7AU    /* 少数模块是 0x3D，即 0x7A */

/* 列地址偏移量。SSD1306 显存就是 128 列，偏移 0；
 * 而 SH1106 显存有 132 列，同样是 128 列的内容必须整体右移 2 列才对齐。
 * 如果屏幕能亮、但文字偏左且右边有竖条串到左边，把这里改成 2U */
#define OLED_COL_OFFSET 0U

static uint8_t g_oled_addr = OLED_ADDR_1;    /* 探测成功后保存实际地址 */

static void OLED_WrCmd(uint8_t cmd)
{
    I2C_Start();
    I2C_SendByte(g_oled_addr);
    I2C_WaitAck();
    I2C_SendByte(0x00);         /* 命令 */
    I2C_WaitAck();
    I2C_SendByte(cmd);
    I2C_WaitAck();
    I2C_Stop();
}

/* 探测某个地址上有没有器件应答 */
static uint8_t OLED_Probe(uint8_t addr)
{
    uint8_t ack;

    I2C_Start();
    I2C_SendByte(addr);
    ack = I2C_WaitAck();
    I2C_Stop();

    return (ack == 0U) ? 1U : 0U;     /* 1 = 有应答 */
}

/* 设定写显存的起始位置：page=0~7 页，col=0~127 列 */
static void OLED_SetPos(uint8_t page, uint8_t col)
{
    col = (uint8_t)(col + OLED_COL_OFFSET);

    OLED_WrCmd(0xB0U + page);            /* 设置页地址 */
    OLED_WrCmd(col & 0x0FU);             /* 列地址低 4 位  */
    OLED_WrCmd(0x10U | (col >> 4));      /* 列地址高 4 位  */
}

/* 连续填满一整页（128 列），用"一次起始 + 连续写"的方式加快速度 */
static void OLED_FillPage(uint8_t page, uint8_t dat)
{
    OLED_SetPos(page, 0);

    I2C_Start();
    I2C_SendByte(g_oled_addr);
    I2C_WaitAck();
    I2C_SendByte(0x40);                  /* 后面是显示数据（页地址模式下列指针自增） */
    I2C_WaitAck();
    for (uint8_t i = 0; i < 128U; i++)
    {
        I2C_SendByte(dat);
        I2C_WaitAck();
    }
    I2C_Stop();
}

static void OLED_FillAll(uint8_t dat)    /* 全屏填充：0xFF 全亮，0x00 全黑 */
{
    for (uint8_t page = 0; page < 8U; page++)
    {
        OLED_FillPage(page, dat);
    }
}

static void OLED_Clear(void)
{
    OLED_FillAll(0x00U);
}

/* 初始化：返回 1 表示成功找到 OLED，返回 0 表示没有应答 */
static uint8_t OLED_Init(void)
{
    delay_ms(100U);          /* 等 OLED 内部上电复位完成 */

    if (OLED_Probe(OLED_ADDR_1))
    {
        g_oled_addr = OLED_ADDR_1;
    }
    else if (OLED_Probe(OLED_ADDR_2))
    {
        g_oled_addr = OLED_ADDR_2;
    }
    else
    {
        return 0U;           /* 两个地址都没人应答：接线或供电有问题 */
    }

    OLED_WrCmd(0xAE);        /* 关闭显示               */
    OLED_WrCmd(0xD5); OLED_WrCmd(0x80);   /* 时钟分频/振荡频率 */
    OLED_WrCmd(0xA8); OLED_WrCmd(0x3F);   /* 多路复用率 = 64 行 */
    OLED_WrCmd(0xD3); OLED_WrCmd(0x00);   /* 显示垂直偏移 = 0  */
    OLED_WrCmd(0x40);        /* 显示起始行 = 0         */
    OLED_WrCmd(0x8D); OLED_WrCmd(0x14);   /* 电荷泵使能（模块用 3.3V 供电必须开） */
    /* 注：SSD1306 复位后默认就是"页寻址模式"，所以这里不必再发 0x20 0x02；
     *     SH1106 控制器不认识这条命令，去掉它两种屏都能用 */
    OLED_WrCmd(0xA1);        /* 左右方向：列 127 映射到 SEG0 */
    OLED_WrCmd(0xC8);        /* 上下方向：从 COM63 扫描到 COM0 */
    OLED_WrCmd(0xDA); OLED_WrCmd(0x12);   /* COM 引脚硬件配置       */
    OLED_WrCmd(0x81); OLED_WrCmd(0xCF);   /* 对比度（亮度）         */
    OLED_WrCmd(0xD9); OLED_WrCmd(0xF1);   /* 预充电周期             */
    OLED_WrCmd(0xDB); OLED_WrCmd(0x30);   /* VCOMH 电压             */
    OLED_WrCmd(0xA4);        /* 显示内容跟随显存       */
    OLED_WrCmd(0xA6);        /* 正常显示（0xA7 为反白）*/
    OLED_WrCmd(0xAF);        /* 开启显示               */

    OLED_Clear();
    return 1U;
}

/* ==========================================================================
 * 8. 8x16 ASCII 点阵字库（ASCII 32~127，共 96 个字符）
 *
 *    每个字符 16 字节：前 8 字节是上半页的 8 列，后 8 字节是下半页的 8 列；
 *    每个字节代表一列 8 个像素，bit0 在最上面。
 * ========================================================================== */
/* ---- 8x16 ASCII 点阵字库，共 96 个字符 ---- */
/* 用等宽字体的 8x16 单元格逐字渲染生成，字形居中，无越界 */
const unsigned char F8X16[96][16] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  /* ' ' */
    {0x00, 0x00, 0x00, 0xFC, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0D, 0x00, 0x00, 0x00},  /* '!' */
    {0x00, 0x00, 0x3C, 0x3C, 0x3C, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  /* '"' */
    {0x00, 0x20, 0xFC, 0x3C, 0x20, 0xFC, 0x34, 0x20, 0x00, 0x01, 0x0F, 0x01, 0x01, 0x0F, 0x01, 0x01},  /* '#' */
    {0x00, 0x38, 0x7C, 0xCC, 0xFF, 0x8C, 0x08, 0x00, 0x00, 0x0C, 0x08, 0x08, 0x3F, 0x0D, 0x07, 0x00},  /* '$' */
    {0x18, 0x3C, 0xA4, 0xBC, 0xD8, 0x60, 0x20, 0x00, 0x00, 0x03, 0x01, 0x00, 0x0F, 0x09, 0x09, 0x07},  /* '%' */
    {0x00, 0x30, 0xFC, 0xC4, 0x84, 0x0C, 0x88, 0x00, 0x02, 0x0F, 0x08, 0x09, 0x0F, 0x06, 0x0F, 0x08},  /* '&' */
    {0x00, 0x00, 0x00, 0x3C, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  /* ''' */
    {0x00, 0x00, 0xF8, 0x1C, 0x06, 0x02, 0x02, 0x00, 0x00, 0x00, 0x07, 0x0E, 0x18, 0x10, 0x10, 0x00},  /* '(' */
    {0x00, 0x02, 0x02, 0x06, 0x0C, 0xF8, 0x00, 0x00, 0x00, 0x10, 0x10, 0x18, 0x0C, 0x07, 0x00, 0x00},  /* ')' */
    {0x00, 0x40, 0x40, 0xF0, 0xF0, 0xC0, 0x40, 0x00, 0x00, 0x00, 0x03, 0x01, 0x01, 0x03, 0x00, 0x00},  /* '*' */
    {0x00, 0x80, 0x80, 0xF0, 0xF0, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x07, 0x07, 0x00, 0x00, 0x00},  /* '+' */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0x1C, 0x00, 0x00, 0x00},  /* ',' */
    {0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  /* '-' */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x00},  /* '.' */
    {0x00, 0x00, 0x00, 0x80, 0xE0, 0x38, 0x0E, 0x00, 0x00, 0x10, 0x1E, 0x03, 0x00, 0x00, 0x00, 0x00},  /* '/' */
    {0x00, 0xF0, 0x1C, 0xC4, 0xC4, 0x0C, 0xF8, 0x00, 0x00, 0x07, 0x0E, 0x08, 0x08, 0x0C, 0x07, 0x00},  /* '0' */
    {0x00, 0x08, 0x0C, 0x0C, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x0C, 0x0F, 0x08, 0x08, 0x00},  /* '1' */
    {0x00, 0x08, 0x0C, 0x04, 0x04, 0xFC, 0x78, 0x00, 0x00, 0x0C, 0x0C, 0x0E, 0x0B, 0x09, 0x08, 0x00},  /* '2' */
    {0x00, 0x08, 0x4C, 0x44, 0x64, 0xFC, 0x98, 0x00, 0x00, 0x08, 0x08, 0x08, 0x08, 0x0C, 0x07, 0x00},  /* '3' */
    {0x00, 0xE0, 0x78, 0x00, 0x00, 0xFC, 0xFC, 0x00, 0x00, 0x03, 0x02, 0x02, 0x02, 0x0F, 0x0F, 0x02},  /* '4' */
    {0x00, 0x00, 0x7C, 0x44, 0x64, 0x44, 0xC4, 0x00, 0x00, 0x06, 0x0C, 0x08, 0x08, 0x0C, 0x07, 0x00},  /* '5' */
    {0x00, 0xE0, 0xF8, 0x48, 0x4C, 0x44, 0xC4, 0x00, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x0C, 0x07, 0x00},  /* '6' */
    {0x00, 0x1C, 0x04, 0x04, 0x04, 0xE4, 0x3C, 0x04, 0x00, 0x00, 0x00, 0x08, 0x0F, 0x01, 0x00, 0x00},  /* '7' */
    {0x00, 0xB8, 0xFC, 0x44, 0x44, 0xEC, 0xB8, 0x00, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x0C, 0x07, 0x00},  /* '8' */
    {0x00, 0xF8, 0xCC, 0x84, 0x84, 0xCC, 0xF8, 0x00, 0x00, 0x00, 0x08, 0x0C, 0x04, 0x06, 0x03, 0x00},  /* '9' */
    {0x00, 0x00, 0x00, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x00},  /* ':' */
    {0x00, 0x00, 0x00, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0x1C, 0x00, 0x00, 0x00},  /* ';' */
    {0x00, 0xC0, 0xC0, 0x60, 0x20, 0x30, 0x10, 0x00, 0x00, 0x00, 0x01, 0x01, 0x03, 0x02, 0x02, 0x00},  /* '<' */
    {0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01},  /* '=' */
    {0x00, 0x10, 0x30, 0x20, 0x60, 0xC0, 0xC0, 0x80, 0x00, 0x06, 0x02, 0x03, 0x01, 0x01, 0x00, 0x00},  /* '>' */
    {0x00, 0x08, 0x0C, 0x84, 0xC4, 0x7C, 0x38, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x0C, 0x00, 0x00, 0x00},  /* '?' */
    {0x00, 0xF0, 0x08, 0xE4, 0x34, 0x2C, 0xF8, 0x00, 0x00, 0x0F, 0x18, 0x13, 0x16, 0x12, 0x07, 0x00},  /* '@' */
    {0x00, 0x00, 0xF0, 0x1C, 0x1C, 0xF8, 0x80, 0x00, 0x08, 0x0F, 0x03, 0x02, 0x02, 0x03, 0x0F, 0x08},  /* 'A' */
    {0x00, 0xFC, 0x6C, 0x44, 0x44, 0x6C, 0xB8, 0x00, 0x00, 0x0F, 0x0C, 0x08, 0x08, 0x0C, 0x0F, 0x03},  /* 'B' */
    {0x00, 0xF0, 0x18, 0x0C, 0x04, 0x04, 0x0C, 0x00, 0x00, 0x03, 0x06, 0x0C, 0x08, 0x08, 0x08, 0x00},  /* 'C' */
    {0x00, 0xFC, 0x04, 0x04, 0x04, 0x0C, 0xF8, 0xE0, 0x00, 0x0F, 0x08, 0x08, 0x08, 0x0C, 0x07, 0x01},  /* 'D' */
    {0x00, 0xFC, 0xCC, 0x84, 0x84, 0x84, 0x04, 0x04, 0x00, 0x0F, 0x0C, 0x08, 0x08, 0x08, 0x08, 0x08},  /* 'E' */
    {0x00, 0xFC, 0xFC, 0x84, 0x84, 0x84, 0x04, 0x04, 0x00, 0x0F, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00},  /* 'F' */
    {0xC0, 0xF8, 0x18, 0x0C, 0x84, 0x84, 0x8C, 0x80, 0x00, 0x07, 0x06, 0x0C, 0x08, 0x08, 0x0F, 0x00},  /* 'G' */
    {0x00, 0xFC, 0x80, 0x80, 0x80, 0x80, 0xFC, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00},  /* 'H' */
    {0x00, 0x04, 0x04, 0xFC, 0xFC, 0x04, 0x04, 0x00, 0x00, 0x08, 0x08, 0x0F, 0x0F, 0x08, 0x08, 0x00},  /* 'I' */
    {0x00, 0x00, 0x00, 0x04, 0x04, 0x04, 0xFC, 0x00, 0x00, 0x06, 0x0C, 0x08, 0x08, 0x0C, 0x07, 0x00},  /* 'J' */
    {0x00, 0xFC, 0x80, 0x80, 0x80, 0xE0, 0x3C, 0x00, 0x00, 0x0F, 0x01, 0x01, 0x01, 0x03, 0x0E, 0x08},  /* 'K' */
    {0x00, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x0C, 0x08, 0x08, 0x08, 0x08, 0x08},  /* 'L' */
    {0x00, 0xFC, 0x1C, 0xF0, 0xE0, 0x1C, 0xFC, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x00},  /* 'M' */
    {0x00, 0xFC, 0x1C, 0x70, 0xC0, 0x00, 0xFC, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x03, 0x0E, 0x0F, 0x00},  /* 'N' */
    {0x00, 0xF8, 0x0C, 0x04, 0x04, 0x0C, 0xF8, 0xE0, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x0C, 0x07, 0x01},  /* 'O' */
    {0x00, 0xFC, 0x8C, 0x04, 0x04, 0x8C, 0xF8, 0x70, 0x00, 0x0F, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00},  /* 'P' */
    {0x00, 0xF8, 0x0C, 0x04, 0x04, 0x0C, 0xF8, 0xE0, 0x00, 0x07, 0x0C, 0x18, 0x38, 0x4C, 0x47, 0x01},  /* 'Q' */
    {0x00, 0xFC, 0x84, 0x84, 0x84, 0xCC, 0x78, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x07, 0x0E, 0x08},  /* 'R' */
    {0x00, 0x38, 0x7C, 0x44, 0xC4, 0x8C, 0x08, 0x00, 0x00, 0x0C, 0x08, 0x08, 0x08, 0x0D, 0x07, 0x00},  /* 'S' */
    {0x00, 0x04, 0x04, 0xFC, 0xFC, 0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x0F, 0x0F, 0x00, 0x00, 0x00},  /* 'T' */
    {0x00, 0xFC, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x0C, 0x07, 0x00},  /* 'U' */
    {0x04, 0x3C, 0xE0, 0x00, 0x00, 0xC0, 0x7C, 0x04, 0x00, 0x00, 0x03, 0x0E, 0x0E, 0x07, 0x00, 0x00},  /* 'V' */
    {0x1C, 0xFC, 0x00, 0xE0, 0xE0, 0x00, 0xF8, 0x7C, 0x00, 0x0F, 0x0E, 0x07, 0x01, 0x0E, 0x0F, 0x00},  /* 'W' */
    {0x00, 0x0C, 0x18, 0xE0, 0xE0, 0x38, 0x0C, 0x00, 0x00, 0x0C, 0x06, 0x01, 0x01, 0x07, 0x0C, 0x00},  /* 'X' */
    {0x04, 0x1C, 0x70, 0xC0, 0xC0, 0x70, 0x1C, 0x04, 0x00, 0x00, 0x00, 0x0F, 0x0F, 0x00, 0x00, 0x00},  /* 'Y' */
    {0x00, 0x04, 0x04, 0x84, 0xE4, 0x3C, 0x0C, 0x00, 0x00, 0x0C, 0x0F, 0x09, 0x08, 0x08, 0x08, 0x00},  /* 'Z' */
    {0x00, 0x00, 0x00, 0xFE, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x30, 0x30, 0x10, 0x00},  /* '[' */
    {0x00, 0x06, 0x3C, 0xE0, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x0E, 0x18, 0x00},  /* '\\' */
    {0x00, 0x02, 0x02, 0x02, 0xFE, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x3F, 0x1F, 0x00, 0x00},  /* ']' */
    {0x00, 0x00, 0x30, 0x0C, 0x0C, 0x38, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  /* '^' */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10},  /* '_' */
    {0x00, 0x00, 0x00, 0x03, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  /* '`' */
    {0x00, 0x00, 0x20, 0x20, 0x60, 0xC0, 0x80, 0x00, 0x00, 0x0F, 0x09, 0x09, 0x0D, 0x07, 0x0F, 0x08},  /* 'a' */
    {0x00, 0xFC, 0xFC, 0x20, 0x20, 0x60, 0xC0, 0x00, 0x00, 0x0F, 0x0F, 0x08, 0x08, 0x0C, 0x07, 0x00},  /* 'b' */
    {0x00, 0xC0, 0xC0, 0x20, 0x20, 0x20, 0x60, 0x00, 0x00, 0x07, 0x06, 0x0C, 0x08, 0x08, 0x08, 0x00},  /* 'c' */
    {0x00, 0xC0, 0x60, 0x20, 0x20, 0xE0, 0xFC, 0x00, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x06, 0x0F, 0x00},  /* 'd' */
    {0x00, 0xC0, 0x60, 0x20, 0x20, 0x60, 0xC0, 0x00, 0x00, 0x07, 0x07, 0x0D, 0x09, 0x09, 0x09, 0x00},  /* 'e' */
    {0x80, 0x80, 0x80, 0xF8, 0x8C, 0x84, 0x84, 0x04, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x00},  /* 'f' */
    {0x00, 0xC0, 0x60, 0x20, 0x20, 0xC0, 0xE0, 0x00, 0x00, 0x47, 0x4C, 0x48, 0x68, 0x3E, 0x3F, 0x00},  /* 'g' */
    {0x00, 0xFC, 0xFC, 0x20, 0x20, 0x60, 0xC0, 0x00, 0x00, 0x0F, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00},  /* 'h' */
    {0x00, 0x20, 0x20, 0x20, 0xEC, 0x00, 0x00, 0x00, 0x00, 0x08, 0x08, 0x08, 0x0F, 0x08, 0x08, 0x08},  /* 'i' */
    {0x00, 0x00, 0x20, 0x20, 0x64, 0xEC, 0x00, 0x00, 0x00, 0x40, 0x40, 0x60, 0x30, 0x3F, 0x00, 0x00},  /* 'j' */
    {0x00, 0xFC, 0xFC, 0x00, 0x80, 0xC0, 0x60, 0x00, 0x00, 0x0F, 0x0F, 0x01, 0x01, 0x07, 0x0C, 0x08},  /* 'k' */
    {0x00, 0x04, 0x04, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x00},  /* 'l' */
    {0x00, 0xE0, 0x20, 0xE0, 0xE0, 0x20, 0xE0, 0x00, 0x00, 0x0F, 0x00, 0x0F, 0x0F, 0x00, 0x0F, 0x00},  /* 'm' */
    {0x00, 0xE0, 0xE0, 0x20, 0x20, 0x60, 0xC0, 0x00, 0x00, 0x0F, 0x0F, 0x00, 0x00, 0x00, 0x0F, 0x00},  /* 'n' */
    {0x00, 0xC0, 0xE0, 0x20, 0x20, 0x60, 0xC0, 0x00, 0x00, 0x07, 0x0E, 0x08, 0x08, 0x0C, 0x07, 0x00},  /* 'o' */
    {0x00, 0xE0, 0xE0, 0x20, 0x20, 0x60, 0xC0, 0x00, 0x00, 0x7F, 0x7F, 0x08, 0x08, 0x0C, 0x07, 0x01},  /* 'p' */
    {0x00, 0xC0, 0x60, 0x20, 0x20, 0x40, 0xE0, 0x00, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x0C, 0x7F, 0x00},  /* 'q' */
    {0x00, 0x20, 0xE0, 0xC0, 0x60, 0x20, 0xE0, 0xC0, 0x00, 0x08, 0x0F, 0x0F, 0x08, 0x00, 0x00, 0x00},  /* 'r' */
    {0x00, 0xC0, 0xE0, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x08, 0x09, 0x09, 0x09, 0x0F, 0x06, 0x00},  /* 's' */
    {0x20, 0x20, 0xF8, 0xF8, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x07, 0x0F, 0x08, 0x08, 0x08, 0x00},  /* 't' */
    {0x00, 0xE0, 0x00, 0x00, 0x00, 0xE0, 0xE0, 0x00, 0x00, 0x07, 0x0C, 0x08, 0x08, 0x0F, 0x0F, 0x08},  /* 'u' */
    {0x00, 0xE0, 0xC0, 0x00, 0x00, 0x80, 0xE0, 0x00, 0x00, 0x00, 0x03, 0x0E, 0x0C, 0x07, 0x00, 0x00},  /* 'v' */
    {0x00, 0xE0, 0x00, 0xE0, 0xE0, 0x00, 0xE0, 0x60, 0x00, 0x0F, 0x0E, 0x03, 0x01, 0x0E, 0x0F, 0x00},  /* 'w' */
    {0x00, 0x20, 0x60, 0x80, 0x80, 0xE0, 0x20, 0x00, 0x00, 0x08, 0x0C, 0x03, 0x03, 0x0E, 0x08, 0x00},  /* 'x' */
    {0x00, 0xE0, 0x80, 0x00, 0x00, 0x80, 0xE0, 0x00, 0x40, 0x40, 0x63, 0x2E, 0x1C, 0x07, 0x00, 0x00},  /* 'y' */
    {0x00, 0x20, 0x20, 0x20, 0xA0, 0xE0, 0x60, 0x00, 0x00, 0x0C, 0x0E, 0x0B, 0x09, 0x08, 0x08, 0x00},  /* 'z' */
    {0x00, 0x00, 0xC0, 0xDC, 0x7E, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x1F, 0x10, 0x10, 0x00},  /* '{' */
    {0x00, 0x00, 0x00, 0xFE, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x3F, 0x00, 0x00, 0x00},  /* '|' */
    {0x00, 0x00, 0x02, 0x3E, 0xFC, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x10, 0x1F, 0x1F, 0x00, 0x00, 0x00},  /* '}' */
    {0x00, 0xC0, 0xC0, 0xC0, 0x80, 0x80, 0xC0, 0x40, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00},  /* '~' */
    {0x00, 0xFC, 0x04, 0x04, 0x04, 0x04, 0xFC, 0x00, 0x00, 0x0F, 0x08, 0x08, 0x08, 0x08, 0x0F, 0x00},  /* '' */
};

/* ==========================================================================
 * 9. 字符显示
 *
 *    在"直接画字符"之上，又加了几个"拼字符串"的小工具：
 *    先用 sprintf 的思路把数字转成文本、再整行显示。
 *    好处是每行长度固定（末尾补空格），新数据比旧数据短时不会留下残影。
 * ========================================================================== */

/* 自定义字符：度数符号 °
 * 字库里只有 ASCII 32~127，没有"度"这个符号，所以单独画一个点阵。
 * 点阵规则和字库一致：数组 16 字节 = 16 列，每字节是一列 8 个像素，bit0 在最上面。
 * 下面这个小圆环画在单元格的第 1~5 行、第 1~5 列，和数字的顶部对齐。 */
#define CH_DEGREE       0x7FU    /* 借用 ASCII 127(DEL) 当"度数符号"的代号 */

static const uint8_t GLYPH_DEGREE[16] = {
    0x00, 0x1C, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00,   /* 上半页：小圆环 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00    /* 下半页：空     */
};

/* 显示一个字符：page 0~6（要占两页，所以最大只能从第 6 页开始），col 0~120 */
static void OLED_ShowChar(uint8_t page, uint8_t col, char ch)
{
    const uint8_t *p;

    if (page > 6U || col > 120U) return;

    if ((uint8_t)ch == CH_DEGREE)
    {
        p = GLYPH_DEGREE;                   /* 度数符号走自定义点阵 */
    }
    else
    {
        if (ch < 32 || ch > 127) ch = ' ';  /* 字库外的字符统一显示空格 */
        p = &F8X16[(uint8_t)ch - 32U][0];
    }

    /* 上半页 */
    OLED_SetPos(page, col);
    I2C_Start();
    I2C_SendByte(g_oled_addr);  I2C_WaitAck();
    I2C_SendByte(0x40);         I2C_WaitAck();
    for (uint8_t i = 0; i < 8U; i++) { I2C_SendByte(p[i]); I2C_WaitAck(); }
    I2C_Stop();

    /* 下半页 */
    OLED_SetPos(page + 1U, col);
    I2C_Start();
    I2C_SendByte(g_oled_addr);  I2C_WaitAck();
    I2C_SendByte(0x40);         I2C_WaitAck();
    for (uint8_t i = 8U; i < 16U; i++) { I2C_SendByte(p[i]); I2C_WaitAck(); }
    I2C_Stop();
}

/* 显示字符串：一行 128 像素，8x16 字符每字占 8 列，所以一行最多 16 个字符 */
static void OLED_ShowStr(uint8_t page, uint8_t col, const char *str)
{
    while (*str != '\0')
    {
        OLED_ShowChar(page, col, *str);
        str++;
        col += 8U;
        if (col > 120U) break;
    }
}

/* ---- 下面四个是"拼字符串"的小工具：都返回新的"末尾下标"，方便连续调用 ----
 * 用法：
 *      uint8_t n = 0U;
 *      n = StrAppendStr(line, n, "Temp: ");
 *      n = StrAppendFix1(line, n, g_temp_avg);
 *      n = StrPadTo(line, n, 16U);
 *      OLED_ShowStr(0, 0, line);
 * 为什么不用 sprintf？标准库会把一大坨格式化代码和浮点支持链接进来，
 * 单片机 Flash 只有 64KB，能省就省，自己写几行反而更快更小。
 */

/* 追加一个字符串 */
static uint8_t StrAppendStr(char *buf, uint8_t n, const char *s)
{
    while (*s != '\0')
    {
        buf[n] = *s;
        n++;
        s++;
    }
    buf[n] = '\0';
    return n;
}

/* 追加一个无符号整数（自己取余拆位） */
static uint8_t StrAppendU32(char *buf, uint8_t n, uint32_t v)
{
    char    tmp[11];
    uint8_t k = 0U;

    if (v == 0U)
    {
        buf[n] = '0';
        n++;
        buf[n] = '\0';
        return n;
    }
    while ((v > 0U) && (k < 10U))     /* 末位先进数组，所以是倒序 */
    {
        tmp[k] = (char)('0' + (v % 10U));
        v /= 10U;
        k++;
    }
    while (k > 0U)                    /* 倒着取出来就是正序 */
    {
        k--;
        buf[n] = tmp[k];
        n++;
    }
    buf[n] = '\0';
    return n;
}

/* 追加"带一位小数的数值"。v10 是放大 10 倍后的整数：-105 显示成 "-10.5" */
static uint8_t StrAppendFix1(char *buf, uint8_t n, int16_t v10)
{
    uint16_t a;

    if (v10 < 0) { buf[n] = '-'; n++; a = (uint16_t)(-v10); }
    else         { a = (uint16_t)v10; }

    n = StrAppendU32(buf, n, (uint32_t)(a / 10U));   /* 整数部分 */
    buf[n] = '.';
    n++;
    buf[n] = (char)('0' + (a % 10U));                /* 小数部分 */
    n++;
    buf[n] = '\0';
    return n;
}

/* 末尾补空格到指定长度。作用：同一行新旧内容长度不一样时，
 * 短的那一次会把上一次多出来的字一起擦掉，屏幕上不留残影 */
static uint8_t StrPadTo(char *buf, uint8_t n, uint8_t width)
{
    while (n < width)
    {
        buf[n] = ' ';
        n++;
    }
    buf[n] = '\0';
    return n;
}

/* 追加一个两位数字，不足两位时前面补 '0'（显示 "10:05:03" 这种时间要用）
 * 不做这个补零的话，9 点 5 分 3 秒会显示成 "9:5:3"，看上去不像时间 */
static uint8_t StrAppendPad2(char *buf, uint8_t n, uint32_t v)
{
    if (v > 99U) v = 99U;
    buf[n] = (char)('0' + (v / 10U));  n++;
    buf[n] = (char)('0' + (v % 10U));  n++;
    buf[n] = '\0';
    return n;
}

/* ==========================================================================
 * 10. DHT11 温湿度传感器驱动（单总线协议，DATA 接 PB0）
 *
 *    【协议流程】
 *      ① 主机发起始信号：把线拉低至少 18ms，然后释放（松手）
 *      ② DHT11 应答：先拉低 80us，再拉高 80us
 *      ③ DHT11 连续发 40 位，每一位的格式都是：
 *             50us 低电平（分隔位） + 高电平
 *             高电平持续 26~28us 表示数据 0；持续 70us 表示数据 1
 *         → 注意：这是用"时间的长度"来编码，不是用"电平的高低"，
 *           所以单总线对时序精度要求很高，读的时候不能被中断打断
 *      ④ 40 位 = 5 个字节：
 *             [0] 湿度整数  [1] 湿度小数  [2] 温度整数  [3] 温度小数  [4] 校验和
 *         校验和 = 前 4 个字节相加，取最低 8 位
 *
 *    【怎么区分 0 和 1】
 *      等"50us 低电平"结束之后，再延时 40us 然后采样：
 *          还是高电平 -> 高电平已经持续 40us 以上 -> 只可能是 70us 的 1
 *          已经是低电平 -> 高电平不到 40us 就结束了 -> 只可能是 28us 的 0
 *      40us 这个取样点正好落在 28us 和 70us 中间，两边余量都很大，很稳。
 *
 *    【为什么引脚模式要来回切】
 *      输出模式   ：STM32 主动把线拉低（发起始信号时必须）
 *      输入上拉模式：松手不驱动，只"看"这根线；同时打开内部上拉，
 *                    这样即使模块没焊上拉电阻，线也能自己回到高电平
 *      注意 STM32F1 的内部上拉只在输入模式下才有，输出模式没有。
 * ========================================================================== */
#define DHT_READ()      ((GPIOB_IDR >> 0) & 0x01U)    /* 读 PB0 的实际电平 */

/* PB0 -> 推挽输出 50MHz（用来主动拉低总线）
 *   通用推挽 50MHz：CNF=00, MODE=11 -> 00<<2|11 = 0011b = 0x3 */
static void DHT_PinOut(void)
{
    GPIOB_ODR &= ~(1U << 0);                             /* 先定好输出电平：低 */
    GPIOB_CRL  = (GPIOB_CRL & ~0x0FU) | 0x03U;
}

/* PB0 -> 上拉输入（松手不驱动，同时开内部上拉，并可供读电平）
 *   上拉输入：CNF=10, MODE=00 -> 10<<2|00 = 1000b = 0x8 */
static void DHT_PinIn(void)
{
    GPIOB_ODR |=  (1U << 0);                             /* ODR=1 表示输入时启用上拉 */
    GPIOB_CRL  = (GPIOB_CRL & ~0x0FU) | 0x08U;
}

/* 等 PB0 变成 want 这个电平。返回 1 = 等到了，返回 0 = 超时
 * 超时保护是必须的：假如传感器没接、线断了，没有超时的话程序会永远
 * 卡在这个 while 里，整块板子看起来就像死机（LED 也不闪了）。 */
static uint8_t DHT_WaitLevel(uint8_t want)
{
    /* 超时上限：一轮循环约 8 个内核时钟（64MHz 下约 0.125us），
     * 0x0008_0000 轮 ≈ 1.05 秒。正常时序里任何电平都不会超过 80us，
     * 所以这个上限对正常工作毫无影响；但一旦传感器掉线，主循环最多
     * 卡 1 秒就会自己出来并报告 err，而不是永久卡死。
     *
     * 【这里踩过的坑】原值写 30000：64MHz 下只有约 3.7ms，对 DHT11
     * 完全没有余量（"等应答"那几步传感器响应稍慢就会误报 err=1）。
     * 更隐蔽的是它和主机的 20ms 起始低电平配合出了一个问题 —— 细节
     * 见 5.3 节开头关于"主机拉低期间串口为什么收到连续 0x00"的说明。 */
    uint32_t t = 0x00080000U;

    while (DHT_READ() != want)
    {
        if (--t == 0U) return 0U;
    }
    return 1U;
}

/* 诊断信息：既给串口打印用，也给 OLED 显示用 */
static uint8_t  g_dht_raw[5];      /* 最近一次读到的 5 个原始字节 */
static uint8_t  g_dht_err;         /* 0 = 成功；非 0 = 失败发生在第几步 */
static uint32_t g_dht_ok_cnt;      /* 累计成功次数 */
static uint32_t g_dht_fail_cnt;    /* 累计失败次数 */

/* 读一次 DHT11。返回 1 = 成功（校验和也正确），返回 0 = 失败
 *   g_dht_err 会记录失败在哪一步，方便定位：
 *      1 = 等不到 DHT11 拉低（没应答，多半是接线问题）
 *      2 = 等不到它把线放开
 *      3 = 等不到应答高电平结束
 *      4 = 读数据位时等不到高电平（时序被破坏）
 *      5 = 读数据位时等不到低电平
 *      6 = 数据读全了，但校验和不匹配（数据不可信） */
static uint8_t DHT11_ReadRaw(void)
{
    uint8_t dat[5];
    uint8_t i, j;

    /* ① 主机发起始信号：拉低 20ms（手册要求 >18ms），然后释放 */
    DHT_PinOut();
    delay_ms(20U);
    DHT_PinIn();                                            /* 释放，由上拉电阻拉回高电平 */

    /* ② 等 DHT11 的应答：80us 低 + 80us 高 */
    if (!DHT_WaitLevel(0U)) { g_dht_err = 1U; return 0U; }   /* 等它把线拉低   */
    if (!DHT_WaitLevel(1U)) { g_dht_err = 2U; return 0U; }   /* 等 80us 低结束 */
    if (!DHT_WaitLevel(0U)) { g_dht_err = 3U; return 0U; }   /* 等 80us 高结束 -> 第 1 位开始 */

    /* ③ 连续读 40 位 = 5 个字节，高位在前 */
    for (i = 0U; i < 5U; i++)
    {
        dat[i] = 0U;
        for (j = 0U; j < 8U; j++)
        {
            if (!DHT_WaitLevel(1U)) { g_dht_err = 4U; return 0U; }   /* 等这位的 50us 低结束 */

            delay_us(40U);                                           /* 关键判据：延时 40us 再采样 */
            dat[i] <<= 1;
            if (DHT_READ() != 0U) dat[i] |= 1U;                      /* 还是高 -> 1，已经变低 -> 0 */

            if (!DHT_WaitLevel(0U)) { g_dht_err = 5U; return 0U; }   /* 等高电平结束，进入下一位 */
        }
    }

    /* ④ 校验和：前 4 字节之和的最低 8 位应等于第 5 字节 */
    if ((uint8_t)(dat[0] + dat[1] + dat[2] + dat[3]) != dat[4])
    {
        for (i = 0U; i < 5U; i++) g_dht_raw[i] = dat[i];   /* 出错也要把原始数据留下来当证据 */
        g_dht_err = 6U;
        return 0U;
    }

    for (i = 0U; i < 5U; i++) g_dht_raw[i] = dat[i];
    g_dht_err = 0U;
    return 1U;
}

/* 对外暴露的读函数：在真正读时序的外面套一层"关中断"保护。
 *
 * 为什么必须关中断？DHT11 是用"高电平持续 28us 还是 70us"来编码 0 和 1 的，
 * 判据是"延时 40us 后采样"。如果正好在这 40us 里来了 TIM2 中断（每 1ms 一次），
 * 中断服务程序执行要花几个微秒，采样点就被推迟了 —— 极端情况下会把 1 读成 0。
 * 一次读取总共约 24ms，关掉中断的代价是时基少计约 24ms（相对 2 秒周期只有 1.2%，
 * 对"显示运行时间"这种用途完全够用），换来的是时序绝对干净，很划算。 */
static uint8_t DHT11_Read(void)
{
    uint8_t r;

    Irq_Disable();
    r = DHT11_ReadRaw();
    Irq_Enable();

    return r;
}

/* ==========================================================================
 * 11. W25Q64 SPI Flash 驱动（历史记录用，掉电不丢）
 *
 *    【为什么用 SPI 而不是 I2C？】
 *      SPI 没有"从机地址"这一套，靠一根独立的片选线（CS）点名，
 *      协议开销小、速度快（本项目跑 4.5MHz，是 I2C 的几十倍）。
 *      代价是线多：SCK / MOSI / MISO / CS 共 4 根。
 *      口诀：SPI 是"四线全双工"，I2C 是"两线半双工"。
 *
 *    【SPI 的四种模式（POL/PHA）】
 *      由 CR1 里的 CPOL（时钟极性）和 CPHA（时钟相位）组合出 4 种模式。
 *      W25Q64 要求「模式 0」：CPOL=0（空闲时 SCK 为低）、
 *                              CPHA=0（第一个时钟沿就采样数据）。
 *      模式不匹配的症状很有特点：读 ID 得到 0x000000 或 0xFFFFFF，
 *      因为数据在错误的边沿被采样，全都错位了。
 *
 *    【Flash 的三条铁律】
 *      ① 读：随便读，什么时候都能读，没有限制。
 *      ② 写：只能把位从 1 改成 0，不能从 0 改回 1。
 *         所以写之前必须先擦（擦完全是 0xFF，即全 1），才能往里写。
 *      ③ 擦：最小单位是「扇区」(4KB)，不能只擦一个字节。
 *         擦除后整个扇区变成 0xFF。
 *      这三条决定了"不能像写文件一样随便改一个字节"，
 *      必须用「双扇区乒乓 + 顺序追加」的方式来组织数据。
 *
 *    【关键时序（数据手册上的 t 参数）】
 *      擦除一个扇区：典型 45ms，最大 400ms —— 所以擦完必须"查忙"等它完
 *      页编程(256B)：典型 0.7ms，最大 3ms
 *      查忙：读状态寄存器 SR1 的 bit0（BUSY），为 0 表示空闲
 *
 *    【状态寄存器 SR1 的位（只看这几个）】
 *      bit0 BUSY ：1 = 正在擦/写，此时除了读状态寄存器，别的命令都不响应
 *      bit1 WEL  ：写使能锁存，每次擦/写之前都必须先发 0x06 把它置 1
 *                  （硬件出于安全考虑设计的：防止误擦写）
 *      bit2 BP0~BP2：块保护位，本项目不做保护，保持默认 0
 * ========================================================================== */

/* ---- 引脚宏：CS 用 PA4，SPI 三个脚由片上外设接管 ---- */
#define W25_CS_LOW()    (GPIOA_BSRR = (1U << (4U + 16U)))   /* BSRR 高 16 位写 1 = 清除该位 */
#define W25_CS_HIGH()   (GPIOA_BSRR = (1U << 4U))           /* BSRR 低 16 位写 1 = 置位该位 */

/* ---- W25Q64 指令集（只列本项目用到的） ---- */
#define W25_CMD_WRITE_EN        0x06U   /* 写使能：擦/写之前必须发 */
#define W25_CMD_READ_SR1        0x05U   /* 读状态寄存器 1 */
#define W25_CMD_READ_DATA       0x03U   /* 读数据（最常用） */
#define W25_CMD_PAGE_PROGRAM    0x02U   /* 页编程（256 字节一页） */
#define W25_CMD_SECTOR_ERASE    0x20U   /* 扇区擦除（4KB） */
#define W25_CMD_JEDEC_ID        0x9FU   /* 读 JEDEC ID：厂商号 + 容量号 */
#define W25_CMD_READ_STATUS2    0x35U   /* 读状态寄存器 2 的 QE 位 */

/* W25Q64 的正确 ID 是 EF 40 17：
 *   0xEF = 华邦(Winbond)   0x40 = SPI Flash 系列   0x17 = 8MB(64Mbit) */
#define W25_ID_MANU             0xEFU

/* ---- SPI1 初始化 ---- */
static uint8_t g_spi_ok;            /* 1 = W25Q64 识别成功 */

static void SPI1_Init(void)
{
    RCC_APB2ENR |= (1U << 2) | (1U << 12);   /* 使能 GPIOA、SPI1 时钟 */

    /* PA5(SCK) / PA7(MOSI)：复用推挽输出 50MHz
     *   复用推挽：CNF=10, MODE=11 -> 10<<2|11 = 1011b = 0xB
     *   这两个脚由 SPI 外设自己驱动，所以必须配成"复用"而不是"通用输出"——
     *   配成通用输出的症状是：软件怎么写 ODR 都没用，时钟线一动不动。
     *   nibble 布局：引脚 5 在第 20~23 位，引脚 7 在第 28~31 位 */
    GPIOA_CRL &= ~(0x0FU << 20);
    GPIOA_CRL |=  (0x0BU << 20);
    GPIOA_CRL &= ~(0x0FU << 28);
    GPIOA_CRL |=  (0x0BU << 28);

    /* PA6(MISO)：浮空输入
     *   浮空输入：CNF=01, MODE=00 -> 01<<2|00 = 0100b = 0x4
     *   为什么不配上拉？因为 Flash 的 DO 脚是主动驱动（推挽），
     *   不需要外部上拉来"扶"住电平；悬空时读到的噪声也不会误判成有效数据。
     *   引脚 6 在第 24~27 位 */
    GPIOA_CRL &= ~(0x0FU << 24);
    GPIOA_CRL |=  (0x04U << 24);

    /* PA4(CS)：通用推挽输出 50MHz
     *   通用推挽：CNF=00, MODE=11 -> 00<<2|11 = 0011b = 0x3
     *   注意这里是"通用"不是"复用"——CS 由软件手动控制，
     *   因为一条 SPI 总线上可以挂多个从机，各自用一根 CS 点名，
     *   硬件 NSS 只有一个，不够用。
     *   引脚 4 在第 16~19 位 */
    GPIOA_CRL &= ~(0x0FU << 16);
    GPIOA_CRL |=  (0x03U << 16);

    W25_CS_HIGH();                     /* 空闲时片选拉高（不选中任何器件） */

    /* SPI1 控制寄存器 1 配置：
     *   BR[2:0] = 011 -> 分频 16，PCLK2=64MHz / 16 = 4MHz
     *   （W25Q64 最高支持 80MHz，4MHz 属于很保守的速度，稳定优先）
     *   MSTR=1 主模式   CPOL=0 CPHA=0 模式0   DFF=0 8 位   SSM+SSI 软件管理 NSS
     *   SPE=1 最后使能（要先配好其他位再打开，否则配置期间会误发时钟） */
    SPI1_CR1 = (0x03U << 3)    /* BR   = 011 -> 4MHz  */
             | (1U    << 2)    /* MSTR = 1   -> 主机  */
             | (1U    << 8)    /* SSI  = 1   -> 内部 NSS 拉高（主机才不会被当成从机） */
             | (1U    << 9)    /* SSM  = 1   -> NSS 软件管理，忽略 PA4 的硬件 NSS 功能 */
             | (1U    << 6);   /* SPE  = 1   -> 使能 SPI */
}

/* 收发一个字节（SPI 是全双工：发的同时也在收，所以叫"交换"）
 * 流程：等 TXE（发送缓冲区空）-> 写入要发的数据 -> 等 RXNE（收到了）-> 读出来 */
static uint8_t SPI1_SwapByte(uint8_t dat)
{
    while ((SPI1_SR & (1U << 1)) == 0U);   /* TXE = 1 表示可以写了 */
    SPI1_DR = (uint32_t)dat;               /* 写 DR 就自动开始发 8 个时钟 */

    while ((SPI1_SR & (1U << 0)) == 0U);   /* RXNE = 1 表示收到了一个字节 */
    return (uint8_t)SPI1_DR;               /* 读 DR 会把 RXNE 自动清掉 */
}

/* 等 Flash 把内部擦/写动作做完（查忙）。
 * 为什么必须查忙？擦一个扇区要 45~400ms，这期间芯片完全不响应别的命令。
 * 如果不查忙就直接读数据，读回来的是垃圾——这是新手最常踩的坑。
 * 超时保护 5 亿次循环（实际约 1 秒多），防止芯片坏掉时死等。 */
static uint8_t W25_WaitBusy(void)
{
    uint32_t t = 0x20000000U;

    W25_CS_LOW();
    SPI1_SwapByte(W25_CMD_READ_SR1);
    while (1)
    {
        uint8_t sr = SPI1_SwapByte(0xFFU);

        if ((sr & 0x01U) == 0U) break;      /* BUSY=0 -> 空闲了 */
        if (--t == 0U) { W25_CS_HIGH(); return 0U; }   /* 超时 */
    }
    W25_CS_HIGH();
    return 1U;
}

/* 发写使能（WEL）。每次擦除/写入之前都必须调用一次，
 * 因为硬件规定：擦/写命令执行完或执行失败后，WEL 会自动清零。
 * 漏掉这一步的症状：命令发出去了，但 Flash 毫无反应（数据没变）。 */
static void W25_WriteEnable(void)
{
    W25_CS_LOW();
    SPI1_SwapByte(W25_CMD_WRITE_EN);
    W25_CS_HIGH();
}

/* 读 JEDEC ID，用来确认"芯片到底在不在、型号对不对"。
 * 返回 32 位：高 8 位无效，中间三字节是 厂商号 / 类型 / 容量。 */
static uint32_t W25_ReadID(void)
{
    uint32_t id = 0U;

    W25_CS_LOW();
    SPI1_SwapByte(W25_CMD_JEDEC_ID);
    id  = (uint32_t)SPI1_SwapByte(0xFFU) << 16;   /* 厂商号 */
    id |= (uint32_t)SPI1_SwapByte(0xFFU) << 8;    /* 类型   */
    id |= (uint32_t)SPI1_SwapByte(0xFFU);         /* 容量   */
    W25_CS_HIGH();

    return id;
}

/* 读任意长度数据。只有读操作不需要先擦、也不需要写使能。
 * 发完 0x03 + 24 位地址后，Flash 会自动从该地址开始连续吐数据，
 * 地址到末尾会自动回卷到 0，所以可以一直读下去。 */
static void W25_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    W25_CS_LOW();
    SPI1_SwapByte(W25_CMD_READ_DATA);
    SPI1_SwapByte((uint8_t)((addr >> 16) & 0xFFU));   /* 地址是 24 位，高位在前 */
    SPI1_SwapByte((uint8_t)((addr >> 8)  & 0xFFU));
    SPI1_SwapByte((uint8_t)( addr        & 0xFFU));

    while (len-- > 0U)
    {
        *buf = SPI1_SwapByte(0xFFU);    /* 发 0xFF 只是为了让时钟跑起来，发什么无所谓 */
        buf++;
    }
    W25_CS_HIGH();
}

/* 页编程：往指定地址写 1~256 字节。
 * 【硬约束】一次写入不能跨过 256 字节的页边界！
 *   跨页时多余的数据会被"卷回"到本页开头，覆盖掉本页已有的内容 ——
 *   这是静默的数据损坏，不会报错，只会让记录莫名其妙变错。
 *
 * 【本项目确实会踩到，不是纸上谈兵】
 *   记录起始地址 = 4 + 3k（k 是第几条）。要跨页，需要 (4+3k) mod 256 == 254 或 255，
 *   因为每条只有 3 字节，落在这两个余数上就刚好被页边界切开。
 *   由于 3 和 256 互质，这个余数序列会遍历所有值，所以**一定会周期性地撞上**：
 *   实测第 169、254、425、510、681、766、937、1022、1193、1278 条都在边界上
 *   （每隔 85 条出现一次，一个扇区里出现 10 次）。
 *   所以下面的自动拆分逻辑是必需的，不是多余的保险。
 *
 * 拆分方法：每次只写到"本页剩余空间"为止（room = 256 - 地址低8位），
 * 写完一段再发起一次新的 PageProgram，直到写完。 */
static void W25_PageProgram(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    while (len > 0U)
    {
        /* 本页还能写几个字节：256 - (地址的低 8 位) */
        uint32_t room  = 256U - (addr & 0xFFU);
        uint32_t chunk = (len < room) ? len : room;

        W25_WriteEnable();
        W25_CS_LOW();
        SPI1_SwapByte(W25_CMD_PAGE_PROGRAM);
        SPI1_SwapByte((uint8_t)((addr >> 16) & 0xFFU));
        SPI1_SwapByte((uint8_t)((addr >> 8)  & 0xFFU));
        SPI1_SwapByte((uint8_t)( addr        & 0xFFU));
        for (uint32_t i = 0U; i < chunk; i++)
        {
            SPI1_SwapByte(buf[i]);
        }
        W25_CS_HIGH();
        W25_WaitBusy();                  /* 等这次写完成，才能发起下一次 */

        addr += chunk;
        buf  += chunk;
        len  -= chunk;
    }
}

/* 扇区擦除（4KB）。擦完之后这个扇区的每一个字节都变成 0xFF。
 * 这是写新数据的前置动作：只有 0xFF 才能被写成任意值。 */
static void W25_SectorErase(uint32_t addr)
{
    W25_WriteEnable();                   /* ① 先发写使能 */
    W25_CS_LOW();
    SPI1_SwapByte(W25_CMD_SECTOR_ERASE);
    /* 擦除命令只认地址的高位，低 12 位被忽略 —— 也就是说，
     * 给 0x0000_1234 和给 0x0000_1000 擦的是同一个扇区。
     * 这里主动把低位清掉，让调用者一眼看出擦的是哪个扇区。 */
    addr &= 0xFFFFF000U;
    SPI1_SwapByte((uint8_t)((addr >> 16) & 0xFFU));
    SPI1_SwapByte((uint8_t)((addr >> 8)  & 0xFFU));
    SPI1_SwapByte((uint8_t)( addr        & 0xFFU));
    W25_CS_HIGH();
    W25_WaitBusy();                      /* ② 等擦完（最长 400ms） */
}

/* ==========================================================================
 * 12. 历史记录存储管理（双扇区乒乓 + 顺序追加）
 *
 *    【为什么不能简单地在文件末尾追加？】
 *      Flash 一个扇区只有 4KB，写满之后必须擦掉才能继续写；
 *      但擦除是"整扇区"动作，一擦就把这个扇区里已有的记录全毁了。
 *      如果只有 1 个扇区，就必须"读出全部 → 擦除 → 写回"，非常繁琐而且
 *      中途掉电就全丢了。
 *
 *    【双扇区乒乓怎么工作】
 *      设两个扇区 A(0x000000) 和 B(0x001000)，格式完全一样：
 *
 *          扇区头部 4 字节：[0]=魔数 0xA5  [1]=状态(0x01有效)  [2..3]=起始记录号
 *          之后紧跟记录，每条 3 字节：温度高字节、温度低字节、湿度字节
 *
 *      写入逻辑（g_wr_sector 指向当前正在写的扇区）：
 *          如果这个扇区还没写满 → 直接在后面追加一条
 *          如果写满了             → 切到另一个扇区：擦掉它，写入新的文件头，
 *                                  把记录号接上（不重头开始），从它开头继续写
 *      这样任意时刻总有"一个扇区是完整的旧数据，另一个在接新数据"，
 *      掉电最多丢失当前这一条，历史不会整体崩掉。
 *
 *    【记录号是干什么的】
 *      乒乓切换后，两个扇区的物理顺序和逻辑时间顺序不一致了。
 *      所以每条记录带一个自增序号，读的时候按序号排序输出，
 *      就能还原出真实的先后顺序。
 *
 *    【已知边界：序号回绕】
 *      序号存在扇区头的 2 个字节里（uint16，最大 65535）。
 *      每扇区 1364 条，写满约 48 次乒乓（约 65000 条 ≈ 1090 小时）后序号会绕回 0，
 *      那一刻 a.seq >= b.seq 的判断会误判新旧，导致排序颠倒一轮。
 *      对本项目（演示 + 短时运行）不影响；若要做产品级，
 *      应把序号扩到 4 字节，或在 init 时做归一化（把两个 seq 都减去较小值）。
 *
 *    【为什么记录只存温湿度、不存时间？】
 *      因为没有 RTC，开机时间归零，存了也没有绝对意义。
 *      这里的"历史"表达的是"温度和湿度随时间的变化趋势"，
 *      序号 + 相对时间（序号 × 1 分钟）已经足够表达这个趋势了。
 * ========================================================================== */

#define W25_SECTOR_A        0x000000UL   /* 记录区 A 的扇区起始地址 */
#define W25_SECTOR_B        0x001000UL   /* 记录区 B 的扇区起始地址 */
#define W25_REC_HDR_SIZE    4UL          /* 扇区头：魔数+状态+起始序号(2B) */
#define W25_REC_SIZE        3UL          /* 每条记录 3 字节 */
#define W25_REC_AREA        (4096UL - W25_REC_HDR_SIZE)          /* 可写数据字节数 4092 */
#define W25_REC_PER_SECTOR  (W25_REC_AREA / W25_REC_SIZE)        /* 每扇区记录条数 1364 */
#define W25_REC_MAGIC       0xA5U        /* 魔数：用来判断这个扇区有没有被初始化过 */
#define W25_REC_VALID       0x01U        /* 状态字节：1 = 本扇区数据有效的 */
#define W25_VERIFY_EVERY    100U         /* 每写这么多条，就实地重扫一遍 Flash 对账 */

/* 头 4 字节的内存结构（不直接往 Flash 写结构体，避免编译器对齐/字节序问题） */
typedef struct
{
    uint16_t seq;        /* 本扇区第一条记录的序号 */
    uint8_t  start;      /* 数据区在本扇区的字节偏移（固定 = 4） */
    uint8_t  count;      /* 本扇区已写了几条记录 */
} W25_RecInfo;

static W25_RecInfo g_recA;              /* 扇区 A 的解析结果 */
static W25_RecInfo g_recB;              /* 扇区 B 的解析结果 */
static uint16_t    g_cntA;              /* 扇区 A 的实际记录条数（精确计数，缓存） */
static uint16_t    g_cntB;              /* 扇区 B 的实际记录条数（精确计数，缓存） */
static uint8_t     g_wr_sector;         /* 当前正在写的扇区：0 = A，1 = B */
static uint16_t    g_wr_count;          /* 当前扇区已写条数 */
static uint16_t    g_wr_seq;            /* 下一条记录的序号 */
static uint32_t    g_rec_total;         /* 两个扇区加起来的总记录条数 */
static uint8_t     g_rec_ok;            /* 1 = 记录系统可用（W25Q64 在线） */

/* 精确数一个扇区里有多少条记录。
 * 判据：连续读到 3 个 0xFF 就认为到末尾了。
 * 为什么可以用 0xFF 当"空位"标志？因为擦除后 Flash 全是 0xFF，
 * 而一条真实记录不可能是 0xFFFF（温度 = 3276.7°C，物理上不存在）。
 *
 * 【性能提示】这个函数要逐条读 1364 次，是个不便宜的操作（约 19ms）。
 * 所以只在"初始化"和"写完一条"时调用，结果缓存到 g_cntA/g_cntB，
 * 绝不在读历史画面时每行都调一次 —— 那样翻一页要 150ms，太慢。 */
static uint16_t W25_CountSector(uint32_t sector_addr)
{
    uint16_t n = 0U;
    uint8_t  raw[W25_REC_SIZE];

    for (uint16_t i = 0U; i < W25_REC_PER_SECTOR; i++)
    {
        W25_Read(sector_addr + W25_REC_HDR_SIZE + (uint32_t)i * W25_REC_SIZE,
                 raw, W25_REC_SIZE);
        if ((raw[0] == 0xFFU) && (raw[1] == 0xFFU) && (raw[2] == 0xFFU)) break;
        n++;
    }
    return n;
}

/* 重新统计两个扇区的条数并刷新缓存（慢，约 40ms）。
 *
 * 正常写入流程走的是 W25_RecAppend 里的"增量 +1"，不需要调这个。
 * 它的用途是**校验**：把"增量算出来的条数"和"实地扫一遍数出来的条数"
 * 对比一下，一致说明缓存没被写乱。
 * 每写 100 条校验一次，代价约 40ms/100 分钟，可以忽略，
 * 但能在开发期尽早发现寻址或乒乓逻辑的问题。 */
static void W25_RefreshCounts(void)
{
    g_cntA = W25_CountSector(W25_SECTOR_A);
    g_cntB = W25_CountSector(W25_SECTOR_B);
    g_rec_total = (uint32_t)g_cntA + (uint32_t)g_cntB;
}

/* 读一个扇区的头，解析出里面已有的记录信息。
 * 返回 1 = 这个扇区是有效的（有魔数、有数据），返回 0 = 空白或无效。 */
static uint8_t W25_LoadSector(uint32_t sector_addr, W25_RecInfo *info)
{
    uint8_t hdr[W25_REC_HDR_SIZE];

    info->seq   = 0U;
    info->start = (uint8_t)W25_REC_HDR_SIZE;
    info->count = 0U;

    W25_Read(sector_addr, hdr, W25_REC_HDR_SIZE);

    if (hdr[0] != W25_REC_MAGIC)  return 0U;    /* 没有魔数 -> 从没写过 */
    if (hdr[1] != W25_REC_VALID)  return 0U;    /* 状态不对 -> 视为无效 */

    info->seq   = (uint16_t)(((uint16_t)hdr[2] << 8) | (uint16_t)hdr[3]);
    info->start = (uint8_t)W25_REC_HDR_SIZE;

    return 1U;
}

/* 初始化记录系统：读两个扇区的头，决定继续往哪个扇区写。
 *
 *   决策规则：
 *     · 两个扇区都没写过       -> 格式化 A，从 A 开始写
 *     · 只有 A 有效            -> 如果 A 没满就接着写 A，满了就切到 B
 *     · 只有 B 有效            -> 同理
 *     · 两个都有效             -> 选"序号更大"的那个作为当前写入区；
 *                                 如果它也满了，就切到另一个（擦掉重写，序号接上）
 */
static void W25_RecInit(void)
{
    uint8_t  okA, okB;
    uint16_t realA, realB;

    g_rec_ok    = 0U;
    g_wr_seq    = 0U;
    g_wr_count  = 0U;
    g_rec_total = 0U;
    g_cntA      = 0U;
    g_cntB      = 0U;

    if (!g_spi_ok) return;

    okA = W25_LoadSector(W25_SECTOR_A, &g_recA);
    okB = W25_LoadSector(W25_SECTOR_B, &g_recB);

    /* 精确统计条数。无效的扇区一律按 0 条算（即使里面恰好有残留数据） */
    realA = okA ? W25_CountSector(W25_SECTOR_A) : 0U;
    realB = okB ? W25_CountSector(W25_SECTOR_B) : 0U;

    g_cntA      = realA;
    g_cntB      = realB;
    g_rec_total = (uint32_t)realA + (uint32_t)realB;

    if (!okA && !okB)
    {
        /* 全新芯片：格式化 A，序号从 0 开始 */
        uint8_t hdr[W25_REC_HDR_SIZE];
        W25_SectorErase(W25_SECTOR_A);
        hdr[0] = W25_REC_MAGIC;
        hdr[1] = W25_REC_VALID;
        hdr[2] = 0U;
        hdr[3] = 0U;
        W25_PageProgram(W25_SECTOR_A, hdr, W25_REC_HDR_SIZE);
        g_wr_sector = 0U;
        g_wr_count  = 0U;
        g_wr_seq    = 0U;
    }
    else if (okA && !okB)
    {
        g_wr_sector = 0U;
        g_wr_count  = realA;
        g_wr_seq    = (uint16_t)(g_recA.seq + realA);   /* 序号接着老数据往上走 */
    }
    else if (!okA && okB)
    {
        g_wr_sector = 1U;
        g_wr_count  = realB;
        g_wr_seq    = (uint16_t)(g_recB.seq + realB);
    }
    else
    {
        /* 两个都有效：接在"序号更大的那个"后面继续写，
         * 因为序号大 = 那一轮更晚开始 = 它的数据更新。 */
        if (g_recA.seq >= g_recB.seq)
        {
            g_wr_sector = 0U;
            g_wr_count  = realA;
            g_wr_seq    = (uint16_t)(g_recA.seq + realA);
        }
        else
        {
            g_wr_sector = 1U;
            g_wr_count  = realB;
            g_wr_seq    = (uint16_t)(g_recB.seq + realB);
        }
    }

    g_rec_ok = 1U;
}

/* 追加一条记录。参数是放大 10 倍的温湿度（和显示用的是同一份数据）。
 * 返回 1 = 写成功，返回 0 = 没写成（芯片不在，或者查忙超时）。 */
static uint8_t W25_RecAppend(int16_t temp_x10, int16_t humi_x10)
{
    uint8_t  rec[W25_REC_SIZE];
    uint32_t sector = (g_wr_sector == 0U) ? W25_SECTOR_A : W25_SECTOR_B;
    uint32_t addr;

    if (!g_rec_ok) return 0U;

    /* ① 判断当前扇区是不是写满了 */
    if (g_wr_count >= W25_REC_PER_SECTOR)
    {
        uint8_t  hdr[W25_REC_HDR_SIZE];
        uint8_t  next = (g_wr_sector == 0U) ? 1U : 0U;      /* 乒乓：换到另一个扇区 */
        uint32_t nsec = (next == 0U) ? W25_SECTOR_A : W25_SECTOR_B;

        W25_SectorErase(nsec);                              /* 擦掉旧数据 */
        hdr[0] = W25_REC_MAGIC;
        hdr[1] = W25_REC_VALID;
        hdr[2] = (uint8_t)(g_wr_seq >> 8);                  /* 序号接上，不重头开始 */
        hdr[3] = (uint8_t)(g_wr_seq & 0xFFU);
        W25_PageProgram(nsec, hdr, W25_REC_HDR_SIZE);

        /* 换了扇区，缓存里的条数和扇区头都要跟着更新，
         * 否则 W25_RecRead 会按旧信息算出错误的地址 */
        if (next == 0U) { g_recA.seq = g_wr_seq; g_cntA = 0U; }
        else            { g_recB.seq = g_wr_seq; g_cntB = 0U; }

        g_rec_total = (uint32_t)g_cntA + (uint32_t)g_cntB;

        g_wr_sector = next;
        g_wr_count  = 0U;
        sector      = nsec;
    }

    /* ② 组装 3 字节记录：温度是两个字节的补码，湿度一个字节 */
    rec[0] = (uint8_t)(((uint16_t)temp_x10) >> 8);          /* 温度高字节 */
    rec[1] = (uint8_t)(((uint16_t)temp_x10) & 0xFFU);       /* 温度低字节 */
    rec[2] = (uint8_t)((humi_x10 > 255) ? 255 : ((humi_x10 < 0) ? 0 : humi_x10));

    /* ③ 写到当前扇区的对应位置 */
    addr = sector + W25_REC_HDR_SIZE + (uint32_t)g_wr_count * W25_REC_SIZE;
    W25_PageProgram(addr, rec, W25_REC_SIZE);

    g_wr_count++;
    g_wr_seq++;

    /* ④ 同步更新缓存（不再回头重新扫扇区，那样每次写都要多花 40ms） */
    if (g_wr_sector == 0U) { if (g_cntA < 0xFFFFU) g_cntA++; }
    else                   { if (g_cntB < 0xFFFFU) g_cntB++; }
    g_rec_total = (uint32_t)g_cntA + (uint32_t)g_cntB;

    return 1U;
}

/* 按"逻辑顺序"读取第 idx 条历史记录（idx 从 0 开始 = 最早的一条）。
 * 之所以要这个函数而不是直接算地址，是因为双扇区乒乓后物理地址不连续。
 * 返回 1 = 成功读出，0 = idx 超范围。
 *
 * 【性能关键】这里用的是缓存好的 g_cntA / g_cntB，不再每次重新扫扇区。
 * 如果每次调用都重新数一遍，OLED_PageHistory 一页要调 4 次，
 * 每次扫 2728 条记录，翻一页就得等 150ms —— 手感明显发滞。 */
static uint8_t W25_RecRead(uint32_t idx, int16_t *temp_x10, int16_t *humi_x10)
{
    uint32_t addr;
    uint8_t  raw[W25_REC_SIZE];

    if (!g_rec_ok) return 0U;
    if (idx >= g_rec_total) return 0U;

    /* 决定这条记录在哪个扇区、第几条。
     * 排序依据：扇区头的序号(seq) 大的表示"更新的那一轮"。 */
    if ((g_cntA > 0U) && (g_cntB > 0U))
    {
        uint8_t a_is_newer = (g_recA.seq >= g_recB.seq) ? 1U : 0U;

        /* 序号小的那个扇区里存的是更早的记录，排在前面 */
        uint16_t old_cnt = a_is_newer ? g_cntB : g_cntA;
        uint32_t old_sec = a_is_newer ? W25_SECTOR_B : W25_SECTOR_A;

        if (idx < (uint32_t)old_cnt)
        {
            addr = old_sec + W25_REC_HDR_SIZE + idx * W25_REC_SIZE;
        }
        else
        {
            uint32_t k = idx - old_cnt;
            uint32_t new_sec = a_is_newer ? W25_SECTOR_A : W25_SECTOR_B;
            addr = new_sec + W25_REC_HDR_SIZE + k * W25_REC_SIZE;
        }
    }
    else
    {
        uint32_t sec = (g_cntA > 0U) ? W25_SECTOR_A : W25_SECTOR_B;
        addr = sec + W25_REC_HDR_SIZE + idx * W25_REC_SIZE;
    }

    W25_Read(addr, raw, W25_REC_SIZE);
    *temp_x10 = (int16_t)(((uint16_t)raw[0] << 8) | (uint16_t)raw[1]);
    *humi_x10 = (int16_t)raw[2];

    return 1U;
}

/* ==========================================================================
 * 13. 蜂鸣器（3 针模块：VCC / IO / GND，IO 接 PB5）
 *
 *    【硬件形态：3 针模块，不是 2 脚裸蜂鸣器 —— 驱动方式完全不同】
 *      裸蜂鸣器只有两根线，要自己考虑限流电阻、灌电流/拉电流能力。
 *      3 针模块背面已经焊好三极管驱动级 + 基极电阻，丝印为 VCC / IO / GND：
 *          VCC -> 3.3V      GND -> GND      IO -> PB5
 *      → IO 是"逻辑电平输入"，**不消耗 STM32 的引脚驱动电流**，
 *        也就不存在"灌电流比拉电流强"的讲究（这点跟裸蜂鸣器不一样）。
 *        代码因此非常干净：只翻转电平，不担心电流。
 *
 *    【触发极性：本项目的模块已确认为"低电平触发"】
 *      高电平触发：IO = 1 -> 响，IO = 0 -> 静音（常见款）
 *      低电平触发：IO = 0 -> 响，IO = 1 -> 静音（模块内部用的是 PNP 三极管）
 *      ★ 本项目这块模块的**丝印直接写了"低电触发"**，所以：
 *            BEEP_ACTIVE_HIGH = 0U   ->  IO 拉低才响
 *        以后若换成高电平触发的模块，把这一行改成 1U 即可，其余代码不用动。
 *      极性写反只会表现为"响反了"，不会烧任何东西，可以放心改。
 *
 *    【上电自检：短鸣一声，验证极性与接线（见 main() 第 8.5 步）】
 *      main() 初始化完成后会短鸣 200ms。对照下表即可定案：
 *        · 短鸣一声后安静      -> 接线正确，OK
 *        · 一直长鸣不停        -> 极性反了，把 BEEP_ACTIVE_HIGH 取反重烧
 *        · 全程一点声音都没有  -> 先查 VCC/GND 是否接反、是否量到 3.3V
 *      把"极性和接线对不对"在上电 5 秒内就能确定，而不是等到温度升到
 *      30°C 触发报警时才发现 —— 这一点在调试效率上很关键。
 *
 *    【有源 vs 无源，一字之差天壤之别】
 *      有源蜂鸣器：内部自带振荡电路，给直流电就发出固定音调，两态驱动即可。
 *      无源蜂鸣器：内部只有一个发声片，必须用 2~4kHz 方波驱动才能响，
 *                  要用定时器输出 PWM，音调由频率决定、音量由占空比决定。
 *      → 判断方法：给 3.3V 直流，响的是有源，不响的是无源。
 *      本项目用的是 3 针有源模块，GPIO 直接翻转即可，无需 PWM。
 * ========================================================================== */
#define BEEP_ACTIVE_HIGH   0U       /* ★ 本模块丝印「低电触发」→ 填 0；高电平触发填 1U */

#if (BEEP_ACTIVE_HIGH)
    #define BEEP_ON()       (GPIOB_BSRR = (1U << 5U))            /* 拉高 -> 响   */
    #define BEEP_OFF()      (GPIOB_BSRR = (1U << (5U + 16U)))    /* 拉低 -> 静音 */
    #define BEEP_TRIG_STR   " Beep:High-Trg  "                   /* OLED 自检页显示 */
    #define BEEP_TRIG_TXT   "高电平触发"
#else
    #define BEEP_ON()       (GPIOB_BSRR = (1U << (5U + 16U)))    /* 拉低 -> 响   */
    #define BEEP_OFF()      (GPIOB_BSRR = (1U << 5U))            /* 拉高 -> 静音 */
    #define BEEP_TRIG_STR   " Beep:Low-Trg   "
    #define BEEP_TRIG_TXT   "低电平触发"
#endif

static void Buzzer_Init(void)
{
    /* 先写输出寄存器、再改模式，顺序不能反。
     * 复位后 PB5 是"浮空输入"，切到"推挽输出"的一瞬间，引脚会立刻跟随
     * ODR bit5 的当前值（复位值是 0）——若模块是高电平触发，就会在
     * 改模式那一刻叫一声。所以先把 ODR 定成"静音"电平，再切输出。 */
#if (BEEP_ACTIVE_HIGH)
    GPIOB_BSRR = (1U << (5U + 16U));           /* 高电平触发：先写 0 = 静音 */
#else
    GPIOB_BSRR = (1U << 5U);                   /* 低电平触发：先写 1 = 静音 */
#endif
    /* PB5 -> 推挽输出 2MHz
     *   通用推挽 2MHz：CNF=00, MODE=10 -> 00<<2|10 = 0010b = 0x2
     *   引脚 5 在 CRL 的第 20~23 位 */
    GPIOB_CRL &= ~(0x0FU << 20);
    GPIOB_CRL |=  (0x02U << 20);
}

/* ==========================================================================
 * 14. 数据解析与软件滤波
 *
 *    为什么必须滤波？DHT11 的分辨率只有 1°C / 1%，响应又慢，
 *    直接显示会看到 "25 -> 26 -> 25" 来回跳，读数体验很差。
 *    做法：保存最近 5 次有效采样，显示它们的平均值，曲线就平滑了。
 *    （定点数存储 + 滑动平均）
 *
 *    为什么不用浮点？用 int16_t 存"放大 10 倍"的整数（256 表示 25.6），
 *    全整数运算既快又不用链接浮点库，64KB Flash 省着点用。
 * ========================================================================== */
#define FILTER_N          5U        /* 参与平均的采样个数 */

#define TEMP_ALARM_X10    300       /* 报警阈值：30.0 °C（×10 存储） */
#define HUMI_ALARM_X10    800       /* 报警阈值：80.0 %（×10 存储）  */

static int16_t  g_temp_buf[FILTER_N];   /* 环形缓冲区：温度样本 */
static int16_t  g_humi_buf[FILTER_N];   /* 环形缓冲区：湿度样本 */
static uint8_t  g_filt_idx;             /* 写指针 */
static uint8_t  g_filt_cnt;             /* 已攒到的有效样本数（最多 FILTER_N） */

static int16_t  g_temp_now;             /* 本次原始读数（×10） */
static int16_t  g_humi_now;
static int16_t  g_temp_avg;             /* 滤波后的显示值（×10） */
static int16_t  g_humi_avg;
static uint8_t  g_alarm;                /* 1 = 超过阈值 */

/* 历史极值：上电以来出现的最高/最低值（拿滤波后的值来比，免得单次跳变把极值带偏）。
 * 初值故意设成 int16_t 能表示的两个极端值，这样第一次采样必定会刷新它们，
 * 不需要额外的"是否第一次"标志位。 */
static int16_t  g_temp_min = 32767;
static int16_t  g_temp_max = -32768;
static int16_t  g_humi_min = 32767;
static int16_t  g_humi_max = -32768;

/* 把原始字节解析成温湿度 -> 入环形缓冲区 -> 算出平均值与报警标志 */
static void DHT_Process(void)
{
    int32_t sum_t = 0;
    int32_t sum_h = 0;
    uint8_t i, n;

    /* 湿度 = 整数部分 ×10 + 小数部分（DHT11 的小数位通常是 0，这样写能兼容别的型号） */
    g_humi_now = (int16_t)((int16_t)g_dht_raw[0] * 10 + (int16_t)g_dht_raw[1]);

    /* 温度：DHT11 规定"温度整数"字节的最高位为 1 时表示零下温度 */
    if ((g_dht_raw[2] & 0x80U) != 0U)
    {
        g_temp_now = (int16_t)(-((int16_t)(g_dht_raw[2] & 0x7FU) * 10 + (int16_t)g_dht_raw[3]));
    }
    else
    {
        g_temp_now = (int16_t)((int16_t)g_dht_raw[2] * 10 + (int16_t)g_dht_raw[3]);
    }

    /* 存入环形缓冲区：写满一圈就从 0 覆盖，永远保留最近的 FILTER_N 次 */
    g_temp_buf[g_filt_idx] = g_temp_now;
    g_humi_buf[g_filt_idx] = g_humi_now;
    g_filt_idx++;
    if (g_filt_idx >= FILTER_N) g_filt_idx = 0U;
    if (g_filt_cnt < FILTER_N) g_filt_cnt++;

    /* 求平均（刚上电时样本还没攒够，就按实际的个数算） */
    n = g_filt_cnt;
    for (i = 0U; i < n; i++)
    {
        sum_t += (int32_t)g_temp_buf[i];
        sum_h += (int32_t)g_humi_buf[i];
    }
    g_temp_avg = (int16_t)(sum_t / (int32_t)n);
    g_humi_avg = (int16_t)(sum_h / (int32_t)n);

    /* 用滤波后的值判断报警，避免单次跳变引起误报 */
    g_alarm = ((g_temp_avg > TEMP_ALARM_X10) || (g_humi_avg > HUMI_ALARM_X10)) ? 1U : 0U;
}

/* 把本次结果打到串口：原始 5 字节 + 当前值 + 滤波值，出问题时最好排查 */
static void DHT_PrintUart(void)
{
    uint8_t i;

    UART_Puts("[DHT11] raw:");
    for (i = 0U; i < 5U; i++)
    {
        UART_PutC(' ');
        UART_PutHex(g_dht_raw[i]);
    }
    UART_Puts(" | Temp ");  UART_PutFix1(g_temp_now);  UART_Puts("C");
    UART_Puts("  Humi ");   UART_PutFix1(g_humi_now);  UART_Puts("%");
    UART_Puts(" | 滤波后 ");UART_PutFix1(g_temp_avg);  UART_Puts("C");
    UART_Puts(" ");         UART_PutFix1(g_humi_avg);  UART_Puts("%");
    UART_Puts(g_alarm ? " | [报警]\r\n" : " | [正常]\r\n");
}

/* 第 3 行（画面 0/1 共用）：状态提示
 *   本次读取失败 -> FAIL(err 编号)，这样不用连串口也能知道错在哪一步
 *   正常         -> ALARM / Normal */
static void OLED_ShowStatus(void)
{
    char    line[32];
    uint8_t n;

    n = StrAppendStr(line, 0U, "Status: ");
    if (g_dht_err != 0U)
    {
        n = StrAppendStr(line, n, "FAIL(");
        n = StrAppendU32(line, n, g_dht_err);
        n = StrAppendStr(line, n, ")");
    }
    else
    {
        n = StrAppendStr(line, n, (g_alarm != 0U) ? "ALARM" : "Normal");
    }
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(4, 0, line);
}

/* 第 4 行（画面 0/1 共用）：采样统计 + 当前画面号
 * 显示 "M2/4" 是为了让人知道现在在看第几个画面、一共几个，方便按键切换 */
static void OLED_ShowFooter(void)
{
    char    line[32];
    uint8_t n;

    n = StrAppendStr(line, 0U, "OK:");
    n = StrAppendU32(line, n, g_dht_ok_cnt);
    n = StrAppendStr(line, n, " F:");
    n = StrAppendU32(line, n, g_dht_fail_cnt);
    n = StrAppendStr(line, n, " M");
    n = StrAppendU32(line, n, (uint32_t)g_mode + 1U);   /* 从 1 开始数，看着自然 */
    n = StrAppendStr(line, n, "/");
    n = StrAppendU32(line, n, MODE_NUM);
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(6, 0, line);
}

/* 画面 0：温湿度 —— 画第 1、2 行 */
static void OLED_PageData(void)
{
    char    line[32];
    uint8_t n;

    n = StrAppendStr(line, 0U, "Temp: ");
    n = StrAppendFix1(line, n, g_temp_avg);
    line[n] = (char)CH_DEGREE;  n++;
    line[n] = 'C';              n++;
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(0, 0, line);

    n = StrAppendStr(line, 0U, "Humi: ");
    n = StrAppendFix1(line, n, g_humi_avg);
    line[n] = '%';              n++;
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(2, 0, line);
}

/* 画面 1：时间 —— 画第 1、2 行。
 * 时间从开机算起（开机 = 00:00:00），由 TIM2 中断累加的 g_ms_tick 换算而来。
 * 这个函数在主循环里每秒被调用一次，所以秒位是"跳着走"的，不会卡住。 */
static void OLED_PageTime(void)
{
    char     line[32];
    uint8_t  n;
    uint32_t hh, mm, ss;

    Time_Get(&hh, &mm, &ss);

    n = StrAppendStr(line, 0U, "Time: ");
    n = StrAppendPad2(line, n, hh);  line[n] = ':';  n++;
    n = StrAppendPad2(line, n, mm);  line[n] = ':';  n++;
    n = StrAppendPad2(line, n, ss);
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(0, 0, line);

    /* 第二行顺便显示总秒数：它单调递增，一眼就能看出计时真的在走，
     * 比只显示"时:分:秒"更有说服力 */
    n = StrAppendStr(line, 0U, "Uptime: ");
    n = StrAppendU32(line, n, g_ms_tick / 1000U);
    n = StrAppendStr(line, n, "s");
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(2, 0, line);
}

/* 画面 2：历史极值 —— 4 行全部用来放极值（所以这一页不画状态行和统计行，
 * 想看状态按一下键切回画面 0 即可）。
 * 演示技巧：对着传感器哈一口气，Tmax/Hmax 会立刻往上走、且不会掉回来。 */
static void OLED_PageMinMax(void)
{
    char    line[32];
    uint8_t n;

    n = StrAppendStr(line, 0U, "Tmax: ");
    n = StrAppendFix1(line, n, g_temp_max);
    line[n] = (char)CH_DEGREE;  n++;
    line[n] = 'C';              n++;
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(0, 0, line);

    n = StrAppendStr(line, 0U, "Tmin: ");
    n = StrAppendFix1(line, n, g_temp_min);
    line[n] = (char)CH_DEGREE;  n++;
    line[n] = 'C';              n++;
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(2, 0, line);

    n = StrAppendStr(line, 0U, "Hmax: ");
    n = StrAppendFix1(line, n, g_humi_max);
    line[n] = '%';              n++;
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(4, 0, line);

    n = StrAppendStr(line, 0U, "Hmin: ");
    n = StrAppendFix1(line, n, g_humi_min);
    line[n] = '%';              n++;
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(6, 0, line);
}

/* 画面 3：历史记录 —— 从 W25Q64 里按顺序读出，每页 3 条。
 * 版面：  第 1 行  H第几页/共几页 N=总条数
 *         第 2~4 行  序号 温度 湿度
 *                     例：12 25.6 60.2
 * 翻页技巧：在这个画面上再按一下 K1 就往后翻一页（由主循环处理）。
 * 这里每次都现场从 Flash 读，虽然慢一点（读 3 条约几毫秒），
 * 但保证显示的一定是最新的数据，不会出现"内存缓存和 Flash 不一致"的问题。 */
static void OLED_PageHistory(void)
{
    char     line[32];
    uint8_t  n, row;
    int16_t  t10, h10;
    uint32_t base, first;      /* first = 本页第一条记录在"总记录"里的绝对序号 */
    uint32_t show_cnt;

    if (!g_rec_ok)
    {
        OLED_ShowStr(0, 0, "History: (no    ");
        OLED_ShowStr(2, 0, " W25Q64 module )");
        OLED_ShowStr(4, 0, " Check SPI wire:");
        OLED_ShowStr(6, 0, " CS4 CLK5 DO6 DI7");
        return;
    }

    if (g_rec_total == 0U)
    {
        OLED_ShowStr(0, 0, "History: empty  ");
        OLED_ShowStr(2, 0, " Need 1 min to  ");
        OLED_ShowStr(4, 0, " record 1st one.");
        OLED_ShowStr(6, 0, " Total: 0       ");
        return;
    }

    /* 只展示最近的 HIST_MAX 条：超出部分从前面丢掉。
     * first 是"本页第 1 条"在总记录里的绝对下标；
     * 如果总记录数超过 HIST_MAX，就把起点往后推，只保留最新的那些。 */
    show_cnt = (g_rec_total > HIST_MAX) ? HIST_MAX : g_rec_total;

    /* 页号从"最新的那些记录"的末尾往回算：
     *   第 0 页 = 最旧的可见记录，最后一页 = 最新的记录
     * base 是相对 show_cnt 的下标，加上 first 才是绝对下标 */
    base  = (uint32_t)g_hist_page * HIST_PER_PAGE;
    first = g_rec_total - show_cnt;

    /* 第 1 行当标题栏：显示 "Hxx/yy  N=总数"，
     * 告诉用户"现在在第几页、一共几页、总共存了多少条"。
     * 没有这一行的话，翻页时完全不知道自己翻到哪了。 */
    {
        uint32_t pages = (show_cnt + HIST_PER_PAGE - 1U) / HIST_PER_PAGE;
        if (pages == 0U) pages = 1U;

        n = StrAppendStr(line, 0U, "H");
        n = StrAppendU32(line, n, (uint32_t)g_hist_page + 1U);
        line[n] = '/';  n++;
        n = StrAppendU32(line, n, pages);
        n = StrAppendStr(line, n, " N=");
        n = StrAppendU32(line, n, g_rec_total);
        n = StrPadTo(line, n, 16U);
        OLED_ShowStr(0, 0, line);
    }

    for (row = 0U; row < HIST_PER_PAGE; row++)
    {
        uint32_t rel  = base + row;                          /* 相对 show_cnt 的下标 */
        /* 行号换算成 SSD1306 的 page：第 1 行(page0+1)留给标题，
         * 数据从第 2 行(page 2)开始，每行占两个 page，所以是 2/4/6。
         * 千万不能再写成 row*2+1 —— 那样每条都会压住上一行的下半截。 */
        uint8_t  rpag = (uint8_t)(HIST_ROW_TOP + row * 2U);

        if (rel >= show_cnt)
        {
            n = StrAppendStr(line, 0U, "  --            ");   /* 空位用横线占位 */
            n = StrPadTo(line, n, 16U);
            OLED_ShowStr(rpag, 0, line);
            continue;
        }

        if (!W25_RecRead(first + rel, &t10, &h10))
        {
            n = StrAppendStr(line, 0U, "  read err     ");
            n = StrPadTo(line, n, 16U);
            OLED_ShowStr(rpag, 0, line);
            continue;
        }

        /* 格式："1234 25.6 60.2"（序号 + 温度 + 湿度），正好 16 格以内。
         * 不写单位符号是为了省宽度——这一页有标题栏提示，单位不会误解。
         * 序号从 1 开始数（first + rel 是 0 起算的下标，显示时 +1），
         * 否则第一条记录会显示成 "0"，看着像程序算错了。
         * 【怎么换算时间】每 1 分钟存一条，所以"序号差 = 分钟差"：
         * 最后一条是刚才，往上数 5 条就是大约 5 分钟前。 */
        n = StrAppendU32(line, 0U, first + rel + 1U);
        line[n] = ' ';  n++;
        n = StrAppendFix1(line, n, t10);
        line[n] = ' ';  n++;
        n = StrAppendFix1(line, n, h10);
        n = StrPadTo(line, n, 16U);
        OLED_ShowStr(rpag, 0, line);
    }
}
/* 画面 4（WiFi）的函数体定义在第 15 章的 ESP8266 部分 —— 它要用到那一章里
 * 定义的 g_esp_ready / g_esp_link / g_esp_sent 三个状态变量，所以跟着放在一起。
 * 这里先声明一下，让本文件前面的调用合法（C 语言：先声明后使用）。 */
static void OLED_PageWifi(void);

static void OLED_Refresh(void)
{
    if (g_mode == 0U)
    {
        OLED_PageData();
        OLED_ShowStatus();
        OLED_ShowFooter();
    }
    else if (g_mode == 1U)
    {
        OLED_PageTime();
        OLED_ShowStatus();
        OLED_ShowFooter();
    }
    else if (g_mode == 2U)
    {
        OLED_PageMinMax();     /* 这一页 4 行都是极值 */
    }
    else if (g_mode == 3U)
    {
        OLED_PageHistory();    /* 这一页 4 行是历史记录 */
    }
    else
    {
        OLED_PageWifi();       /* 画面 4：WiFi 热点状态 */
    }
}

/* ==========================================================================
 * 15. ESP8266（ESP-01S）—— USART2 + AT 指令 + 自建热点推数据
 *
 *  【为什么用 AT 指令，而不是直接给 ESP8266 写固件】
 *     ESP8266 本身是一颗"带 WiFi 的单片机"，可以往里烧自己写的程序；
 *     但那样就要再学一套工具链，两颗芯片的程序还会互相耦合、一起调试。
 *     这里把 ESP8266 当成一个"串口转 WiFi 的模块"用：STM32 只发文本命令、
 *     读文本应答，职责清晰 —— 而且模块本身可以用电脑串口助手单独验证，
 *     一次只留一个未知数。这是嵌入式里非常常见的分工。
 *
 *  【接线】PA2(USART2_TX) -> 模块 RXD      PA3(USART2_RX) <- 模块 TXD  （交叉！）
 *    CH_PD 必须接 3.3V（不接模块毫无反应），RST/GPIO0/GPIO2 悬空，VCC 只认 3.3V。
 *
 *  【为什么单独占一条 USART2】
 *    USART1 留给 CH340 打调试日志。两条串口独立，就能同时看到
 *    "STM32 发了什么 AT 指令 / 模块回了什么" 和 "温湿度业务日志"，
 *    排查时不必靠猜 —— 这是本方案最重要的一个设计决定。
 *
 *  【数据通路】
 *    手机 --连热点 ESP_TEMP--> ESP-01S（内置 AP + TCP Server:8080）
 *         --USART2（AT 指令）--> STM32 每 2 秒推一行温湿度
 *
 *  【本模块实测参数（脱机测出来的，不是抄文档）】
 *    AT 版本 1.1.0.0 / SDK 1.5.4，出厂波特率 115200；
 *    AT+CWSAP 用 **4 个参数**；没有客户端时 CIPSEND 回 "link is not valid"；
 *    用 ATE0 关掉回显后应答最干净。
 * ========================================================================== */

#define ESP_BAUD        115200U     /* 实测值：这块模块出厂就是 115200 */
#define ESP_AP_SSID     "ESP_TEMP"
#define ESP_AP_PWD      "12345678"
#define ESP_TCP_PORT    8080U

#define ESP_RX_BUF      192U        /* 收模块应答的缓冲区，够放下一条完整回显 */
#define ESP_PROMPT_MS   1000U       /* 等 '>' 提示符的上限。2026-09-21 实测：连发 12 次，
                                     * 提示符耗时 208~219ms（抖动仅 11ms）。留 4.5 倍余量，
                                     * 因为重连后第一次发送、或模块繁忙时可能更慢；
                                     * 提示符按时到达时这个上限根本不会消耗到，所以给宽无害。 */
#define ESP_SENDOK_MS   1000U       /* 等 SEND OK 的上限 */
#define ESP_KA_MS       30000U      /* 心跳周期：每 30 秒确认一次模块还活着 */

/* 是否把"每 2 秒一帧"的数据交互也打到 CH340 上？
 *   0 = 只打启动配置和出错（默认）。否则每 2 秒两行 AT 日志会把业务日志淹掉。
 *   排查发送链路时改成 1，就能看到完整的 AT 一来一回。 */
#define ESP_LOG_FRAMES  0U

static uint8_t  g_esp_ready;        /* 1 = 热点 + TCP 服务器已就绪 */
static uint8_t  g_esp_link;         /* 1 = 已有 TCP 客户端连着 */
static uint32_t g_esp_sent;         /* 成功推送的数据帧数 */
static uint32_t g_esp_fail;         /* 推送失败次数 */
static uint32_t g_esp_last_ka;      /* 上次心跳的时刻 */
static char     g_esp_rx[ESP_RX_BUF];   /* 模块应答 / 主动上报的累积缓冲 */
static uint16_t g_esp_rx_n;             /* 里面已有多少字节 */
static uint16_t g_esp_scan;             /* 关键词已经扫到哪个位置（避免重复判定） */
static char     g_esp_cmd[48];          /* 拼 AT 指令用 */

/* ---------------- 15.1 USART2 收发 ---------------- */

static void USART2_PutC(char c)
{
    while ((USART2_SR & (1U << 7)) == 0U);   /* TXE=1 表示发送寄存器空了 */
    USART2_DR = (uint32_t)c;
}

static void USART2_Puts(const char *s)
{
    while (*s != '\0')
    {
        USART2_PutC(*s);
        s++;
    }
}

static void ESP_UartInit(void)
{
    RCC_APB2ENR |= (1U << 2);    /* GPIOA 时钟（UART_Init 里已开，这里再确认一次） */
    RCC_APB1ENR |= (1U << 17);   /* USART2 时钟：它在 APB1 上，使能位是第 17 位 */

    /* PA2 = USART2_TX：复用推挽输出 50MHz -> CNF=10, MODE=11 -> 1011b = 0xB
     * 【和 USART1 一样，绝不能写成 0xE（那是复用开漏，推不出高电平）】 */
    GPIOA_CRL &= ~(0x0FU << 8);
    GPIOA_CRL |=  (0x0BU << 8);

    /* PA3 = USART2_RX：浮空输入 -> CNF=01, MODE=00 -> 0100b = 0x4
     * 接收脚由模块驱动，单片机这边只要"听着"就行 */
    GPIOA_CRL &= ~(0x0FU << 12);
    GPIOA_CRL |=  (0x04U << 12);

    /* 波特率：注意 USART2 挂在 APB1 上，时钟只有主频的一半（64/2 = 32MHz）。
     * BRR = fCK / 波特率（不要再乘 16，见第 4 章那一段推导）。
     * 32MHz / 115200 = 277.8 -> 278，误差 -0.08%，完全够用。
     * 这里写成 (主频/2) 的表达式，万一时钟没升上去（HSI 8MHz）也能自动算对。 */
    USART2_BRR = (((g_cpu_mhz * 1000000U) / 2U) + (ESP_BAUD / 2U)) / ESP_BAUD;

    USART2_CR1 = (1U << 13)      /* UE：串口使能     */
               | (1U << 3)       /* TE：发送使能     */
               | (1U << 2)       /* RE：接收使能     */
               | (1U << 5);      /* RXNEIE：收到一个字节就中断（见下方长注释）*/

    /* 【为什么必须开接收中断，而不能靠主循环轮询】
     *   115200 波特率下，一个字节只占 10 位 = 约 87us。
     *   而主循环里 ESP_RxPoll 是每 10ms 才轮到一次（外层 200 步 x 10ms），
     *   AT 指令等待期间最密也就 1ms 一次 —— 都远远慢于 87us。
     *   STM32 的硬件接收寄存器只能存 1 个字节：第 2 个字节到来时前一个还没被读走，
     *   就置 ORE（溢出）标志，并把新字节直接丢弃。
     *   ⇒ 模块回 "\r\nOK\r\n" 这 6 个字节里，只有第 1 个 '\r' 能活下来，
     *     "OK" 永远凑不出来 → 每一条 AT 都超时。
     *   【2026-09-21 实测踩坑】当时现象是：日志里 "(没等到期望应答)" 后面多出一个空格，
     *   而那正是被打印函数转成空格的 '\r' —— 一个字节的证据。
     *   ⇒ 开 RXNE 中断，字节一进 DR 就在中断里搬进缓冲，中间没有间隙，一个都不丢。 */
    NVIC_ISER1 = (1U << 6);      /* 在 NVIC 里打开 USART2 中断（编号 38 -> ISER1 bit 6） */
}

/* USART2 接收中断：把刚到的字节搬进缓冲。
 * 函数名必须和启动文件向量表里的一致，写对了会自动覆盖启动文件里的弱定义。 */
void USART2_IRQHandler(void)
{
    /* 读 SR 再读 DR：这一对操作同时清掉 RXNE / ORE / NE / FE / PE */
    if ((USART2_SR & (1U << 5)) != 0U)          /* RXNE=1：确实收到字节了 */
    {
        char c = (char)(USART2_DR & 0xFFU);

        if (g_esp_rx_n < (ESP_RX_BUF - 1U))
        {
            g_esp_rx[g_esp_rx_n] = c;
            g_esp_rx_n++;
        }
        else
        {
            /* 缓冲满了整体丢掉重来。正常不会走到这里；真出现说明有大量
             * 我们没处理的数据（比如客户端一直往我们发），丢掉比卡死好。 */
            g_esp_rx_n = 0U;
            g_esp_scan = 0U;
            g_esp_rx[0] = c;
            g_esp_rx_n  = 1U;
        }
        g_esp_rx[g_esp_rx_n] = '\0';            /* 始终保持 NUL 结尾 */
    }
}

/* 把 USART2 收到的字节收进缓冲（非阻塞：只取当前已到的，立刻返回）
 *
 * 注意：自 2026-09-21 起，字节主要由 USART2_IRQHandler 在中断里搬走，
 * 这个函数降级为"安全网"——万一某个字节在中断打开之前就到了 DR，
 * 或者中断被长时间屏蔽过，这里还能补收。留着不占什么开销。 */
static void ESP_RxPoll(void)
{
    uint16_t guard = 0U;

    while ((USART2_SR & (1U << 5)) != 0U)      /* RXNE=1 表示收到一个字节 */
    {
        char c = (char)(USART2_DR & 0xFFU);    /* 读 DR 同时清掉 RXNE */

        if (g_esp_rx_n < (ESP_RX_BUF - 1U))
        {
            g_esp_rx[g_esp_rx_n] = c;
            g_esp_rx_n++;
        }
        else
        {
            /* 缓冲满了就整体丢掉重来。正常情况不会走到这里；
             * 真出现说明有大量我们没有处理的数据（比如客户端一直往我们发），
             * 丢掉比卡死好。 */
            g_esp_rx_n  = 0U;
            g_esp_scan  = 0U;
        }

        if (++guard >= 300U) break;            /* 防止持续有数据时一直不返回 */
    }
    g_esp_rx[g_esp_rx_n] = '\0';
}

/* ---------------- 15.2 关键词查找 ---------------- */

/* 判断缓冲里从 pos 开始是不是给定字符串 */
static uint8_t ESP_MatchAt(uint16_t pos, const char *t)
{
    uint8_t j = 0U;

    while (t[j] != '\0')
    {
        if (g_esp_rx[pos + j] != t[j]) return 0U;   /* 缓冲是 NUL 结尾的，越界读只会读到 '\0' */
        j++;
    }
    return 1U;
}

static uint8_t ESP_Has(const char *t)
{
    uint16_t i;

    for (i = 0U; i < g_esp_rx_n; i++)
    {
        if (ESP_MatchAt(i, t)) return 1U;
    }
    return 0U;
}

/* 解析模块**主动**上报的连接状态：0,CONNECT / 0,CLOSED
 *
 * 【为什么要专门写这一段】
 *   老固件（这版 1.1.0.0）的 AT+CIPSTATUS 只回一个 STATUS:5，
 *   根本看不出"有没有客户端连着"。想知道客户端上下线，
 *   只能靠模块自己吐出来的这两条消息。这是本方案里唯一的事件驱动部分。
 *
 * 【g_esp_scan 的作用】只处理"新到的"字节，避免同一条 CONNECT 被反复判定。 */
static void ESP_TrackLink(void)
{
    uint16_t i;
    uint16_t limit;

    if (g_esp_rx_n < 7U) return;

    /* 末尾 6 个字节留到下一轮再扫：关键词可能正好被切在两批数据中间 */
    limit = (uint16_t)(g_esp_rx_n - 6U);

    for (i = g_esp_scan; i < limit; i++)
    {
        if (ESP_MatchAt(i, "CONNECT"))
        {
            g_esp_scan = (uint16_t)(i + 7U);
            if (g_esp_link == 0U)
            {
                g_esp_link = 1U;
                UART_Puts("[ESP] 手机已连上，开始推送数据\r\n");
            }
        }
        else if (ESP_MatchAt(i, "CLOSED"))
        {
            g_esp_scan = (uint16_t)(i + 6U);
            if (g_esp_link != 0U)
            {
                g_esp_link = 0U;
                UART_Puts("[ESP] 客户端断开（热点还在，等它重新连）\r\n");
            }
        }
    }

    if (g_esp_scan < limit) g_esp_scan = limit;
}

/* ---------------- 15.3 发指令 / 等应答 ---------------- */

/* 等某个关键词出现，超时返回 0。
 * 等待期间也在收数据，所以模块中途上报 CONNECT/CLOSED 不会丢。 */
static uint8_t ESP_WaitFor(const char *token, uint32_t timeout_ms)
{
    uint32_t t0 = g_ms_tick;

    while ((g_ms_tick - t0) < timeout_ms)
    {
        ESP_RxPoll();
        ESP_TrackLink();
        if (ESP_Has(token)) return 1U;
        delay_ms(1U);
    }

    ESP_RxPoll();                    /* 超时前再收一次，不然最后几个字节会漏 */
    ESP_TrackLink();
    return (ESP_Has(token) != 0U) ? 1U : 0U;
}

/* 把模块回显打成一整行：换行换成空格，否则一条应答会把日志撑成十几行 */
static void ESP_LogResp(void)
{
    uint16_t i;

    for (i = 0U; i < g_esp_rx_n; i++)
    {
        char c = g_esp_rx[i];

        if ((c == '\r') || (c == '\n'))            UART_PutC(' ');
        else if ((c >= 0x20) && (c < 0x7F))        UART_PutC(c);
        else                                       UART_PutC('?');
    }
    UART_Puts("\r\n");
}

/* 发一条 AT 指令并等期望应答。log_it=1 时把一来一回都打到 CH340。 */
static uint8_t ESP_Cmd(const char *cmd, const char *expect, uint32_t timeout_ms, uint8_t log_it)
{
    uint8_t ok;

    /* 清空缓冲：上一次的应答不能混进来。
     * 关中断再清 —— 否则接收中断可能正好插在这三行中间把新字节写进来，
     * 出现"计数被清零、字节却已写入"的错位。几周期的关中断，代价可忽略。 */
    __asm volatile ("cpsid i" ::: "memory");
    g_esp_rx_n   = 0U;
    g_esp_scan   = 0U;
    g_esp_rx[0]  = '\0';
    __asm volatile ("cpsie i" ::: "memory");

    USART2_Puts(cmd);
    USART2_Puts("\r\n");
    if (log_it != 0U) { UART_Puts("[ESP>] "); UART_Puts(cmd); UART_Puts("\r\n"); }

    ok = ESP_WaitFor(expect, timeout_ms);

    if (log_it != 0U)
    {
        UART_Puts(ok ? "[ESP<] " : "[ESP<] (没等到期望应答) ");
        ESP_LogResp();
    }
    return ok;
}

/* ---------------- 15.4 上电配置：建热点 + 开 TCP 服务器 ---------------- */

static uint8_t ESP_Setup(void)
{
    uint8_t try;

    UART_Puts("[ESP] 开始配置：AP 模式 + 热点 + TCP 服务器\r\n");

    /* ① 关回显。模块默认把收到的指令原样回显一遍，关掉以后应答解析干净很多 */
    for (try = 0U; try < 3U; try++)
    {
        if (ESP_Cmd("ATE0", "OK", 800U, 1U)) break;
        delay_ms(200U);
    }

    /* ② 一条一条来。这四条的顺序不能换：
     *    先切 AP 模式 -> 才能建热点 -> 再开多连接(CIPMUX，AP 模式必须开)
     *    -> 最后开 TCP 服务器 */
    if (!ESP_Cmd("AT+CWMODE=2", "OK", 2000U, 1U))
    {
        UART_Puts("[ESP] AT+CWMODE=2 失败，模块可能没在 AT 状态\r\n");
        return 0U;
    }
    if (!ESP_Cmd("AT+CWSAP=\"" ESP_AP_SSID "\",\"" ESP_AP_PWD "\",5,3", "OK", 3000U, 1U))
    {
        UART_Puts("[ESP] 建热点失败（AT+CWSAP）\r\n");
        return 0U;
    }
    if (!ESP_Cmd("AT+CIPMUX=1", "OK", 2000U, 1U))
    {
        UART_Puts("[ESP] AT+CIPMUX=1 失败（AP 模式必须开多连接）\r\n");
        return 0U;
    }
    if (!ESP_Cmd("AT+CIPSERVER=1,8080", "OK", 3000U, 1U))
    {
        UART_Puts("[ESP] 开 TCP 服务器失败（AT+CIPSERVER）\r\n");
        return 0U;
    }

    /* ③ 清掉可能残留的 0 号链路。
     *    为什么需要：如果模块在 STM32 上电前就已经建过连接、而那条连接被对端
     *    粗暴掐断（RST），模块内部会留下一条"僵尸链路"——此后所有
     *    AT+CIPSEND=0 都回 `link is not valid`，而且 AT+CIPSTATUS 照样报
     *    STATUS:5 看着像连着。2026-09-21 实测踩到过这个坑。
     *    没有链路时这条指令回 `UNLINK` + `ERROR`，属正常提示，所以只看不断言。 */
    (void)ESP_Cmd("AT+CIPCLOSE=0", "OK", 1500U, 1U);

    /* ④ 回读一次 IP 当证据（回显里有 +CIFSR:APIP,"192.168.4.1"）。
     *    查询失败不算错误，只当参考 —— 老固件对查询指令支持不全。 */
    (void)ESP_Cmd("AT+CIFSR", "OK", 1500U, 1U);

    g_esp_ready = 1U;
    g_esp_link  = 0U;

    UART_Puts("[ESP] 就绪：热点 SSID=" ESP_AP_SSID "  密码=" ESP_AP_PWD "\r\n");
    UART_Puts("[ESP] 手机连上热点后，建 TCP Client 连 192.168.4.1:8080\r\n");
    return 1U;
}

/* 上电初始化：配置串口 -> 跑一遍 AT 配置（失败重试一次） */
static uint8_t ESP_Init(void)
{
    ESP_UartInit();
    delay_ms(500U);              /* 等模块上电稳定：它上电会先吐一串启动信息 */

    if (ESP_Setup()) return 1U;

    UART_Puts("[ESP] 第一轮配置失败，隔 300ms 重试一次\r\n");
    delay_ms(300U);
    if (ESP_Setup()) return 1U;

    g_esp_ready = 0U;
    return 0U;
}

/* ---------------- 15.5 推送一帧数据 ---------------- */

/* 把当前温湿度推给 0 号客户端。
 *
 * 【AT+CIPSEND 的三步握手，一步都不能省】
 *    ① 发 "AT+CIPSEND=0,<字节数>"      声明"我要发 N 字节"
 *    ② 模块回一个 '>' 提示符            必须等到它，才能发内容
 *    ③ 发 N 字节内容，等 "SEND OK"       长度必须和声明的一模一样
 *  这中间不能插别的指令，所以整段是阻塞的（有超时兜底）。 */
static uint8_t ESP_SendFrame(void)
{
    char     pay[64];
    uint8_t  n = 0U;
    uint32_t len;

    /* 组装数据帧：T=29.3C H=50.0% #12 A=0
     * 用"键=值"的紧凑文本，手机端随便一个网络调试助手就能读，不用解析库；
     * 每帧以 \r\n 结尾，一条一行，肉眼和脚本都好处理。 */
    n = StrAppendStr(pay, n, "T=");
    n = StrAppendFix1(pay, n, g_temp_avg);
    n = StrAppendStr(pay, n, "C H=");
    n = StrAppendFix1(pay, n, g_humi_avg);
    n = StrAppendStr(pay, n, "% #");
    n = StrAppendU32(pay, n, g_rec_total);
    n = StrAppendStr(pay, n, " A=");
    n = StrAppendU32(pay, n, (uint32_t)g_alarm);
    n = StrAppendStr(pay, n, "\r\n");
    len = (uint32_t)n;

    /* ① 拼 "AT+CIPSEND=0,<长度>" */
    n = 0U;
    n = StrAppendStr(g_esp_cmd, n, "AT+CIPSEND=0,");
    n = StrAppendU32(g_esp_cmd, n, len);
    g_esp_cmd[n] = '\0';

    /* 同样关中断清缓冲，避免和接收中断抢同一个计数器 */
    __asm volatile ("cpsid i" ::: "memory");
    g_esp_rx_n  = 0U;
    g_esp_scan  = 0U;
    g_esp_rx[0] = '\0';
    __asm volatile ("cpsie i" ::: "memory");

    USART2_Puts(g_esp_cmd);
    USART2_Puts("\r\n");
    if (ESP_LOG_FRAMES != 0U) { UART_Puts("[ESP>] "); UART_Puts(g_esp_cmd); UART_Puts("\r\n"); }

    /* ② 等 '>' —— 模块只在这个字符出现之后才收内容 */
    if (!ESP_WaitFor(">", ESP_PROMPT_MS))
    {
        g_esp_fail++;
        UART_Puts("[ESP] 没等到 '>' 提示符，模块回显: ");
        ESP_LogResp();

        /* 模块没上报 CLOSED、但已经发不出去了 —— 靠这句错误文本补判 */
        if (ESP_Has("link is not valid"))
        {
            g_esp_link = 0U;
            UART_Puts("[ESP] 客户端其实已断开（link is not valid）\r\n");
        }
        return 0U;
    }

    /* ③ 发内容本体（不能多一个字节、也不能少一个） */
    if (ESP_LOG_FRAMES != 0U) { UART_Puts("[ESP>] "); UART_Puts(pay); }

    USART2_Puts(pay);

    /* ④ 等 SEND OK */
    if (!ESP_WaitFor("SEND OK", ESP_SENDOK_MS))
    {
        g_esp_fail++;
        UART_Puts("[ESP] 没收到 SEND OK，模块回显: ");
        ESP_LogResp();
        return 0U;
    }

    g_esp_sent++;
    if (ESP_LOG_FRAMES != 0U) UART_Puts("[ESP<] SEND OK\r\n");
    return 1U;
}

/* ---------------- 15.6 心跳与自动恢复 ---------------- */

/* 每 30 秒确认模块还在。
 *
 * 【为什么必须做这件事（断线重连）】
 *   ESP-01S 峰值电流 300mA+，供电一瞬跌（比如手机刚连上、发射突然变强）
 *   就可能自己复位。复位后**热点和 TCP 服务器全部丢失**，但 STM32 这边
 *   毫无察觉 —— 代码看起来一切正常，用户那边却是"怎么突然连不上了"。
 *   定期发一条 AT，不通就重跑一遍初始化，它就能自己爬起来。 */
static void ESP_KeepAlive(void)
{
    if ((g_ms_tick - g_esp_last_ka) < ESP_KA_MS) return;
    g_esp_last_ka = g_ms_tick;

    if (!ESP_Cmd("AT", "OK", 500U, 0U))
    {
        UART_Puts("[ESP] 模块无应答（很可能复位了），重新初始化...\r\n");
        g_esp_ready = 0U;
        g_esp_link  = 0U;
        delay_ms(500U);

        if (ESP_Setup()) UART_Puts("[ESP] 已恢复：热点重新建立\r\n");
        else             UART_Puts("[ESP] 恢复失败，30 秒后再试\r\n");
    }
    else if (g_esp_link == 0U)
    {
        UART_Puts("[ESP] 心跳正常，等待手机连接（SSID " ESP_AP_SSID "）\r\n");
    }
}

/* 主循环每 10ms 调一次：收上报 + 心跳。全程非阻塞，不影响按键响应 */
static void ESP_RxService(void)
{
    if (g_esp_ready == 0U) return;

    ESP_RxPoll();
    ESP_TrackLink();
    ESP_KeepAlive();
}

/* ---------------- 15.7 画面 4：WiFi 状态 ---------------- */

/* 这一页专门给演示用：一眼看出"热点起了没 / 手机连上了没 / 发了多少帧"。
 * 现场演示时这比看串口日志直观得多。 */
static void OLED_PageWifi(void)
{
    char    line[32];
    uint8_t n;

    n = StrAppendStr(line, 0U, (g_esp_ready != 0U) ? "WiFi: AP OK" : "WiFi: FAIL");
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(0, 0, line);

    n = StrAppendStr(line, 0U, "SSID:");
    n = StrAppendStr(line, n, (g_esp_ready != 0U) ? ESP_AP_SSID : "-------");
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(2, 0, line);

    n = StrAppendStr(line, 0U, "IP: 192.168.4.1");
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(4, 0, line);

    n = StrAppendStr(line, 0U, (g_esp_link != 0U) ? "Client:1" : "Client:0");
    n = StrAppendStr(line, n, " TX:");
    n = StrAppendU32(line, n, g_esp_sent);
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(6, 0, line);
}

/* ==========================================================================
 * 16. 主函数
 * ========================================================================== */
int main(void)
{
    uint8_t  oled_ok;
    uint8_t  sda_pull_ok;
    uint8_t  found[8];
    uint8_t  n_dev;
    uint8_t  scl_lv;
    uint8_t  sda_lv;
    uint8_t  k;
    uint32_t id;
    uint32_t last_rec_ms = 0U;       /* 上次写历史记录的时刻，用于"每 1 分钟记一条" */

    Clock_Init();            /* 1. 主频提到 64MHz                      */
    SysTick_Init();          /* 2. 延时基准就绪（SysTick 当"秒表"用）  */
    TIM2_Init();             /* 3. 系统时基就绪（每 1ms 中断累加一次） */
    UART_Init();             /* 4. 串口日志就绪（PA9=TX）              */
    GPIO_Init();             /* 5. 引脚配置（含 PB1 按键）             */
    SPI1_Init();             /* 6. SPI1 就绪（W25Q64 用，PA4~PA7）     */
    Buzzer_Init();           /* 7. 蜂鸣器就绪（PB5，3 针模块的 IO 脚）  */

    /* 8. 上电信号：LED 长亮 1.5 秒。这一步不碰任何外设，
     *    如果连它都不亮，说明问题在烧录或供电，跟外设无关 */
    LED_ON();
    delay_ms(1500U);
    LED_OFF();
    delay_ms(200U);

    UART_Puts("\r\n=== 温湿度监测仪 v1.2（DHT11 + OLED + W25Q64 + 蜂鸣器 + ESP8266 WiFi）===\r\n");
    UART_Puts("CPU 主频: ");  UART_Putu(g_cpu_mhz);  UART_Puts(" MHz\r\n");

    /* 8.5 蜂鸣器自检：短鸣 200ms
     *
     *    为什么上电就要叫一声？——"触发极性对不对"这件事，如果等到
     *    温度升到 30°C 触发报警才知道，可能要等半小时。
     *    短鸣一下，5 秒内就能定案（判据见第 13 章注释）：
     *        短鸣后安静  -> 极性与接线都正确，OK
     *        一直长鸣    -> 极性反了，把 BEEP_ACTIVE_HIGH 取反重烧
     *        完全没声    -> 查 VCC/GND 是否接反、是否量到 3.3V */
    BEEP_ON();
    delay_ms(200U);
    BEEP_OFF();
    UART_Puts("[自检] 蜂鸣器短鸣 200ms（配置为");
    UART_Puts(BEEP_TRIG_TXT);
    UART_Puts("）：若此后一直长鸣不停，说明模块是另一种触发极性\r\n");

    /* 9. 诊断第一步：SDA 回读自检 —— 直接回答"OLED 的线到底接上没有"。
     *    LED 编码：通过闪 2 次；不通过闪 1 次 */
    sda_pull_ok = I2C_SdaEchoTest();
    for (k = 0U; k < (sda_pull_ok ? 2U : 1U); k++)
    {
        LED_ON();   delay_ms(250U);
        LED_OFF();  delay_ms(400U);
    }
    if (sda_pull_ok)
    {
        UART_Puts("[SDA自检] 通过（线上有上拉，接线正常）\r\n");
    }
    else
    {
        UART_Puts("[SDA自检] 失败：释放 SDA 后仍读到低电平 ——\r\n");
        UART_Puts("          SDA(PB7) 没接上 / 杜邦线断 / 模块没供电 / 模块无上拉电阻\r\n");
    }

    /* 10. 诊断第二步：总线空闲电平 + 全地址扫描 */
    I2C_BusIdleCheck(&scl_lv, &sda_lv);
    UART_Puts("[I2C总线] 空闲电平 SCL(PB6)=");
    UART_PutC((char)('0' + scl_lv));
    UART_Puts(" SDA(PB7)=");
    UART_PutC((char)('0' + sda_lv));
    UART_Puts("  (正常两个都应为 1)\r\n");

    n_dev = I2C_Scan(found, 8U);
    UART_Puts("[I2C总线] 扫描到 ");  UART_Putu(n_dev);  UART_Puts(" 个器件:");
    for (k = 0U; (k < n_dev) && (k < 8U); k++)
    {
        UART_Puts(" 0x");
        UART_PutHex(found[k]);
    }
    UART_Puts("\r\n");

    /* 真实总线上不可能挂几十个器件：扫到一大堆说明 SDA 被拉死或悬空，读到的是假应答 */
    if (n_dev >= 3U)
    {
        UART_Puts("[I2C总线] 可疑：器件数量过多，SDA 可能被拉死/悬空\r\n");
    }

    /* 11. 诊断第三步：读 W25Q64 的 JEDEC ID，确认 SPI Flash 在线。
     *     这一步同时验证了 SPI1 的四个引脚接线和模式配置 ——
     *     如果 ID 读不出来，问题一定在 SPI 侧，和后面记录逻辑无关。 */
    id = W25_ReadID();
    UART_Puts("[W25Q64] JEDEC ID = 0x");
    UART_PutHex((uint8_t)((id >> 16) & 0xFFU));
    UART_PutHex((uint8_t)((id >> 8)  & 0xFFU));
    UART_PutHex((uint8_t)( id        & 0xFFU));
    UART_Puts("\r\n");

    if (((id >> 16) & 0xFFU) == W25_ID_MANU)
    {
        g_spi_ok = 1U;
        UART_Puts("[W25Q64] 识别成功（Winbond，容量 ");
        UART_Putu((uint32_t)1U << (((id & 0xFFU) >= 0x18U) ? 3U : 2U));
        UART_Puts("MB 量级）\r\n");
    }
    else
    {
        g_spi_ok = 0U;
        UART_Puts("[W25Q64] 识别失败！请检查接线：\r\n");
        UART_Puts("           CS->PA4  CLK->PA5  DO->PA6(MISO)  DI->PA7(MOSI)\r\n");
        UART_Puts("           VCC 必须是 3.3V（W25Q64 不耐 5V）\r\n");
        UART_Puts("           若读回 0x000000 或 0xFFFFFF，多半是 DO/DI 接反了\r\n");
    }

    /* 识别成功就闪 3 次，一眼区分于 OLED 的 1/2 次 */
    if (g_spi_ok)
    {
        for (k = 0U; k < 3U; k++)
        {
            LED_ON();   delay_ms(150U);
            LED_OFF();  delay_ms(250U);
        }
    }

    /* 12. 初始化历史记录系统：读两个扇区的头，决定从哪里接着写 */
    W25_RecInit();
    if (g_rec_ok)
    {
        UART_Puts("[记录] 已有 ");
        UART_Putu(g_rec_total);
        UART_Puts(" 条历史记录，本次从扇区 ");
        UART_PutC((g_wr_sector == 0U) ? 'A' : 'B');
        UART_Puts(" 继续写入\r\n");
    }

    /* 12.5 【复核扫描】进入 OLED 初始化之前，再扫一次总线。
     *      和启动时的第一次扫描做对照：
     *        两次都扫到 0x78    -> 器件全程在线，问题出在 OLED_Init 内部
     *        第一次有、这次没了 -> 中间某一步把总线/器件搞挂了（重点排查这一段）
     *      扫描只发地址读应答，不写任何数据，不会改变器件状态。 */
    {
        uint8_t n_dev2 = I2C_Scan(found, 8U);
        UART_Puts("[I2C复核] 进入 OLED_Init 前再扫一次: ");
        UART_Putu(n_dev2);
        UART_Puts(" 个器件:");
        for (k = 0U; (k < n_dev2) && (k < 8U); k++)
        {
            UART_Puts(" 0x");
            UART_PutHex(found[k]);
        }
        UART_Puts("\r\n");
    }

    /* 13. OLED 初始化 + 全屏自检 */
    oled_ok = OLED_Init();

    if (oled_ok)
    {
        UART_Puts("[OLED] 初始化成功，地址 0x");
        UART_PutHex(g_oled_addr);
        UART_Puts("\r\n");

        if (!sda_pull_ok)
        {
            UART_Puts("[OLED] 提醒：SDA 自检没过，这个'成功'可能是假应答\r\n");
        }

        /* 全屏点亮 1 秒：如果这一屏是完整的白色，说明 128x64 共 8192 个
         * 像素全部受控，通信与显存都没问题。若这里全黑而地址又能应答，
         * 问题就在屏幕侧（面板排线松动 / 其实是 SH1106 芯片 / 模块损坏） */
        OLED_FillAll(0xFF);
        delay_ms(1000U);
        OLED_Clear();

        OLED_ShowStr(0, 0, " DHT11 Monitor  ");
        OLED_ShowStr(2, 0, "  v1.2  Ready   ");
        OLED_ShowStr(4, 0, g_spi_ok ? " Flash: OK      " : " Flash: FAIL    ");
        OLED_ShowStr(6, 0, BEEP_TRIG_STR);
    }
    else
    {
        UART_Puts("[OLED] 无应答！请检查：\r\n");
        UART_Puts("        1) SCL->PB6  SDA->PB7 是否接反\r\n");
        UART_Puts("        2) 模块 VCC 是否真的量到 3.3V\r\n");
        UART_Puts("        3) 杜邦线是否断线\r\n");
        UART_Puts("        （没有 OLED 也能继续采集，数据会从串口输出）\r\n");

        /* 失败后才做【接反判别】：用两种引脚映射各探测一次 OLED 地址。
         * 之所以放在失败分支、而不是启动自检里：
         *   交换映射会在两条线上产生"时钟/数据角色颠倒"的时序，
         *   从机看到的是非法波形，可能被卡在"传输中途等待"的状态。
         *   正常启动路径上绝不能跑它，否则会把本来好的器件搞坏。 */
        {
            uint8_t ack_a = I2C_ProbePin(PIN_SCL, PIN_SDA, (uint8_t)(0x3CU << 1));
            uint8_t ack_b = I2C_ProbePin(PIN_SDA, PIN_SCL, (uint8_t)(0x3CU << 1));

            UART_Puts("[接反判别] 映射A SCL=PB");  UART_Putu(PIN_SCL);
            UART_Puts(" SDA=PB");  UART_Putu(PIN_SDA);  UART_Puts(" : ");
            UART_Puts((ack_a == 0U) ? "ACK\r\n" : "无应答\r\n");

            UART_Puts("[接反判别] 映射B SCL=PB");  UART_Putu(PIN_SDA);
            UART_Puts(" SDA=PB");  UART_Putu(PIN_SCL);  UART_Puts(" : ");
            UART_Puts((ack_b == 0U) ? "ACK\r\n" : "无应答\r\n");

            if ((ack_a != 0U) && (ack_b == 0U))
            {
                UART_Puts("[接反判别] >>> 两根线接反了！对调即可 <<<\r\n");
            }
            else if ((ack_a != 0U) && (ack_b != 0U))
            {
                UART_Puts("[接反判别] >>> 不是接反；查 GND 虚接 / 供电 / 模块本身\r\n");
            }
        }
    }

    /* 13.5 ESP8266 初始化：自建热点 + TCP 服务器
     *
     * 【为什么放在主循环之前一次做完】
     *   这一步是阻塞的：要连发 4 条 AT 指令、每条都等模块回 OK，
     *   实测总共约 1~2 秒。放进主循环会把第一次采集拖后，放这里最干净。
     *   失败也不影响主功能 —— 温湿度/OLED/Flash 照常跑，
     *   只是 WiFi 画面显示 FAIL（此时按复位键重来，见本文件 15.6 的说明）。 */
    if (ESP_Init())
    {
        UART_Puts("[ESP] 初始化成功：热点 " ESP_AP_SSID " 已建立\r\n");
        UART_Puts("[ESP] 手机连上该热点后，用 TCP Client 连 192.168.4.1:8080\r\n");
    }
    else
    {
        UART_Puts("[ESP] 初始化失败：WiFi 不可用，其余功能不受影响\r\n");
        UART_Puts("[ESP] 查这三处：TXD->PA3、RXD->PA2（交叉）、CH_PD 是否接 3.3V\r\n");
    }

    UART_Puts("--- 开始采集：每 2 秒一次，每 1 分钟存一条历史 ---\r\n");
    UART_Puts("--- 按 K1(PB1) 切换画面（共 5 个），历史画面内再按则翻页 ---\r\n");
    if (oled_ok) OLED_Refresh();

    /* 14. 主循环：读 DHT11 -> 串口输出 -> 刷新 OLED -> 用剩余时间做 LED/蜂鸣器/按键
     *
     *    关键设计：这 2 秒原来是用 delay_ms(500) 死等的，按键按下去最多要等 2 秒
     *    才有反应，手感很差。现在把 2 秒切成 200 个 10ms 的小步，每步顺手做几件事：
     *      ① 到时间就翻转 LED
     *      ② 扫一次按键
     *      ③ 报警时让蜂鸣器按节拍断续响（不能一直响，太吵）
     *    这样按键的响应延迟只有 10ms 左右，按下去立刻见效。 */

    while (1)
    {
        uint32_t step;
        uint32_t blink_ms;              /* LED 翻转间隔：1Hz / 4Hz / 8Hz */
        uint32_t toggled = 0U;          /* 距离上次翻转过了多少毫秒     */
        uint32_t last_sec;              /* 上一次刷新时间画面时的"秒数" */

        if (DHT11_Read())
        {
            g_dht_ok_cnt++;
            DHT_Process();              /* 解析 + 滑动平均 + 更新极值 + 报警判断 */
            DHT_PrintUart();
            if (oled_ok) OLED_Refresh();

            blink_ms = (g_alarm != 0U) ? 125U : 500U;   /* 报警 4Hz / 正常 1Hz */
        }
        else
        {
            g_dht_fail_cnt++;
            UART_Puts("[DHT11] 读取失败 err=");
            UART_Putu(g_dht_err);
            UART_Puts("（1~3=传感器无应答, 4~5=读位超时, 6=校验和错）\r\n");
            if (g_dht_err == 6U)        /* 校验和错时把原始 5 字节也打出来当证据 */
            {
                UART_Puts("        raw:");
                for (k = 0U; k < 5U; k++) { UART_PutC(' '); UART_PutHex(g_dht_raw[k]); }
                UART_Puts("\r\n");
            }
            if (oled_ok) OLED_Refresh();

            blink_ms = 62U;             /* 失败用 8Hz 超快闪，与报警的 4Hz 区分开 */
        }

        /* ---- 历史记录：每 60 秒往 W25Q64 写一条 ----
         * 为什么定 60 秒而不是跟着 2 秒的采样节奏？
         *   ① Flash 有擦写寿命（W25Q64 约 10 万次/扇区）。按 2 秒一条，
         *      1364 条一个扇区只要 45 分钟就写满，寿命消耗太快；
         *      按 60 秒一条，一个扇区能撑 22 小时以上，实用得多。
         *   ② "历史趋势"本来就该是分钟级的，2 秒的抖动没有记录价值。
         * 用无符号减法比较 g_ms_tick 到上次记录的间隔，
         * 好处是不受 32 位计数溢出影响（溢出后减法结果依然正确）。 */
        if (g_rec_ok && ((g_ms_tick - last_rec_ms) >= 60000U))
        {
            last_rec_ms = g_ms_tick;

            if (W25_RecAppend(g_temp_avg, g_humi_avg))
            {
                UART_Puts("[记录] 第 ");  UART_Putu(g_rec_total);
                UART_Puts(" 条已存入 Flash: T=");  UART_PutFix1(g_temp_avg);
                UART_Puts("C H=");  UART_PutFix1(g_humi_avg);  UART_Puts("%\r\n");

                /* 每写 W25_VERIFY_EVERY 条，实地重扫一遍 Flash，和"增量计数"对一下账。
                 * 两者不一致就说明寻址或乒乓逻辑有问题，早发现早好。
                 * 代价约 40ms 每 100 分钟，可以忽略。 */
                if ((g_rec_total % W25_VERIFY_EVERY) == 0U)
                {
                    uint32_t inc_total = g_rec_total;

                    W25_RefreshCounts();
                    if (g_rec_total == inc_total)
                    {
                        UART_Puts("[记录] 自检通过：增量计数与实地扫描一致 (");
                        UART_Putu(g_rec_total);  UART_Puts(" 条)\r\n");
                    }
                    else
                    {
                        UART_Puts("[记录] 自检不一致！增量=");  UART_Putu(inc_total);
                        UART_Puts(" 实地=");  UART_Putu(g_rec_total);
                        UART_Puts("（以实地为准，已修正缓存）\r\n");
                    }
                }

                /* 如果正在看历史画面，写完立刻跳到最后一页，能当场看到新记录 */
                if ((g_mode == 3U) && oled_ok)
                {
                    uint32_t show  = (g_rec_total > HIST_MAX) ? HIST_MAX : g_rec_total;
                    uint32_t pages = (show + HIST_PER_PAGE - 1U) / HIST_PER_PAGE;

                    g_hist_page = (uint8_t)((pages > 0U) ? (pages - 1U) : 0U);
                    OLED_PageHistory();
                }
            }
            else
            {
                UART_Puts("[记录] 写入失败（Flash 查忙超时或未就绪）\r\n");
            }
        }

        /* ---- 往手机客户端推一帧数据 ----
         * 每个采样周期（2 秒）推一帧。整段约 300ms（实测 '>' 提示符 210ms，
         * 再加等 SEND OK），属于阻塞操作，所以只在"确实有客户端"时才推；
         * 没有客户端时直接跳过，不白等超时。
         * "连着但已经发不出去"由 ESP_SendFrame 内部的 link is not valid
         * 补判处理，它会自动把 g_esp_link 清 0，下一轮就不再来打扰。 */
        if ((g_esp_ready != 0U) && (g_esp_link != 0U))
        {
            (void)ESP_SendFrame();

            /* 正停在 WiFi 画面上就顺手刷新一次，能当场看到 TX 计数在涨 */
            if ((oled_ok != 0U) && (g_mode == 4U)) OLED_PageWifi();
        }

        /* 采样间隔 2 秒 = 200 步 x 10ms。这期间按键随时可响应 */
        last_sec = g_ms_tick / 1000U;
        for (step = 0U; step < 200U; step++)
        {
            delay_ms(10U);
            toggled += 10U;

            /* ESP8266 的日常维护：收模块上报（0,CONNECT / 0,CLOSED / +IPD）
             * + 30 秒心跳自恢复。全程非阻塞，插在这里不影响按键响应。 */
            ESP_RxService();

            if (toggled >= blink_ms)            /* 该翻转 LED 了 */
            {
                LED_TOGGLE();
                toggled = 0U;
            }

            /* ---- 蜂鸣器断续报警 ----
             * 报警时以 1Hz 通断（响 200ms、停 800ms）；不报警就保持静音。
             * 为什么不让它一直响？一是吵，二是"断续"这个动作本身就在传递信息：
             * 光看灯要盯着，听声音不用看就知道出事了。
             * 这里借 g_ms_tick 的 200ms 相位来判断，不需要额外变量。 */
            if (g_alarm != 0U)
            {
                if ((g_ms_tick % 1000U) < 200U) BEEP_ON();
                else                            BEEP_OFF();
            }
            else
            {
                BEEP_OFF();
            }

            /* 时间画面：每秒重绘一次那两行，让秒位跳动起来（只画 2 行，很快） */
            if ((g_mode == 1U) && (oled_ok))
            {
                uint32_t sec = g_ms_tick / 1000U;

                if (sec != last_sec)
                {
                    last_sec = sec;
                    OLED_PageTime();
                }
            }

            /* ---------------- 按键处理 ----------------
             *   短按：切到下一个画面；但在历史记录画面上是"往后翻一页"
             *   长按：不管当前在哪个画面，直接回到第 1 个画面（温湿度）
             *
             * 【为什么长按是必需的，不是锦上添花】
             *   历史画面上短按被用来翻页了，如果按键只有短按一种动作，
             *   用户一旦按进历史画面就再也退不出来，只能按复位键 ——
             *   等于把这个功能做成了单向门。
             *   长按就是那道逃生门，在任何画面都有效。 */
            {
                uint8_t k = Key_Scan();

                if (k == 2U)                    /* ---------- 长按：回主画面 ---------- */
                {
                    g_mode      = 0U;           /* 回到温湿度画面 */
                    g_hist_page = 0U;           /* 页号也复位，下次进历史从头算 */

                    if (oled_ok) OLED_Refresh();

                    UART_Puts("[按键] 长按(>=1s) -> 回到画面 1/4（温湿度）\r\n");

                    /* 长按给"双闪"反馈，和短按的单闪区分开：
                     * 不然用户不知道自己刚才那一下算短按还是长按 */
                    LED_ON();   delay_ms(60U);  LED_OFF();  delay_ms(80U);
                    LED_ON();   delay_ms(60U);  LED_OFF();
                }
                else if (k == 1U)               /* ---------- 短按 ---------- */
                {
                    /* 历史画面：按键用来翻页，不切画面。
                     * 这样看历史时可以一页页往后翻，不用退出重进。
                     * 翻到最后一页再按就回到第 0 页。
                     * 页数按 HIST_MAX 算（只展示最近的那些），
                     * 而不是按 g_rec_total —— 否则页号会超出 uint8 范围。 */
                    if (g_mode == 3U)
                    {
                        uint32_t show = (g_rec_total > HIST_MAX) ? HIST_MAX : g_rec_total;
                        uint32_t pages = (show + HIST_PER_PAGE - 1U) / HIST_PER_PAGE;

                        if (pages == 0U) pages = 1U;
                        g_hist_page++;
                        if ((uint32_t)g_hist_page >= pages) g_hist_page = 0U;

                        if (oled_ok) OLED_PageHistory();

                        UART_Puts("[按键] 短按 -> 历史翻页：第 ");
                        UART_Putu((uint32_t)g_hist_page + 1U);
                        UART_Puts("/");  UART_Putu(pages);
                        UART_Puts(" 页\r\n");
                    }
                    else
                    {
                        g_mode++;
                        if (g_mode >= MODE_NUM) g_mode = 0U;
                        if (g_mode == 3U)
                        {
                            /* 刚进历史画面：直接跳到最后一页（最新的那几条），
                             * 这比从最旧的一条看起有用得多 */
                            uint32_t show = (g_rec_total > HIST_MAX) ? HIST_MAX : g_rec_total;
                            uint32_t pages = (show + HIST_PER_PAGE - 1U) / HIST_PER_PAGE;

                            g_hist_page = (uint8_t)((pages > 0U) ? (pages - 1U) : 0U);

                            /* 把"这一页显示第几页/共几页/共几条"打到串口，
                             * 方便和屏幕内容对照（也便于远程判断显示是否正常） */
                            UART_Puts("[历史] 进入画面：第 ");
                            UART_Putu((uint32_t)g_hist_page + 1U);
                            UART_Puts("/");  UART_Putu(pages);
                            UART_Puts(" 页，共 ");  UART_Putu(g_rec_total);
                            UART_Puts(" 条记录（每页 3 条）\r\n");
                        }
                        if (oled_ok) OLED_Refresh();   /* 立刻重绘，不等下一个采样周期 */

                        UART_Puts("[按键] 短按 -> 画面 ");
                        UART_Putu((uint32_t)g_mode + 1U);
                        UART_Puts("/");  UART_Putu(MODE_NUM);
                        UART_Puts("\r\n");
                    }

                    LED_ON();                       /* 单闪：按键已响应 */
                    delay_ms(30U);
                    LED_OFF();
                }
            }
        }
    }

}


