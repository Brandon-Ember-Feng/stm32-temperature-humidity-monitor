/* =============================================================
 * test_filter.c —— Util/filter.c 的单元测试（滑动平均滤波器）
 *
 * 这个模块的坑集中在"边界"上：
 *   · 刚上电、样本还没攒够时，平均要按**实际个数**算，不能除 FILTER_N
 *   · 写满一圈之后，丢掉的是**最旧的**那条
 *   · 全是负数（零下温度）时，整数除法的截断方向
 * ============================================================= */
#include "framework.h"
#include "filter.h"

/* ---- 初始化必须把状态清干净，且空窗口求平均不能除零 ---- */
TEST_CASE(test_filter_init_resets_state)
{
    Filter_T f;
    uint8_t  i;

    f.idx = 3U;                     /* 先塞点脏数据 */
    f.cnt = 2U;
    for (i = 0U; i < FILTER_N; i++) f.buf[i] = 999;

    Filter_Init(&f);

    CHECK_EQ_INT(Filter_Count(&f), 0);
    CHECK_EQ_INT(Filter_Avg(&f), 0);
}

/* ---- 单个样本 ---- */
TEST_CASE(test_filter_single_sample)
{
    Filter_T f;

    Filter_Init(&f);
    Filter_Push(&f, 256);           /* 25.6 度 */

    CHECK_EQ_INT(Filter_Count(&f), 1);
    CHECK_EQ_INT(Filter_Avg(&f), 256);
}

/* ---- 窗口未满：除以实际个数，不是除以 FILTER_N ---- */
TEST_CASE(test_filter_partial_window_uses_actual_count)
{
    Filter_T f;

    Filter_Init(&f);
    Filter_Push(&f, 300);
    Filter_Push(&f, 320);
    Filter_Push(&f, 280);

    CHECK_EQ_INT(Filter_Count(&f), 3);
    CHECK_EQ_INT(Filter_Avg(&f), 300);   /* 900/3；若错写成 /5 会得到 180 */
}

/* ---- 窗口刚好满：方案 2.2 给的例子 [30,32,28,31,29] -> 30.0 ---- */
TEST_CASE(test_filter_full_window)
{
    Filter_T f;
    uint8_t  i;
    int16_t  in[FILTER_N];

    in[0] = 300; in[1] = 320; in[2] = 280; in[3] = 310; in[4] = 290;

    Filter_Init(&f);
    for (i = 0U; i < FILTER_N; i++) Filter_Push(&f, in[i]);

    CHECK_EQ_INT(Filter_Count(&f), FILTER_N);
    CHECK_EQ_INT(Filter_Avg(&f), 300);   /* 1500/5 */
}

/* ---- 滚动覆盖：写满一圈后进来的新值，挤掉的是最旧的 ---- */
TEST_CASE(test_filter_rollover_drops_oldest)
{
    Filter_T f;
    uint8_t  i;

    Filter_Init(&f);
    for (i = 0U; i < 5U; i++) Filter_Push(&f, (int16_t)((int16_t)(i + 1U) * 100));
    CHECK_EQ_INT(Filter_Avg(&f), 300);   /* 100..500 */

    Filter_Push(&f, 600);                /* 窗口 -> 200..600 */
    CHECK_EQ_INT(Filter_Avg(&f), 400);

    Filter_Push(&f, 700);                /* 窗口 -> 300..700 */
    CHECK_EQ_INT(Filter_Avg(&f), 500);

    CHECK_EQ_INT(Filter_Count(&f), FILTER_N);   /* 条数封顶，不再增长 */
}

/* ---- 负数：零下温度也要算对 ---- */
TEST_CASE(test_filter_negative_values)
{
    Filter_T f;

    Filter_Init(&f);
    Filter_Push(&f, -100);
    Filter_Push(&f, -200);

    CHECK_EQ_INT(Filter_Avg(&f), -150);  /* -300/2 */
}

/* ---- 整数除法的截断方向：C 语言向零取整，不是向下取整 ---- */
TEST_CASE(test_filter_division_truncates_toward_zero)
{
    Filter_T f;

    Filter_Init(&f);
    Filter_Push(&f, 1);
    Filter_Push(&f, 2);
    CHECK_EQ_INT(Filter_Avg(&f), 1);     /* 3/2 = 1 */

    Filter_Init(&f);
    Filter_Push(&f, -1);
    Filter_Push(&f, -2);
    CHECK_EQ_INT(Filter_Avg(&f), -1);    /* -3/2 = -1（向下取整会得到 -2） */
}

/* ---- 连续推入远超窗口长度，内部计数不能溢出 ---- */
TEST_CASE(test_filter_count_caps_at_window_size)
{
    Filter_T f;
    uint8_t  i;

    Filter_Init(&f);
    for (i = 0U; i < 200U; i++) Filter_Push(&f, (int16_t)i);

    CHECK_EQ_INT(Filter_Count(&f), FILTER_N);
}

void suite_filter(void)
{
    tf_suite_begin("Util/filter.c - moving average");

    RUN_CASE(test_filter_init_resets_state);
    RUN_CASE(test_filter_single_sample);
    RUN_CASE(test_filter_partial_window_uses_actual_count);
    RUN_CASE(test_filter_full_window);
    RUN_CASE(test_filter_rollover_drops_oldest);
    RUN_CASE(test_filter_negative_values);
    RUN_CASE(test_filter_division_truncates_toward_zero);
    RUN_CASE(test_filter_count_caps_at_window_size);
}
