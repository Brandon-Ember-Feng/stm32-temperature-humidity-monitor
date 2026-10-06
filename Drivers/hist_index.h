#ifndef HIST_INDEX_H
#define HIST_INDEX_H
/* =============================================================
 * hist_index.h —— 历史记录的「逻辑序号 → Flash 物理地址」换算
 *                 （纯逻辑，无硬件依赖）
 *
 * 为什么单独拆出来：
 *   双扇区乒乓之后，物理地址和时间顺序不再一致 —— 序号小的那个扇区里
 *   存的反而是更早的记录。这段「排序 + 寻址」是整个存储模块里最容易
 *   算错的地方（尤其是"跨扇区翻转"那一步），但它完全可以脱离 Flash
 *   验证：喂进两个扇区的条数和起始序号，看算出来的地址对不对。
 *   所以把它从 hist_store.c 里剥出来，做成纯函数。
 * ============================================================= */
#include <stdint.h>
#include "hist_store.h"

/* 换算所需的全部输入 —— 就是驱动里那几个 static 状态的快照 */
typedef struct
{
    uint16_t cnt_a;   /* 扇区 A 的实际记录条数 */
    uint16_t cnt_b;   /* 扇区 B 的实际记录条数 */
    uint16_t seq_a;   /* 扇区 A 头的起始序号（越大表示这轮数据越新） */
    uint16_t seq_b;   /* 扇区 B 头的起始序号 */
} Hist_Index_T;

/* 把「逻辑序号 idx」（0 = 最早的一条）换算成 Flash 上的绝对地址。
 * 返回 1 = 换算成功，返回 0 = idx 超出总条数 */
uint8_t Hist_Locate(const Hist_Index_T *ix, uint32_t idx, uint32_t *addr);

#endif /* HIST_INDEX_H */
