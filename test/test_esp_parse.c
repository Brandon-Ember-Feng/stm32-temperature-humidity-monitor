/* =============================================================
 * test_esp_parse.c —— Drivers/esp_parse.c 的单元测试（AT 应答匹配）
 *
 * 串口是"字节流"不是"消息流"：一次 AT 指令的应答可能分几批才收完，
 * 上一批的尾巴和这一批的头拼在一起才算一条完整响应（粘包）。
 * 所以"在缓冲里找 OK"这件事的边界很关键：
 *   · 半截 "O" 不能算匹配到 "OK"
 *   · 缓冲区末尾的 token 前缀不能被当成命中
 *   · 不能读到 len 之外（那里可能是上一批的残留数据）
 *
 * 这里故意用**不含 '\0' 结尾**的数组来测，逼实现只依赖显式的 len。
 * ============================================================= */
#include "framework.h"
#include "esp_parse.h"

/* ---- 基本查找 ---- */
TEST_CASE(test_find_token_basic)
{
    const char buf[] = "AT\r\nOK\r\n";
    uint16_t   n = (uint16_t)(sizeof(buf) - 1U);   /* 8，不含 '\0' */

    CHECK_EQ_INT(ESP_FindToken(buf, n, "OK"), 1);
    CHECK_EQ_INT(ESP_FindToken(buf, n, "ERROR"), 0);
    CHECK_EQ_INT(ESP_FindToken(buf, n, "AT"), 1);
}

/* ---- 粘包：响应被切成两批，第一批只有半个关键词 ---- */
TEST_CASE(test_find_token_split_across_batches)
{
    const char half[]  = "AT\r\nO";
    const char whole[] = "AT\r\nOK";

    CHECK_EQ_INT(ESP_FindToken(half,  (uint16_t)(sizeof(half)  - 1U), "OK"), 0);
    CHECK_EQ_INT(ESP_FindToken(whole, (uint16_t)(sizeof(whole) - 1U), "OK"), 1);
}

/* ---- 从指定位置开始匹配 ---- */
TEST_CASE(test_match_at_position)
{
    const char buf[] = "AT\r\nOK\r\n";
    uint16_t   n = (uint16_t)(sizeof(buf) - 1U);

    CHECK_EQ_INT(ESP_MatchAtIn(buf, n, 4U, "OK"), 1);    /* 索引 4 正是 'O' */
    CHECK_EQ_INT(ESP_MatchAtIn(buf, n, 3U, "OK"), 0);    /* 索引 3 是 '\n' */
    CHECK_EQ_INT(ESP_MatchAtIn(buf, n, 0U, "AT"), 1);
    CHECK_EQ_INT(ESP_MatchAtIn(buf, n, 1U, "AT"), 0);
}

/* ---- 末尾的半截 token 不算命中 ---- */
TEST_CASE(test_match_rejects_partial_token_at_end)
{
    const char buf[] = "xxO";
    uint16_t   n = (uint16_t)(sizeof(buf) - 1U);   /* 3 */

    CHECK_EQ_INT(ESP_MatchAtIn(buf, n, 2U, "OK"), 0);   /* "O" 后面没有字节了 */
    CHECK_EQ_INT(ESP_FindToken(buf, n, "OK"), 0);
}

/* ---- 绝不允许读到 len 之外 ----
 * 这几个数组故意不写 '\0'，如果实现里用 strcmp / strlen 之类的思路，
 * 就会读到数组外面去，把垃圾数据当成匹配结果。 */
TEST_CASE(test_never_reads_past_length)
{
    const char buf[8] = { 'O', 'K', 'O', 'K', 'O', 'K', 'O', 'K' };

    /* len = 1 时只有 'O' 是有效的，不能因为后面正好还有 'K' 就判"找到" */
    CHECK_EQ_INT(ESP_MatchAtIn(buf, 1U, 0U, "OK"), 0);
    CHECK_EQ_INT(ESP_FindToken(buf, 1U, "OK"), 0);

    /* len = 2 时正好凑成 "OK"，应当命中 */
    CHECK_EQ_INT(ESP_FindToken(buf, 2U, "OK"), 1);

    /* 末尾完整出现也算 */
    CHECK_EQ_INT(ESP_FindToken(buf, 8U, "OK"), 1);
}

/* ---- 空输入与空关键词 ---- */
TEST_CASE(test_empty_inputs)
{
    const char buf[] = "abc";

    CHECK_EQ_INT(ESP_FindToken(buf, 0U, "abc"), 0);      /* len = 0：什么都没收到 */
    CHECK_EQ_INT(ESP_FindToken(buf, 3U, ""), 1);         /* 空串约定为"总能找到" */
    CHECK_EQ_INT(ESP_MatchAtIn(buf, 3U, 0U, ""), 1);
}

/* ---- 重叠匹配：OOOK 里找 OK ---- */
TEST_CASE(test_overlapping_match)
{
    const char buf[] = "OOOK";

    CHECK_EQ_INT(ESP_FindToken(buf, (uint16_t)(sizeof(buf) - 1U), "OK"), 1);
}

/* ---- token 比整个缓冲还长 ---- */
TEST_CASE(test_token_longer_than_buffer)
{
    const char buf[] = "OK";

    CHECK_EQ_INT(ESP_FindToken(buf, 2U, "OKOKOK"), 0);
}

/* ---- 单字符 token（CIPSEND 的 '>' 提示符）---- */
TEST_CASE(test_single_char_token)
{
    const char hit[]  = "AT+CIPSEND=4\r\n> ";
    const char miss[] = "abcdefg";

    CHECK_EQ_INT(ESP_FindToken(hit,  (uint16_t)(sizeof(hit)  - 1U), ">"), 1);
    CHECK_EQ_INT(ESP_FindToken(miss, (uint16_t)(sizeof(miss) - 1U), ">"), 0);
}

/* ---- 真实场景：模块主动上报的连接事件 ---- */
TEST_CASE(test_real_at_responses)
{
    const char conn[]   = "0,CONNECT\r\n";
    const char closed[] = "0,CLOSED\r\n";
    const char gotip[]  = "\r\nWIFI GOT IP\r\n\r\nOK\r\n";
    const char err[]    = "ERROR\r\n";
    const char busy[]   = "busy p...\r\n";
    const char invalid[] = "link is not valid\r\n";

    CHECK_EQ_INT(ESP_FindToken(conn,   (uint16_t)(sizeof(conn)   - 1U), "CONNECT"), 1);
    CHECK_EQ_INT(ESP_FindToken(conn,   (uint16_t)(sizeof(conn)   - 1U), "CLOSED"),  0);

    CHECK_EQ_INT(ESP_FindToken(closed, (uint16_t)(sizeof(closed) - 1U), "CLOSED"),  1);
    CHECK_EQ_INT(ESP_FindToken(closed, (uint16_t)(sizeof(closed) - 1U), "CONNECT"), 0);

    CHECK_EQ_INT(ESP_FindToken(gotip,  (uint16_t)(sizeof(gotip)  - 1U), "OK"),      1);
    CHECK_EQ_INT(ESP_FindToken(err,    (uint16_t)(sizeof(err)    - 1U), "OK"),      0);
    CHECK_EQ_INT(ESP_FindToken(err,    (uint16_t)(sizeof(err)    - 1U), "ERROR"),   1);
    CHECK_EQ_INT(ESP_FindToken(busy,   (uint16_t)(sizeof(busy)   - 1U), "OK"),      0);
    CHECK_EQ_INT(ESP_FindToken(invalid,(uint16_t)(sizeof(invalid)- 1U), "link is not valid"), 1);
}

/* ---- 缓冲里同时有 CONNECT 和 CLOSED 时，两个都找得到（顺序由调用方决定）---- */
TEST_CASE(test_both_keywords_present)
{
    const char buf[] = "0,CLOSED\r\n0,CONNECT\r\n";
    uint16_t   n = (uint16_t)(sizeof(buf) - 1U);

    CHECK_EQ_INT(ESP_FindToken(buf, n, "CLOSED"), 1);
    CHECK_EQ_INT(ESP_FindToken(buf, n, "CONNECT"), 1);
}

void suite_esp_parse(void)
{
    tf_suite_begin("Drivers/esp_parse.c - AT response matching");

    RUN_CASE(test_find_token_basic);
    RUN_CASE(test_find_token_split_across_batches);
    RUN_CASE(test_match_at_position);
    RUN_CASE(test_match_rejects_partial_token_at_end);
    RUN_CASE(test_never_reads_past_length);
    RUN_CASE(test_empty_inputs);
    RUN_CASE(test_overlapping_match);
    RUN_CASE(test_token_longer_than_buffer);
    RUN_CASE(test_single_char_token);
    RUN_CASE(test_real_at_responses);
    RUN_CASE(test_both_keywords_present);
}
