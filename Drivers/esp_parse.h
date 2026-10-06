#ifndef ESP_PARSE_H
#define ESP_PARSE_H
/* =============================================================
 * esp_parse.h —— ESP-01S 的 AT 应答解析（纯逻辑，无硬件依赖）
 *
 * 为什么单独拆出来：
 *   串口数据是「流」，不是「消息」—— AT 应答可能在任意位置被切断，
 *   这次收不全、下次又收到一半（粘包）。判断"缓冲里有没有 OK"看着简单，
 *   边界却很容易错：读越界、把半截字符误判成关键词。
 *   把它做成接受 (buf, len) 的纯函数之后，可以直接用构造出来的
 *   半截响应、超长响应去测，不必真的接一个 ESP 模块。
 * ============================================================= */
#include <stdint.h>

/* 判断 buf[0..len) 里从 pos 开始是否正好是 token（要求 token 完整落在 len 之内）。
 * 返回 1 = 匹配，返回 0 = 不匹配 */
uint8_t ESP_MatchAtIn(const char *buf, uint16_t len, uint16_t pos, const char *token);

/* 在 buf[0..len) 里任意位置查找 token。
 * 返回 1 = 找到，返回 0 = 没找到 */
uint8_t ESP_FindToken(const char *buf, uint16_t len, const char *token);

#endif /* ESP_PARSE_H */
