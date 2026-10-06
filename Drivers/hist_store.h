#ifndef HIST_STORE_H
#define HIST_STORE_H
/* =============================================================
 * hist_store.h —— 历史记录存储管理（双扇区乒乓 + 顺序追加）
 *
 * Flash 只能把 1 写成 0、擦除最小单位是 4KB 扇区，所以不能像写文件
 * 那样随便改一个字节。这里用「双扇区乒乓」：一个扇区是完整的旧数据，
 * 另一个在接新数据，掉电最多丢当前这一条。
 * ============================================================= */
#include <stdint.h>
#define W25_SECTOR_A        0x000000UL   /* 记录区 A 的扇区起始地址 */
#define W25_SECTOR_B        0x001000UL   /* 记录区 B 的扇区起始地址 */
#define W25_REC_HDR_SIZE    4UL          /* 扇区头：魔数+状态+起始序号(2B) */
#define W25_REC_SIZE        3UL          /* 每条记录 3 字节 */
#define W25_REC_AREA        (4096UL - W25_REC_HDR_SIZE)          /* 4092 */
#define W25_REC_PER_SECTOR  (W25_REC_AREA / W25_REC_SIZE)        /* 1364 */
#define W25_REC_MAGIC       0xA5U        /* 魔数：判断扇区有没有被初始化过 */
#define W25_REC_VALID       0x01U        /* 状态字节：1 = 本扇区数据有效 */
#define W25_VERIFY_EVERY    100U         /* 每写这么多条，重扫一遍 Flash 对账 */
/* 头 4 字节的内存结构（不直接往 Flash 写结构体，避免对齐/字节序问题） */
typedef struct
{
    uint16_t seq;        /* 本扇区第一条记录的序号 */
    uint8_t  start;      /* 数据区在扇区的字节偏移（固定 = 4） */
    uint8_t  count;      /* 本扇区已写了几条记录 */
} W25_RecInfo;
/* 记录系统的三个只读状态，全部通过访问函数暴露 */
uint32_t Hist_Count(void);      /* 两个扇区加起来的总记录条数 */
uint8_t  Hist_IsReady(void);    /* 1 = 记录系统可用（W25Q64 在线） */
uint8_t  Hist_WrSector(void);   /* 当前正在写的扇区：0 = A，1 = B */
void    W25_RecInit(void);
uint8_t W25_RecAppend(int16_t temp_x10, int16_t humi_x10);
uint8_t W25_RecRead(uint32_t idx, int16_t *temp_x10, int16_t *humi_x10);
void    W25_RefreshCounts(void);
#endif /* HIST_STORE_H */
