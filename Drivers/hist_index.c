#include "hist_index.h"

/* =============================================================
 * hist_index.c —— 记录寻址实现（纯逻辑）
 *
 * 本文件不 include 任何 bsp_*.h，只做算术：给定两个扇区的条数与起始
 * 序号，算出第 idx 条记录落在 Flash 的哪个绝对地址上。
 * ============================================================= */

uint8_t Hist_Locate(const Hist_Index_T *ix, uint32_t idx, uint32_t *addr)
{
    uint32_t total = (uint32_t)ix->cnt_a + (uint32_t)ix->cnt_b;

    if (idx >= total) return 0U;   /* 越界 */

    if ((ix->cnt_a > 0U) && (ix->cnt_b > 0U))
    {
        /* 两个扇区都有数据：扇区头里序号(seq)大的那个是"更新的那一轮"， */
        uint8_t  a_is_newer = (ix->seq_a >= ix->seq_b) ? 1U : 0U;

        /* 序号小的那个扇区里存的是更早的记录，排在逻辑序号的前面 */
        uint16_t old_cnt = a_is_newer ? ix->cnt_b : ix->cnt_a;
        uint32_t old_sec = a_is_newer ? W25_SECTOR_B : W25_SECTOR_A;

        if (idx < (uint32_t)old_cnt)
        {
            /* 落在旧扇区里 */
            *addr = old_sec + W25_REC_HDR_SIZE + idx * W25_REC_SIZE;
        }
        else
        {
            /* 越过旧扇区，落到新扇区的第 (idx - old_cnt) 条 */
            uint32_t k       = idx - (uint32_t)old_cnt;
            uint32_t new_sec = a_is_newer ? W25_SECTOR_A : W25_SECTOR_B;

            *addr = new_sec + W25_REC_HDR_SIZE + k * W25_REC_SIZE;
        }
    }
    else
    {
        /* 只有一个扇区有数据（刚上电，或刚乒乓过一次） */
        uint32_t sec = (ix->cnt_a > 0U) ? W25_SECTOR_A : W25_SECTOR_B;

        *addr = sec + W25_REC_HDR_SIZE + idx * W25_REC_SIZE;
    }

    return 1U;
}
