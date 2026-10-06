/* =============================================================
 * test_dht11_frame.c —— Drivers/dht11_frame.c 的单元测试（帧解码）
 *
 * 这是整份固件里最该被测的一段：DHT11 的 40 位数据没有任何"同步头"，
 * 一旦时序被干扰就会读出错位数据。校验和是最后一道防线 ——
 * 如果它失效，屏幕会显示一个看起来很合理、其实是错的温度。
 *
 * 已知的物理边界：
 *   · 传感器没接 / 线被上拉到 3.3V -> 读到全 0xFF -> 必须拒收
 *   · 全 0 帧的校验和恰好是 0，数学上"合法"（真实模块不会发，但要说明）
 *   · 负温靠「温度整数字节」的最高位标记，不是补码
 * ============================================================= */
#include "framework.h"
#include "dht11_frame.h"

/* ---- 正常帧：65.0% / 25.6 度 ---- */
TEST_CASE(test_frame_normal)
{
    const uint8_t raw[DHT_FRAME_LEN] = { 65U, 0U, 25U, 6U, 96U };   /* 65+0+25+6 = 96 */

    CHECK_EQ_INT(DHT_CheckSum(raw), 1);
    CHECK_EQ_INT(DHT_DecodeHumi(raw), 650);    /* 65.0 */
    CHECK_EQ_INT(DHT_DecodeTemp(raw), 256);    /* 25.6 */
}

/* ---- 小数位非 0（兼容别的型号）---- */
TEST_CASE(test_frame_with_decimals)
{
    const uint8_t raw[DHT_FRAME_LEN] = { 65U, 5U, 25U, 6U, 101U };  /* 65+5+25+6 */

    CHECK_EQ_INT(DHT_CheckSum(raw), 1);
    CHECK_EQ_INT(DHT_DecodeHumi(raw), 655);    /* 65.5 */
    CHECK_EQ_INT(DHT_DecodeTemp(raw), 256);
}

/* ---- 校验和错一位就必须被拒 ---- */
TEST_CASE(test_frame_rejects_bad_checksum)
{
    const uint8_t ok[DHT_FRAME_LEN]   = { 65U, 0U, 25U, 6U, 96U };
    const uint8_t bad1[DHT_FRAME_LEN] = { 65U, 0U, 25U, 6U, 97U };  /* 校验和 +1 */
    const uint8_t bad2[DHT_FRAME_LEN] = { 66U, 0U, 25U, 6U, 96U };  /* 数据 +1 */

    CHECK_EQ_INT(DHT_CheckSum(ok), 1);
    CHECK_EQ_INT(DHT_CheckSum(bad1), 0);
    CHECK_EQ_INT(DHT_CheckSum(bad2), 0);
}

/* ---- 全 0xFF：传感器没接、线被上拉的典型表现 ---- */
TEST_CASE(test_frame_rejects_all_ones)
{
    const uint8_t raw[DHT_FRAME_LEN] = { 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU };

    /* 前 4 字节之和 = 1020，取低 8 位 = 252，与 255 不符 -> 拒收 */
    CHECK_EQ_INT(DHT_CheckSum(raw), 0);
}

/* ---- 全 0：校验和恰好是 0，数学上"合法"，说明清楚这条边界 ---- */
TEST_CASE(test_frame_all_zero_passes_checksum)
{
    const uint8_t raw[DHT_FRAME_LEN] = { 0U, 0U, 0U, 0U, 0U };

    CHECK_EQ_INT(DHT_CheckSum(raw), 1);        /* 0 == 0，无法靠校验和识别 */
    CHECK_EQ_INT(DHT_DecodeHumi(raw), 0);
    CHECK_EQ_INT(DHT_DecodeTemp(raw), 0);
}

/* ---- 校验和按 8 位回绕，不能当成 32 位加法 ---- */
TEST_CASE(test_frame_checksum_wraps_at_8_bits)
{
    /* 200+100+100+100 = 500 -> 低 8 位 244 */
    const uint8_t raw[DHT_FRAME_LEN] = { 200U, 100U, 100U, 100U, 244U };

    CHECK_EQ_INT(DHT_CheckSum(raw), 1);
}

/* ---- 负温：最高位是"符号标记"，不是补码 ---- */
TEST_CASE(test_frame_negative_temperature)
{
    /* 0x80 | 10 = 138 -> -10.5 度 */
    const uint8_t raw[DHT_FRAME_LEN] = { 50U, 0U, 138U, 5U, 193U };

    CHECK_EQ_INT(DHT_CheckSum(raw), 1);
    CHECK_EQ_INT(DHT_DecodeTemp(raw), -105);
    CHECK_EQ_INT(DHT_DecodeHumi(raw), 500);
}

/* ---- 负温边界：0x80 表示 -0.0，解出来就是 0 ---- */
TEST_CASE(test_frame_negative_zero)
{
    const uint8_t raw[DHT_FRAME_LEN] = { 50U, 0U, 128U, 0U, 178U };

    CHECK_EQ_INT(DHT_CheckSum(raw), 1);
    CHECK_EQ_INT(DHT_DecodeTemp(raw), 0);
}

/* ---- 零下 1 度这类"小数位为 0 的负数"，别把 -0 算成 0 ---- */
TEST_CASE(test_frame_negative_one_degree)
{
    /* 0x80 | 1 = 129 -> -1.0 */
    const uint8_t raw[DHT_FRAME_LEN] = { 50U, 0U, 129U, 0U, 179U };

    CHECK_EQ_INT(DHT_CheckSum(raw), 1);
    CHECK_EQ_INT(DHT_DecodeTemp(raw), -10);
}

void suite_dht11_frame(void)
{
    tf_suite_begin("Drivers/dht11_frame.c - frame decode");

    RUN_CASE(test_frame_normal);
    RUN_CASE(test_frame_with_decimals);
    RUN_CASE(test_frame_rejects_bad_checksum);
    RUN_CASE(test_frame_rejects_all_ones);
    RUN_CASE(test_frame_all_zero_passes_checksum);
    RUN_CASE(test_frame_checksum_wraps_at_8_bits);
    RUN_CASE(test_frame_negative_temperature);
    RUN_CASE(test_frame_negative_zero);
    RUN_CASE(test_frame_negative_one_degree);
}
