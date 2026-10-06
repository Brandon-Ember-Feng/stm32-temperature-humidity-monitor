#include "bsp_time.h"
#include "bsp_reg.h"
#include "bsp_rcc.h"
/* ==========================================================================
 * 3. 延时：用内核自带的 SysTick 做 1us 基准，比"for 空循环"准得多
 * ========================================================================== */
void SysTick_Init(void)
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

    reload = us * BSP_CpuMhz();               /* 1us 需要 BSP_CpuMhz() 个内核时钟 */
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

void TIM2_Init(void)
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
void Time_Get(uint32_t *hh, uint32_t *mm, uint32_t *ss)
{
    uint32_t t = BSP_Millis() / 1000U;      /* 先换算成"秒" */

    *ss = t % 60U;
    t  /= 60U;
    *mm = t % 60U;
    t  /= 60U;
    *hh = t % 24U;                       /* 满 24 小时回零（没有日历，只显示时:分:秒） */
}

/* --------------------------------------------------------------------------
 * 系统时基的只读出口，语义等价于 Arduino 的 millis()。
 *
 * 为什么不让上层直接读写 g_ms_tick 这个变量？
 *   ① 它只在 TIM2 中断里累加，是「单纯增」的；一旦别处也能写，
 *      整个系统的墙上时间立刻失去意义；
 *   ② 必须 volatile + static 才能让编译器确信「没人从外部改它」，
 *      从而在中断外的读取处老老实实每次从内存取。
 * 上层一律用 BSP_Millis()，配合无符号减法比较时间差（天然处理溢出）。
 * -------------------------------------------------------------------------- */
uint32_t BSP_Millis(void)
{
    return g_ms_tick;
}
