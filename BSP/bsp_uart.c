#include "bsp_uart.h"
#include "bsp_reg.h"
#include "bsp_rcc.h"
#include "fixed_str.h"
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
void UART_Init(void)
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
     *     USART1_BRR = (BSP_CpuMhz() * 1000000U * 16U) / 115200U;
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
    USART1_BRR = ((BSP_CpuMhz() * 1000000U) + (115200U / 2U)) / 115200U;

    USART1_CR1 = (1U << 13)      /* UE：串口使能      */
               | (1U << 3)       /* TE：发送使能      */
               | (1U << 2);      /* RE：接收使能      */
}

void UART_PutC(char c)
{
    while ((USART1_SR & (1U << 7)) == 0U);   /* TXE=1 表示发送寄存器空了 */
    USART1_DR = (uint32_t)c;
}

void UART_Puts(const char *s)
{
    while (*s != '\0')
    {
        UART_PutC(*s);
        s++;
    }
}

void UART_Putu(uint32_t v)
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

void UART_PutHex(uint8_t v)
{
    const char *hex = "0123456789ABCDEF";

    UART_PutC(hex[(v >> 4) & 0x0FU]);
    UART_PutC(hex[v & 0x0FU]);
}

/* 打印"带一位小数的数值"。参数是放大 10 倍后的整数：256 表示 25.6
 * 为什么要放大 10 倍存？因为单片机里能省则省，用整数运算代替浮点，
 * 速度更快、也不用把浮点库链接进来（浮点库会让 Flash 占用大一圈）。 */
void UART_PutFix1(int16_t v10)
{
    uint16_t a;

    if (v10 < 0) { UART_PutC('-'); a = (uint16_t)(-v10); }
    else         { a = (uint16_t)v10; }

    UART_Putu((uint32_t)(a / 10U));       /* 整数部分 */
    UART_PutC('.');
    UART_PutC((char)('0' + (a % 10U)));   /* 小数部分 */
}
/* =============================================================
 * USART2：ESP-01S 的物理层（收发 + 接收中断 + 接收缓冲）
 * ============================================================= */
static char     g_uart2_rx[BSP_UART2_RX_BUF];   /* 模块应答 / 主动上报的累积缓冲 */
static uint16_t g_uart2_rx_n;                   /* 里面已有多少字节             */
static uint16_t g_uart2_scan;                   /* 关键词已扫到哪个位置         */

/* --------------------------------------------------------------------------
 * 接收缓冲的访问接口。变量本体 static，只有下面 5 个口子能进出：
 * 读长度、读某一字节、读/移游标、整体复位。
 * 这样「缓冲由中断写、协议层读」这条规则是代码强制保证的，不靠注释约束人。
 * -------------------------------------------------------------------------- */
uint16_t BSP_Uart2_RxLen(void)
{
    return g_uart2_rx_n;
}

char BSP_Uart2_RxAt(uint16_t idx)
{
    /* 越界返回 '\0'：缓冲始终保持 NUL 结尾，所以协议层"多读几个字符"
     * 去匹配关键词时，自然会撞上 '\0' 而停下，不需要自己判边界。 */
    return (idx < BSP_UART2_RX_BUF) ? g_uart2_rx[idx] : '\0';
}

uint16_t BSP_Uart2_ScanPos(void)
{
    return g_uart2_scan;
}

void BSP_Uart2_ScanMove(uint16_t pos)
{
    g_uart2_scan = pos;
}

/* 缓冲 + 游标一起复位。二者必须同时清：只清缓冲不清游标，协议层会从旧位置
 * 继续往后扫，把新收到的一整批数据全部跳过（症状：断线重连后再也不认 CONNECT）。
 * 调用方需在关中断状态下调用，否则接收中断可能插在中间造成计数与内容错位。 */
void BSP_Uart2_RxReset(void)
{
    g_uart2_rx_n  = 0U;
    g_uart2_scan  = 0U;
    g_uart2_rx[0] = '\0';
}

void BSP_Uart2_PutC(char c)
{
    while ((USART2_SR & (1U << 7)) == 0U);   /* TXE=1 表示发送寄存器空了 */
    USART2_DR = (uint32_t)c;
}

void BSP_Uart2_Puts(const char *s)
{
    while (*s != '\0')
    {
        BSP_Uart2_PutC(*s);
        s++;
    }
}

void BSP_Uart2_Init(void)
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
    USART2_BRR = (((BSP_CpuMhz() * 1000000U) / 2U) + (BSP_UART2_BAUD / 2U)) / BSP_UART2_BAUD;

    USART2_CR1 = (1U << 13)      /* UE：串口使能     */
               | (1U << 3)       /* TE：发送使能     */
               | (1U << 2)       /* RE：接收使能     */
               | (1U << 5);      /* RXNEIE：收到一个字节就中断（见下方长注释）*/

    /* 【为什么必须开接收中断，而不能靠主循环轮询】
     *   115200 波特率下，一个字节只占 10 位 = 约 87us。
     *   而主循环里 BSP_Uart2_RxPoll 是每 10ms 才轮到一次（外层 200 步 x 10ms），
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

        if (g_uart2_rx_n < (BSP_UART2_RX_BUF - 1U))
        {
            g_uart2_rx[g_uart2_rx_n] = c;
            g_uart2_rx_n++;
        }
        else
        {
            /* 缓冲满了整体丢掉重来。正常不会走到这里；真出现说明有大量
             * 我们没处理的数据（比如客户端一直往我们发），丢掉比卡死好。 */
            g_uart2_rx_n = 0U;
            g_uart2_scan = 0U;
            g_uart2_rx[0] = c;
            g_uart2_rx_n  = 1U;
        }
        g_uart2_rx[g_uart2_rx_n] = '\0';            /* 始终保持 NUL 结尾 */
    }
}

/* 把 USART2 收到的字节收进缓冲（非阻塞：只取当前已到的，立刻返回）
 *
 * 注意：自 2026-09-21 起，字节主要由 USART2_IRQHandler 在中断里搬走，
 * 这个函数降级为"安全网"——万一某个字节在中断打开之前就到了 DR，
 * 或者中断被长时间屏蔽过，这里还能补收。留着不占什么开销。 */
void BSP_Uart2_RxPoll(void)
{
    uint16_t guard = 0U;

    while ((USART2_SR & (1U << 5)) != 0U)      /* RXNE=1 表示收到一个字节 */
    {
        char c = (char)(USART2_DR & 0xFFU);    /* 读 DR 同时清掉 RXNE */

        if (g_uart2_rx_n < (BSP_UART2_RX_BUF - 1U))
        {
            g_uart2_rx[g_uart2_rx_n] = c;
            g_uart2_rx_n++;
        }
        else
        {
            /* 缓冲满了就整体丢掉重来。正常情况不会走到这里；
             * 真出现说明有大量我们没有处理的数据（比如客户端一直往我们发），
             * 丢掉比卡死好。 */
            g_uart2_rx_n  = 0U;
            g_uart2_scan  = 0U;
        }

        if (++guard >= 300U) break;            /* 防止持续有数据时一直不返回 */
    }
    g_uart2_rx[g_uart2_rx_n] = '\0';
}
