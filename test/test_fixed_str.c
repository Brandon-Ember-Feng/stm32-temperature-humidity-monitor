/* =============================================================
 * test_fixed_str.c —— Util/fixed_str.c 的单元测试（定点数转字符串）
 *
 * 这些函数替代了 sprintf，手写的取余拆位最容易在小数和负数上出错：
 *   · 5（0.5 度）要显示成 "0.5"，整数部分得补个 0
 *   · -5 要显示成 "-0.5"，负号位置在整数部分之前
 *   · 时间显示要补零：9 分 5 秒是 "09:05"，不是 "9:5"
 * ============================================================= */
#include "framework.h"
#include "fixed_str.h"

/* ---- 带一位小数：正常值 ---- */
TEST_CASE(test_fix1_normal)
{
    char buf[24];

    StrAppendFix1(buf, 0U, 278);
    CHECK_EQ_STR(buf, "27.8");

    StrAppendFix1(buf, 0U, 300);
    CHECK_EQ_STR(buf, "30.0");

    StrAppendFix1(buf, 0U, 0);
    CHECK_EQ_STR(buf, "0.0");
}

/* ---- 带一位小数：小于 1 的值，整数部分要补 0 ---- */
TEST_CASE(test_fix1_less_than_one)
{
    char buf[24];

    StrAppendFix1(buf, 0U, 5);
    CHECK_EQ_STR(buf, "0.5");

    StrAppendFix1(buf, 0U, 9);
    CHECK_EQ_STR(buf, "0.9");
}

/* ---- 带一位小数：负数 ---- */
TEST_CASE(test_fix1_negative)
{
    char buf[24];

    StrAppendFix1(buf, 0U, -105);
    CHECK_EQ_STR(buf, "-10.5");

    StrAppendFix1(buf, 0U, -5);       /* -0.5：负号 + "0" + "." + "5" */
    CHECK_EQ_STR(buf, "-0.5");

    StrAppendFix1(buf, 0U, -100);
    CHECK_EQ_STR(buf, "-10.0");
}

/* ---- 无符号整数：0 与各量级 ---- */
TEST_CASE(test_u32_values)
{
    char buf[24];

    StrAppendU32(buf, 0U, 0U);
    CHECK_EQ_STR(buf, "0");

    StrAppendU32(buf, 0U, 7U);
    CHECK_EQ_STR(buf, "7");

    StrAppendU32(buf, 0U, 1234567U);
    CHECK_EQ_STR(buf, "1234567");

    StrAppendU32(buf, 0U, 4294967295U);   /* uint32 满量程，内部 tmp[11] 够用 */
    CHECK_EQ_STR(buf, "4294967295");
}

/* ---- 拼接：返回的是新的末尾下标，可以不看 buf 直接续着写 ---- */
TEST_CASE(test_append_chain_returns_new_index)
{
    char    buf[32];
    uint8_t n = 0U;

    n = StrAppendStr(buf, n, "Temp ");
    CHECK_EQ_INT(n, 5);
    n = StrAppendFix1(buf, n, 256);
    CHECK_EQ_INT(n, 9);
    n = StrAppendStr(buf, n, " C");
    CHECK_EQ_INT(n, 11);
    CHECK_EQ_STR(buf, "Temp 25.6 C");
}

/* ---- 补空格：同一行刷新时擦掉上一次的残影 ---- */
TEST_CASE(test_pad_to_width)
{
    char    buf[32];
    uint8_t n = 0U;

    n = StrAppendStr(buf, n, "ab");
    n = StrPadTo(buf, n, 6U);

    CHECK_EQ_INT(n, 6);
    CHECK_EQ_STR(buf, "ab    ");
}

/* ---- 补空格：已经够长时不动 ---- */
TEST_CASE(test_pad_to_does_not_truncate)
{
    char    buf[32];
    uint8_t n = 0U;

    n = StrAppendStr(buf, n, "abcdef");
    n = StrPadTo(buf, n, 4U);          /* width 比现有长度小 */

    CHECK_EQ_INT(n, 6);
    CHECK_EQ_STR(buf, "abcdef");
}

/* ---- 两位补零：时间显示用 ---- */
TEST_CASE(test_pad2)
{
    char buf[8];

    StrAppendPad2(buf, 0U, 0U);
    CHECK_EQ_STR(buf, "00");

    StrAppendPad2(buf, 0U, 5U);
    CHECK_EQ_STR(buf, "05");

    StrAppendPad2(buf, 0U, 59U);
    CHECK_EQ_STR(buf, "59");

    StrAppendPad2(buf, 0U, 100U);      /* 超过两位 -> 截到 99，不越界 */
    CHECK_EQ_STR(buf, "99");
}

/* ---- 完整时间串：10:05:03 这种格式 ---- */
TEST_CASE(test_time_string_composition)
{
    char    buf[24];
    uint8_t n = 0U;

    n = StrAppendPad2(buf, n, 10U);
    n = StrAppendStr(buf, n, ":");
    n = StrAppendPad2(buf, n, 5U);
    n = StrAppendStr(buf, n, ":");
    n = StrAppendPad2(buf, n, 3U);

    CHECK_EQ_STR(buf, "10:05:03");
}

void suite_fixed_str(void)
{
    tf_suite_begin("Util/fixed_str.c - fixed-point formatting");

    RUN_CASE(test_fix1_normal);
    RUN_CASE(test_fix1_less_than_one);
    RUN_CASE(test_fix1_negative);
    RUN_CASE(test_u32_values);
    RUN_CASE(test_append_chain_returns_new_index);
    RUN_CASE(test_pad_to_width);
    RUN_CASE(test_pad_to_does_not_truncate);
    RUN_CASE(test_pad2);
    RUN_CASE(test_time_string_composition);
}
