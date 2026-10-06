#ifndef BSP_GPIO_H
#define BSP_GPIO_H
/* =============================================================
 * bsp_gpio.h —— GPIO 初始化 + 板载 LED + 按键 + 通用引脚读写
 *
 * 驱动层需要操作引脚时（如蜂鸣器），一律通过本模块的
 * BSP_Pin_Write / BSP_Pin_Read，不得直接接触寄存器。
 * ============================================================= */
#include <stdint.h>
#define BSP_PORT_A  0U
#define BSP_PORT_B  1U
#define BSP_PORT_C  2U
/* 引脚配置值（nibble = CNF<<2 | MODE，这是本工程踩过的最大的坑）
 *   【位3位2 = CNF(输出形式)，位1位0 = MODE(模式/速度)】—— 顺序绝不可记反 */
#define BSP_MODE_IN_FLOAT    0x4U   /* 浮空输入       CNF=01 MODE=00 */
#define BSP_MODE_IN_PULL     0x8U   /* 上拉/下拉输入  CNF=10 MODE=00 */
#define BSP_MODE_OUT_PP_2M   0x2U   /* 通用推挽 2MHz  CNF=00 MODE=10 */
#define BSP_MODE_OUT_PP_50M  0x3U   /* 通用推挽 50MHz CNF=00 MODE=11 */
#define BSP_MODE_OUT_OD_50M  0x7U   /* 通用开漏 50MHz CNF=01 MODE=11 */
#define BSP_MODE_AF_PP_50M   0xBU   /* 复用推挽 50MHz CNF=10 MODE=11 */
void GPIO_Init(void);
void    BSP_Pin_SetMode(uint8_t port, uint8_t pin, uint8_t cfg);
/* 通用引脚读写（内部用 BSRR，写操作是原子的，不会被中断打断） */
void    BSP_Pin_Write(uint8_t port, uint8_t pin, uint8_t level);
uint8_t BSP_Pin_Read(uint8_t port, uint8_t pin);
/* 板载 LED：PC13 低电平点亮（阳极接 3.3V）。若板子相反，改实现即可 */
void LED_On(void);
void LED_Off(void);
void LED_Toggle(void);
/* 按键 K1（PB1，上拉输入）：0 = 按下，1 = 松开 */
uint8_t BSP_Key_Read(void);
/* I2C 两个引脚的角色（正常 PB6=SCL / PB7=SDA）。
 * 交换映射只用于诊断「模块的 SCL/SDA 是不是被接反了」。 */
#define I2C_SWAP_DIAG   0U
#if (I2C_SWAP_DIAG != 0U)
  #define PIN_SCL         7U
  #define PIN_SDA         6U
#else
  #define PIN_SCL         6U
  #define PIN_SDA         7U
#endif
#endif /* BSP_GPIO_H */
