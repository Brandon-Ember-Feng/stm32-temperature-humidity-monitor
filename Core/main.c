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
#include "bsp_rcc.h"
#include "bsp_time.h"
#include "bsp_gpio.h"
#include "bsp_soft_i2c.h"
#include "bsp_spi.h"
#include "bsp_uart.h"
#include "ssd1306.h"
#include "w25q64.h"
#include "hist_store.h"
#include "dht11.h"
#include "beep.h"
#include "key.h"
#include "esp8266_at.h"
#include "app.h"
#include "ui_pages.h"
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
    LED_On();
    delay_ms(1500U);
    LED_Off();
    delay_ms(200U);

    UART_Puts("\r\n=== 温湿度监测仪 v1.2（DHT11 + OLED + W25Q64 + 蜂鸣器 + ESP8266 WiFi）===\r\n");
    UART_Puts("CPU 主频: ");  UART_Putu(BSP_CpuMhz());  UART_Puts(" MHz\r\n");

    /* 8.5 蜂鸣器自检：短鸣 200ms
     *
     *    为什么上电就要叫一声？——"触发极性对不对"这件事，如果等到
     *    温度升到 30°C 触发报警才知道，可能要等半小时。
     *    短鸣一下，5 秒内就能定案（判据见第 13 章注释）：
     *        短鸣后安静  -> 极性与接线都正确，OK
     *        一直长鸣    -> 极性反了，把 BEEP_ACTIVE_HIGH 取反重烧
     *        完全没声    -> 查 VCC/GND 是否接反、是否量到 3.3V */
    BEEP_On();
    delay_ms(200U);
    BEEP_Off();
    UART_Puts("[自检] 蜂鸣器短鸣 200ms（配置为");
    UART_Puts(BEEP_TRIG_TXT);
    UART_Puts("）：若此后一直长鸣不停，说明模块是另一种触发极性\r\n");

    /* 9. 诊断第一步：SDA 回读自检 —— 直接回答"OLED 的线到底接上没有"。
     *    LED 编码：通过闪 2 次；不通过闪 1 次 */
    sda_pull_ok = I2C_SdaEchoTest();
    for (k = 0U; k < (sda_pull_ok ? 2U : 1U); k++)
    {
        LED_On();   delay_ms(250U);
        LED_Off();  delay_ms(400U);
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
    id = W25_Probe();                       /* 读 ID + 在驱动内部判定是否识别成功 */
    UART_Puts("[W25Q64] JEDEC ID = 0x");
    UART_PutHex((uint8_t)((id >> 16) & 0xFFU));
    UART_PutHex((uint8_t)((id >> 8)  & 0xFFU));
    UART_PutHex((uint8_t)( id        & 0xFFU));
    UART_Puts("\r\n");

    if (W25_IsOk())                         /* 结论由驱动给，上层不再自己判定/写标志 */
    {
        UART_Puts("[W25Q64] 识别成功（Winbond，容量 ");
        UART_Putu((uint32_t)1U << (((id & 0xFFU) >= 0x18U) ? 3U : 2U));
        UART_Puts("MB 量级）\r\n");
    }
    else
    {
        UART_Puts("[W25Q64] 识别失败！请检查接线：\r\n");
        UART_Puts("           CS->PA4  CLK->PA5  DO->PA6(MISO)  DI->PA7(MOSI)\r\n");
        UART_Puts("           VCC 必须是 3.3V（W25Q64 不耐 5V）\r\n");
        UART_Puts("           若读回 0x000000 或 0xFFFFFF，多半是 DO/DI 接反了\r\n");
    }

    /* 识别成功就闪 3 次，一眼区分于 OLED 的 1/2 次 */
    if (W25_IsOk())
    {
        for (k = 0U; k < 3U; k++)
        {
            LED_On();   delay_ms(150U);
            LED_Off();  delay_ms(250U);
        }
    }

    /* 12. 初始化历史记录系统：读两个扇区的头，决定从哪里接着写 */
    W25_RecInit();
    if (Hist_IsReady())
    {
        UART_Puts("[记录] 已有 ");
        UART_Putu(Hist_Count());
        UART_Puts(" 条历史记录，本次从扇区 ");
        UART_PutC((Hist_WrSector() == 0U) ? 'A' : 'B');
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
        UART_PutHex(OLED_Addr());
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
        OLED_ShowStr(4, 0, W25_IsOk() ? " Flash: OK      " : " Flash: FAIL    ");
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
            DHT_Process();              /* 解析 + 滑动平均 + 更新极值 + 报警判断 */
            DHT_PrintUart();
            if (oled_ok) OLED_Refresh();

            blink_ms = (g_alarm != 0U) ? 125U : 500U;   /* 报警 4Hz / 正常 1Hz */
        }
        else
        {
            UART_Puts("[DHT11] 读取失败 err=");
            UART_Putu(DHT_Error());
            UART_Puts("（1~3=传感器无应答, 4~5=读位超时, 6=校验和错）\r\n");
            if (DHT_Error() == 6U)        /* 校验和错时把原始 5 字节也打出来当证据 */
            {
                UART_Puts("        raw:");
                for (k = 0U; k < 5U; k++) { UART_PutC(' '); UART_PutHex(DHT_RawByte(k)); }
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
         * 用无符号减法比较 BSP_Millis() 到上次记录的间隔，
         * 好处是不受 32 位计数溢出影响（溢出后减法结果依然正确）。 */
        if (Hist_IsReady() && ((BSP_Millis() - last_rec_ms) >= 60000U))
        {
            last_rec_ms = BSP_Millis();

            if (W25_RecAppend(g_temp_avg, g_humi_avg))
            {
                UART_Puts("[记录] 第 ");  UART_Putu(Hist_Count());
                UART_Puts(" 条已存入 Flash: T=");  UART_PutFix1(g_temp_avg);
                UART_Puts("C H=");  UART_PutFix1(g_humi_avg);  UART_Puts("%\r\n");

                /* 每写 W25_VERIFY_EVERY 条，实地重扫一遍 Flash，和"增量计数"对一下账。
                 * 两者不一致就说明寻址或乒乓逻辑有问题，早发现早好。
                 * 代价约 40ms 每 100 分钟，可以忽略。 */
                if ((Hist_Count() % W25_VERIFY_EVERY) == 0U)
                {
                    uint32_t inc_total = Hist_Count();

                    W25_RefreshCounts();
                    if (Hist_Count() == inc_total)
                    {
                        UART_Puts("[记录] 自检通过：增量计数与实地扫描一致 (");
                        UART_Putu(Hist_Count());  UART_Puts(" 条)\r\n");
                    }
                    else
                    {
                        UART_Puts("[记录] 自检不一致！增量=");  UART_Putu(inc_total);
                        UART_Puts(" 实地=");  UART_Putu(Hist_Count());
                        UART_Puts("（以实地为准，已修正缓存）\r\n");
                    }
                }

                /* 如果正在看历史画面，写完立刻跳到最后一页，能当场看到新记录 */
                if ((g_mode == 3U) && oled_ok)
                {
                    uint32_t show  = (Hist_Count() > HIST_MAX) ? HIST_MAX : Hist_Count();
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
         * 补判处理，它会自动把 ESP_IsLinked() 清 0，下一轮就不再来打扰。 */
        if ((ESP_IsReady() != 0U) && (ESP_IsLinked() != 0U))
        {
            (void)ESP_SendFrame(g_temp_avg, g_humi_avg, Hist_Count(), g_alarm);

            /* 正停在 WiFi 画面上就顺手刷新一次，能当场看到 TX 计数在涨 */
            if ((oled_ok != 0U) && (g_mode == 4U)) OLED_PageWifi();
        }

        /* 采样间隔 2 秒 = 200 步 x 10ms。这期间按键随时可响应 */
        last_sec = BSP_Millis() / 1000U;
        for (step = 0U; step < 200U; step++)
        {
            delay_ms(10U);
            toggled += 10U;

            /* ESP8266 的日常维护：收模块上报（0,CONNECT / 0,CLOSED / +IPD）
             * + 30 秒心跳自恢复。全程非阻塞，插在这里不影响按键响应。 */
            ESP_RxService();

            if (toggled >= blink_ms)            /* 该翻转 LED 了 */
            {
                LED_Toggle();
                toggled = 0U;
            }

            /* ---- 蜂鸣器断续报警 ----
             * 报警时以 1Hz 通断（响 200ms、停 800ms）；不报警就保持静音。
             * 为什么不让它一直响？一是吵，二是"断续"这个动作本身就在传递信息：
             * 光看灯要盯着，听声音不用看就知道出事了。
             * 这里借 BSP_Millis() 的 200ms 相位来判断，不需要额外变量。 */
            if (g_alarm != 0U)
            {
                if ((BSP_Millis() % 1000U) < 200U) BEEP_On();
                else                            BEEP_Off();
            }
            else
            {
                BEEP_Off();
            }

            /* 时间画面：每秒重绘一次那两行，让秒位跳动起来（只画 2 行，很快） */
            if ((g_mode == 1U) && (oled_ok))
            {
                uint32_t sec = BSP_Millis() / 1000U;

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
                    LED_On();   delay_ms(60U);  LED_Off();  delay_ms(80U);
                    LED_On();   delay_ms(60U);  LED_Off();
                }
                else if (k == 1U)               /* ---------- 短按 ---------- */
                {
                    /* 历史画面：按键用来翻页，不切画面。
                     * 这样看历史时可以一页页往后翻，不用退出重进。
                     * 翻到最后一页再按就回到第 0 页。
                     * 页数按 HIST_MAX 算（只展示最近的那些），
                     * 而不是按 Hist_Count() —— 否则页号会超出 uint8 范围。 */
                    if (g_mode == 3U)
                    {
                        uint32_t show = (Hist_Count() > HIST_MAX) ? HIST_MAX : Hist_Count();
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
                            uint32_t show = (Hist_Count() > HIST_MAX) ? HIST_MAX : Hist_Count();
                            uint32_t pages = (show + HIST_PER_PAGE - 1U) / HIST_PER_PAGE;

                            g_hist_page = (uint8_t)((pages > 0U) ? (pages - 1U) : 0U);

                            /* 把"这一页显示第几页/共几页/共几条"打到串口，
                             * 方便和屏幕内容对照（也便于远程判断显示是否正常） */
                            UART_Puts("[历史] 进入画面：第 ");
                            UART_Putu((uint32_t)g_hist_page + 1U);
                            UART_Puts("/");  UART_Putu(pages);
                            UART_Puts(" 页，共 ");  UART_Putu(Hist_Count());
                            UART_Puts(" 条记录（每页 3 条）\r\n");
                        }
                        if (oled_ok) OLED_Refresh();   /* 立刻重绘，不等下一个采样周期 */

                        UART_Puts("[按键] 短按 -> 画面 ");
                        UART_Putu((uint32_t)g_mode + 1U);
                        UART_Puts("/");  UART_Putu(MODE_NUM);
                        UART_Puts("\r\n");
                    }

                    LED_On();                       /* 单闪：按键已响应 */
                    delay_ms(30U);
                    LED_Off();
                }
            }
        }
    }

}
