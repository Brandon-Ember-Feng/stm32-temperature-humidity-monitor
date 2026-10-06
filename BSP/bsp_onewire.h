#ifndef BSP_ONEWIRE_H
#define BSP_ONEWIRE_H
/* =============================================================
 * bsp_onewire.h —— 单总线（DHT11 数据线）的底层线操作
 *
 * 为什么单独成层：单总线的时间判据精度在微秒级，寄存器读写必须
 * 留在 BSP；驱动层（dht11.c）只负责协议流程，不碰寄存器。
 * 引脚：PB0
 * ============================================================= */
#include <stdint.h>
void    OW_PinOut(void);   /* PB0 -> 推挽输出（主动拉低）      */
void    OW_PinIn(void);    /* PB0 -> 上拉输入（松手，靠上拉回高）*/
uint8_t OW_Read(void);     /* 读 PB0 实际电平                   */
uint8_t OW_WaitLevel(uint8_t want);  /* 等电平，1=等到 0=超时   */
#define OW_TIMEOUT  0x00080000U
#endif /* BSP_ONEWIRE_H */
