#ifndef BSP_REG_H
#define BSP_REG_H
/* =============================================================
 * bsp_reg.h —— STM32F103 寄存器地址定义（本工程唯一允许出现
 * 「基地址 + 偏移量」的地方，由 BSP 层独占）
 *
 * 分层约束：Drivers / Core / Util 三层**不得** include 本头文件，
 * 由 tools/check_layering.py 在构建时强制校验。
 * ============================================================= */
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
/* ---- 本工程补充用到的寄存器（原文件未定义，重构时补齐） ---- */
#define GPIOB_CRH       (*(volatile uint32_t *)0x40010C04U)   /* 端口配置高（引脚8-15） */
#define GPIOC_CRL       (*(volatile uint32_t *)0x40011000U)   /* 端口配置低（引脚0-7）  */
#define GPIOC_IDR       (*(volatile uint32_t *)0x40011008U)   /* 输入数据 +0x08 */
#define GPIOC_BSRR      (*(volatile uint32_t *)0x40011010U)   /* 位设置/清除 +0x10 */
#endif /* BSP_REG_H */
