/* =============================================================
 * test_hist_index.c —— Drivers/hist_index.c 的单元测试（记录寻址）
 *
 * 双扇区乒乓之后，"逻辑上第 idx 条"和"物理上的哪个地址"不再是线性关系：
 * 扇区头里的序号(seq)小的那一轮，存的反而是更早的记录。这段算术
 * 在板子上没法逐步调试（要靠串口打日志猜），但它是纯函数，可以
 * 用构造出来的条数/序号把每个边界都算一遍。
 *
 * 布局复习：扇区头占 4 字节，之后每条记录 3 字节。
 *   = W25_REC_HDR_SIZE            = 4
 *   = W25_REC_SIZE                = 3
 *   = W25_SECTOR_A / _B           = 0x000000 / 0x001000
 * ============================================================= */
#include "framework.h"
#include "hist_index.h"

#define ADDR_A(n)  (W25_SECTOR_A + W25_REC_HDR_SIZE + (uint32_t)(n) * W25_REC_SIZE)
#define ADDR_B(n)  (W25_SECTOR_B + W25_REC_HDR_SIZE + (uint32_t)(n) * W25_REC_SIZE)

/* ---- 只有一个扇区有数据（刚上电 / 刚乒乓过一次）---- */
TEST_CASE(test_locate_only_sector_a)
{
    Hist_Index_T ix;
    uint32_t     addr = 0U;

    ix.cnt_a = 10U; ix.cnt_b = 0U; ix.seq_a = 0U; ix.seq_b = 0U;

    CHECK_EQ_INT(Hist_Locate(&ix, 0U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_A(0));            /* 4 */

    CHECK_EQ_INT(Hist_Locate(&ix, 9U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_A(9));

    CHECK_EQ_INT(Hist_Locate(&ix, 10U, &addr), 0);   /* 越界 */
}

TEST_CASE(test_locate_only_sector_b)
{
    Hist_Index_T ix;
    uint32_t     addr = 0U;

    ix.cnt_a = 0U; ix.cnt_b = 5U; ix.seq_a = 0U; ix.seq_b = 0U;

    CHECK_EQ_INT(Hist_Locate(&ix, 0U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_B(0));            /* 0x1004 */
}

/* ---- 两个扇区都有数据：B 更新（seq_b 大），A 里是更早的记录 ---- */
TEST_CASE(test_locate_b_is_newer)
{
    Hist_Index_T ix;
    uint32_t     addr = 0U;

    ix.cnt_a = 3U; ix.cnt_b = 2U; ix.seq_a = 0U; ix.seq_b = 3U;

    CHECK_EQ_INT(Hist_Locate(&ix, 0U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_A(0));            /* A 的第 0 条 */

    CHECK_EQ_INT(Hist_Locate(&ix, 2U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_A(2));            /* A 的最后一条 */

    /* ---- 跨扇区翻转的那个边界 ---- */
    CHECK_EQ_INT(Hist_Locate(&ix, 3U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_B(0));            /* 换到 B 的第 0 条，不是 A 的第 3 条 */

    CHECK_EQ_INT(Hist_Locate(&ix, 4U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_B(1));

    CHECK_EQ_INT(Hist_Locate(&ix, 5U, &addr), 0);   /* 越界 */
}

/* ---- 反过来：A 更新，B 里是更早的记录 ---- */
TEST_CASE(test_locate_a_is_newer)
{
    Hist_Index_T ix;
    uint32_t     addr = 0U;

    ix.cnt_a = 2U; ix.cnt_b = 4U; ix.seq_a = 10U; ix.seq_b = 6U;

    CHECK_EQ_INT(Hist_Locate(&ix, 0U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_B(0));            /* B 是旧的，排前面 */

    CHECK_EQ_INT(Hist_Locate(&ix, 3U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_B(3));

    CHECK_EQ_INT(Hist_Locate(&ix, 4U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_A(0));            /* 翻到 A */

    CHECK_EQ_INT(Hist_Locate(&ix, 5U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_A(1));

    CHECK_EQ_INT(Hist_Locate(&ix, 6U, &addr), 0);
}

/* ---- 满载：两个扇区各 1364 条，共 2728 条。这是容量的真实上限 ---- */
TEST_CASE(test_locate_both_sectors_full)
{
    Hist_Index_T ix;
    uint32_t     addr = 0U;

    ix.cnt_a = 1364U; ix.cnt_b = 1364U; ix.seq_a = 0U; ix.seq_b = 1364U;

    /* 旧扇区（A）的最后一条：4 + 1363*3 = 4093，仍在 4096 字节之内 */
    CHECK_EQ_INT(Hist_Locate(&ix, 1363U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_A(1363));
    CHECK(addr < (W25_SECTOR_A + 4096UL));

    /* 跨到新扇区（B）的第一条 */
    CHECK_EQ_INT(Hist_Locate(&ix, 1364U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_B(0));

    /* 全库最后一条 */
    CHECK_EQ_INT(Hist_Locate(&ix, 2727U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_B(1363));
    CHECK(addr < (W25_SECTOR_B + 4096UL));

    /* 再往后就越界了 */
    CHECK_EQ_INT(Hist_Locate(&ix, 2728U, &addr), 0);
}

/* ---- 序号相等：认为 A 不更新，走"只有一个扇区有数据"的分支 ---- */
TEST_CASE(test_locate_equal_seq_prefers_single_branch)
{
    Hist_Index_T ix;
    uint32_t     addr = 0U;

    ix.cnt_a = 0U; ix.cnt_b = 5U; ix.seq_a = 5U; ix.seq_b = 5U;

    CHECK_EQ_INT(Hist_Locate(&ix, 0U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_B(0));
}

/* ---- 全空：任何 idx 都必须越界，不能算出合法地址 ---- */
TEST_CASE(test_locate_empty_storage)
{
    Hist_Index_T ix;
    uint32_t     addr = 0U;

    ix.cnt_a = 0U; ix.cnt_b = 0U; ix.seq_a = 0U; ix.seq_b = 0U;

    CHECK_EQ_INT(Hist_Locate(&ix, 0U, &addr), 0);
    CHECK_EQ_INT(Hist_Locate(&ix, 1U, &addr), 0);
}

/* ---- 邻近边界：旧扇区只有 1 条时，idx 0 和 1 分别落在两个扇区 ---- */
TEST_CASE(test_locate_boundary_old_sector_has_one_record)
{
    Hist_Index_T ix;
    uint32_t     addr = 0U;

    ix.cnt_a = 1U; ix.cnt_b = 4U; ix.seq_a = 0U; ix.seq_b = 1U;

    CHECK_EQ_INT(Hist_Locate(&ix, 0U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_A(0));

    CHECK_EQ_INT(Hist_Locate(&ix, 1U, &addr), 1);
    CHECK_EQ_INT(addr, ADDR_B(0));
}

void suite_hist_index(void)
{
    tf_suite_begin("Drivers/hist_index.c - record addressing");

    RUN_CASE(test_locate_only_sector_a);
    RUN_CASE(test_locate_only_sector_b);
    RUN_CASE(test_locate_b_is_newer);
    RUN_CASE(test_locate_a_is_newer);
    RUN_CASE(test_locate_both_sectors_full);
    RUN_CASE(test_locate_equal_seq_prefers_single_branch);
    RUN_CASE(test_locate_empty_storage);
    RUN_CASE(test_locate_boundary_old_sector_has_one_record);
}
