#ifndef BSP_RCC_H
#define BSP_RCC_H
#include <stdint.h>
/* 系统时钟：HSI 8MHz /2 -> 4MHz，再 x16 -> 64MHz */
void Clock_Init(void);
/* 当前内核主频（MHz），只读。串口 BRR 与 SysTick 延时都按它换算。
 * 变量本体是 bsp_rcc.c 的 static，对外只暴露取值的口子 */
uint32_t BSP_CpuMhz(void);
#endif /* BSP_RCC_H */
