#ifndef SSD1306_H
#define SSD1306_H
/* =============================================================
 * ssd1306.h —— OLED 驱动（SSD1306 控制器，128x64 像素，软件 I2C）
 * ============================================================= */
#include <stdint.h>
#define OLED_ADDR_1     0x78U    /* 7 位地址 0x3C 左移一位，最常见 */
#define OLED_ADDR_2     0x7AU    /* 少数模块是 0x3D，即 0x7A      */
#define CH_DEGREE       0x7FU    /* 借用 ASCII 127(DEL) 当「度数符号」的代号 */
uint8_t OLED_Addr(void);         /* 探测成功后保存的实际地址（0x78 / 0x7A） */
uint8_t OLED_Init(void);
uint8_t OLED_Probe(uint8_t addr);
void    OLED_SetPos(uint8_t page, uint8_t col);
void    OLED_FillPage(uint8_t page, uint8_t dat);
void    OLED_FillAll(uint8_t dat);
void    OLED_Clear(void);
/* 整屏只有 4 行字符位置：page 0 / 2 / 4 / 6（字库 8x16 占两个 page） */
void    OLED_ShowChar(uint8_t page, uint8_t col, char ch);
void    OLED_ShowStr(uint8_t page, uint8_t col, const char *str);
#endif /* SSD1306_H */
