#ifndef UTIL_FIXED_STR_H
#define UTIL_FIXED_STR_H
/* =============================================================
 * fixed_str.h —— 定点数转字符串的小工具（无硬件依赖，可在 PC 上编译测试）
 *
 * 为什么不用 sprintf：标准库会把一大坨格式化代码和浮点支持链接进来，
 * 单片机 Flash 只有 64KB，能省就省；自己写几行反而更快更小。
 * 约定：所有「带一位小数的温湿度」统一用「放大 10 倍的 int16_t」表示，
 *       256 表示 25.6。
 * ============================================================= */
#include <stdint.h>
uint8_t StrAppendStr(char *buf, uint8_t n, const char *s);
uint8_t StrAppendU32(char *buf, uint8_t n, uint32_t v);
uint8_t StrAppendFix1(char *buf, uint8_t n, int16_t v10);
uint8_t StrPadTo(char *buf, uint8_t n, uint8_t width);
uint8_t StrAppendPad2(char *buf, uint8_t n, uint32_t v);
#endif /* UTIL_FIXED_STR_H */
