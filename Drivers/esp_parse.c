#include "esp_parse.h"

/* =============================================================
 * esp_parse.c —— AT 应答关键词匹配（纯逻辑）
 *
 * 本文件不 include 任何 bsp_*.h。所有边界都由显式的 len 决定，
 * 不依赖"缓冲是 NUL 结尾"这个前提 —— 这样测试时可以随便构造
 * 不含 '\0' 的定长数据，也不会读到缓冲区外面去。
 * ============================================================= */

uint8_t ESP_MatchAtIn(const char *buf, uint16_t len, uint16_t pos, const char *token)
{
    uint16_t k = pos;
    uint8_t  j = 0U;

    while (token[j] != '\0')
    {
        if (k >= len) return 0U;          /* token 没有完整落在 len 之内 -> 不算匹配 */
        if (buf[k] != token[j]) return 0U;
        k++;
        j++;
    }
    return 1U;
}

uint8_t ESP_FindToken(const char *buf, uint16_t len, const char *token)
{
    uint16_t i;

    if (token[0] == '\0') return 1U;      /* 空串约定为"总能找到" */

    for (i = 0U; i < len; i++)
    {
        if (ESP_MatchAtIn(buf, len, i, token) != 0U) return 1U;
    }
    return 0U;
}
