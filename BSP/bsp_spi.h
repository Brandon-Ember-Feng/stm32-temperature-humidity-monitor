#ifndef BSP_SPI_H
#define BSP_SPI_H
/* =============================================================
 * bsp_spi.h —— SPI1 外设（W25Q64 用，PA4=CS / PA5=SCK / PA6=MISO / PA7=MOSI）
 * 模式 0（CPOL=0 CPHA=0），时钟 4MHz
 * ============================================================= */
#include <stdint.h>
void    SPI1_Init(void);
uint8_t SPI1_SwapByte(uint8_t dat);
/* 软件片选：一条 SPI 总线可挂多个从机，硬件 NSS 只有一个，不够用 */
void    SPI1_CS_Low(void);
void    SPI1_CS_High(void);
#endif /* BSP_SPI_H */
