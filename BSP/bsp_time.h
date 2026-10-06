#ifndef BSP_TIME_H
#define BSP_TIME_H
/* =============================================================
 * bsp_time.h —— 时间基准（两层用途，不要混）
 *   SysTick  : delay_us / delay_ms 的「秒表」，用完就关，一次性
 *   TIM2 1ms : 系统「墙上时间」，一直在跑，从不被打扰
 * ============================================================= */
#include <stdint.h>
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);
void SysTick_Init(void);
void TIM2_Init(void);
void Time_Get(uint32_t *hh, uint32_t *mm, uint32_t *ss);
/* 开机以来的毫秒数（只读），等价于 Arduino 的 millis()。
 * 变量本体是 bsp_time.c 的 static volatile，只在 TIM2 中断里累加；
 * 外部一律用本函数读取，保证时基只有一个写入点。 */
uint32_t BSP_Millis(void);
#endif /* BSP_TIME_H */
