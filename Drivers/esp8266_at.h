#ifndef ESP8266_AT_H
#define ESP8266_AT_H
/* =============================================================
 * esp8266_at.h —— ESP-01S（WiFi）AT 指令驱动
 *
 * 方案：模块自建热点(AP) + TCP 服务器，手机连上后用 TCP Client
 * 连 192.168.4.1:8080，每 2 秒收到一行温湿度。
 * 物理层（USART2 收发 + 接收中断）在 BSP/bsp_uart.c。
 *
 * 实测参数（脱机测出来的，不是抄文档）：AT 1.1.0.0 / SDK 1.5.4，
 * 出厂 115200；AT+CWSAP 用 4 个参数。
 * ============================================================= */
#include <stdint.h>
#define ESP_AP_SSID     "ESP_TEMP"
#define ESP_AP_PWD      "12345678"
#define ESP_TCP_PORT    8080U
#define ESP_PROMPT_MS   1000U   /* 等 '>' 提示符上限（实测 208~219ms，留 4.5 倍余量）*/
#define ESP_SENDOK_MS   1000U   /* 等 SEND OK 的上限 */
#define ESP_KA_MS       30000U  /* 心跳周期：每 30 秒确认一次模块还活着 */
/* 是否把「每 2 秒一帧」的数据交互也打到 CH340 上？
 *   0 = 只打启动配置和出错（默认）。排查发送链路时改成 1。 */
#define ESP_LOG_FRAMES  0U
uint8_t ESP_Init(void);        /* 初始化：配 AP + 开 TCP 服务器，1 = 成功 */
/* 推一帧温湿度给已连上的手机客户端。
 * 温湿度由调用方（应用层）传入，驱动层不反向依赖应用层状态。 */
uint8_t ESP_SendFrame(int16_t temp_x10, int16_t humi_x10, uint32_t seq, uint8_t alarm);
void    ESP_RxService(void);   /* 主循环每 10ms 调一次：收上报 + 心跳    */
void    ESP_KeepAlive(void);   /* 心跳：30 秒一次，不通就重跑初始化      */
/* ---- 状态查询（供 OLED 画面 4 显示） ---- */
uint8_t  ESP_IsReady(void);
uint8_t  ESP_IsLinked(void);
uint32_t ESP_SentCount(void);
#endif /* ESP8266_AT_H */
