#ifndef W25Q64_H
#define W25Q64_H
/* =============================================================
 * w25q64.h —— W25Q64 SPI Flash 驱动（历史记录用，掉电不丢）
 * 8MB = 2048 个 4KB 扇区；SPI 模式 0；4MHz
 * 底层 SPI 外设在 BSP/bsp_spi.c
 * ============================================================= */
#include <stdint.h>
/* ---- W25Q64 指令集（只列本项目用到的） ---- */
#define W25_CMD_WRITE_EN        0x06U   /* 写使能：擦/写之前必须发 */
#define W25_CMD_READ_SR1        0x05U   /* 读状态寄存器 1 */
#define W25_CMD_READ_DATA       0x03U   /* 读数据（最常用） */
#define W25_CMD_PAGE_PROGRAM    0x02U   /* 页编程（256 字节一页） */
#define W25_CMD_SECTOR_ERASE    0x20U   /* 扇区擦除（4KB） */
#define W25_CMD_JEDEC_ID        0x9FU   /* 读 JEDEC ID：厂商号 + 容量号 */
#define W25_CMD_READ_STATUS2    0x35U   /* 读状态寄存器 2 的 QE 位 */
/* W25Q64 的正确 ID 是 EF 40 17：
 *   0xEF = 华邦(Winbond)   0x40 = SPI Flash 系列   0x17 = 8MB(64Mbit) */
#define W25_ID_MANU             0xEFU
/* 上电探测：读 JEDEC ID 并判断是否为本项目支持的型号，返回读到的 32 位 ID。
 * 「是否识别成功」由驱动内部记录，上层用 W25_IsOk() 取结论即可。 */
uint32_t W25_Probe(void);
uint8_t  W25_IsOk(void);      /* 1 = W25Q64 识别成功（W25_Probe() 之后有效） */
uint8_t  W25_WaitBusy(void);
void     W25_WriteEnable(void);
uint32_t W25_ReadID(void);
void     W25_Read(uint32_t addr, uint8_t *buf, uint32_t len);
void     W25_PageProgram(uint32_t addr, const uint8_t *buf, uint32_t len);
void     W25_SectorErase(uint32_t addr);
#endif /* W25Q64_H */
