#ifndef BSP_CPU_H
#define BSP_CPU_H
/* =============================================================
 * bsp_cpu.h —— Cortex-M3 内核级原语（PRIMASK 开关中断）
 *
 * 为什么需要它：DHT11 靠「数时间长短」区分 0 和 1，一旦被中断打断，
 * 采样点就会被推迟，可能把 1 读成 0 —— 所以那几毫秒必须关中断。
 * ============================================================= */
#include <stdint.h>
/* 关/开全局中断。cpsid i / cpsie i 直接改动 PRIMASK 寄存器。 */
static inline void Irq_Disable(void) { __asm volatile ("cpsid i" ::: "memory"); }
static inline void Irq_Enable(void)  { __asm volatile ("cpsie i" ::: "memory"); }
#endif /* BSP_CPU_H */
