#include "app.h"
#include "filter.h"
#include "dht11.h"
#include "dht11_frame.h"
#include "bsp_uart.h"
#include "fixed_str.h"
static Filter_T s_temp_filt;     /* 温度滑动平均器 */
static Filter_T s_humi_filt;     /* 湿度滑动平均器 */
int16_t  g_temp_now;
int16_t  g_humi_now;
int16_t  g_temp_avg;
int16_t  g_humi_avg;
uint8_t  g_alarm;
int16_t  g_temp_min = 32767;
int16_t  g_temp_max = -32768;
int16_t  g_humi_min = 32767;
int16_t  g_humi_max = -32768;
void App_Init(void)
{
    Filter_Init(&s_temp_filt);
    Filter_Init(&s_humi_filt);
}
void DHT_Process(void)
{
    uint8_t raw[DHT_FRAME_LEN];
    uint8_t i;

    /* 先把驱动里的 5 个原始字节取出来，再交给纯函数解码。
     * 解码规则在 Drivers/dht11_frame.c，那边能在 PC 上喂已知帧做测试。 */
    for (i = 0U; i < DHT_FRAME_LEN; i++) raw[i] = DHT_RawByte(i);

    g_temp_now = DHT_DecodeTemp(raw);
    g_humi_now = DHT_DecodeHumi(raw);

    Filter_Push(&s_temp_filt, g_temp_now);
    Filter_Push(&s_humi_filt, g_humi_now);

    g_temp_avg = Filter_Avg(&s_temp_filt);
    g_humi_avg = Filter_Avg(&s_humi_filt);

    /* ---- 更新历史极值 ----
     * 【2026-10-06 修复】原 v1.2 只声明了这 4 个变量、从未赋值，
     * 导致「极值画面」恒显示 int16_t 的两个极端值（3276.7 / -3276.8）。
     * 用滤波后的值来比，免得单次跳变把极值带偏。 */
    if (g_temp_avg > g_temp_max) g_temp_max = g_temp_avg;
    if (g_temp_avg < g_temp_min) g_temp_min = g_temp_avg;
    if (g_humi_avg > g_humi_max) g_humi_max = g_humi_avg;
    if (g_humi_avg < g_humi_min) g_humi_min = g_humi_avg;

    /* 用滤波后的值判断报警，避免单次跳变引起误报 */
    g_alarm = ((g_temp_avg > TEMP_ALARM_X10) || (g_humi_avg > HUMI_ALARM_X10)) ? 1U : 0U;
}
/* 把本次结果打到串口：原始 5 字节 + 当前值 + 滤波值，出问题时最好排查 */
void DHT_PrintUart(void)
{
    uint8_t i;

    UART_Puts("[DHT11] raw:");
    for (i = 0U; i < 5U; i++)
    {
        UART_PutC(' ');
        UART_PutHex(DHT_RawByte(i));
    }
    UART_Puts(" | Temp ");  UART_PutFix1(g_temp_now);  UART_Puts("C");
    UART_Puts("  Humi ");   UART_PutFix1(g_humi_now);  UART_Puts("%");
    UART_Puts(" | 滤波后 ");UART_PutFix1(g_temp_avg);  UART_Puts("C");
    UART_Puts(" ");         UART_PutFix1(g_humi_avg);  UART_Puts("%");
    UART_Puts(g_alarm ? " | [报警]\r\n" : " | [正常]\r\n");
}
