#ifndef BSP_SOFT_I2C_H
#define BSP_SOFT_I2C_H
/* =============================================================
 * bsp_soft_i2c.h —— 软件模拟 I2C（时序靠 GPIO 手动翻转）
 *
 * 为什么用软件 I2C 而不是硬件 I2C：任意引脚可复用、无总线死锁风险、
 * 波形可抓。代价是速率上限约 200kHz 且占用 CPU。
 * ============================================================= */
#include <stdint.h>
/* ---- I2C 基本时序 ---- */
void    I2C_Start(void);
void    I2C_Stop(void);
void    I2C_SendByte(uint8_t dat);
uint8_t I2C_WaitAck(void);
/* ---- 总线诊断（自检 / 故障定位用） ---- */
void    I2C_BusIdleCheck(uint8_t *scl_lv, uint8_t *sda_lv);
uint8_t I2C_SdaEchoTest(void);
uint8_t I2C_ProbePin(uint8_t scl_bit, uint8_t sda_bit, uint8_t addr8);
uint8_t I2C_Scan(uint8_t *found, uint8_t max);
#endif /* BSP_SOFT_I2C_H */
