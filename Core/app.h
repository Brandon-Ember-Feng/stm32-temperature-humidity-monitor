#ifndef APP_H
#define APP_H
/* =============================================================
 * app.h —— 应用层：采样任务与共享状态
 *
 * 这里是「设备要做什么」，不关心「怎么操作寄存器」。
 * 滤波算法本身在 Util/filter.c（纯逻辑，可在 PC 上单元测试）。
 * ============================================================= */
#include <stdint.h>
/* 报警阈值（放大 10 倍存整数，300 表示 30.0 °C） */
#define TEMP_ALARM_X10    300
#define HUMI_ALARM_X10    800
/* ---- 共享状态 ----
 * 说明：应用层为单实例单线程（主循环 + 中断只写时基），无并发访问，
 * 故直接以 extern 暴露。若将来引入 RTOS，应改为访问器 + 互斥保护。 */
extern int16_t  g_temp_now;      /* 本次原始读数（×10） */
extern int16_t  g_humi_now;
extern int16_t  g_temp_avg;      /* 滤波后的显示值（×10） */
extern int16_t  g_humi_avg;
extern uint8_t  g_alarm;         /* 1 = 超过阈值 */
/* 历史极值：上电以来出现的最高/最低值（拿滤波后的值来比）。
 * 初值故意设成 int16_t 的两个极端值，第一次采样必定刷新它们，
 * 不需要额外的「是否第一次」标志位。 */
extern int16_t  g_temp_min;
extern int16_t  g_temp_max;
extern int16_t  g_humi_min;
extern int16_t  g_humi_max;
void App_Init(void);
/* 把原始字节解析成温湿度 -> 入环形缓冲区 -> 算平均值、更新极值、判报警 */
void DHT_Process(void);
/* 把本次结果打到串口：原始 5 字节 + 当前值 + 滤波值 */
void DHT_PrintUart(void);
#endif /* APP_H */
