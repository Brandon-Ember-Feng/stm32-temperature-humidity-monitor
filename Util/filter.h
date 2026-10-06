#ifndef UTIL_FILTER_H
#define UTIL_FILTER_H
/* =============================================================
 * filter.h —— 滑动平均滤波器（纯逻辑，无硬件依赖，可在 PC 上编译测试）
 *
 * 为什么必须滤波：DHT11 分辨率只有 1°C / 1%，响应又慢，直接显示会看到
 * "25 -> 26 -> 25" 来回跳。保存最近 N 次有效采样求平均，曲线就平滑了。
 * 为什么不用浮点：用 int16_t 存「放大 10 倍」的整数，全整数运算既快
 * 又不用链接浮点库。
 * ============================================================= */
#include <stdint.h>
#define FILTER_N   5U        /* 参与平均的采样个数 */
typedef struct
{
    int16_t buf[FILTER_N];   /* 环形缓冲区 */
    uint8_t idx;             /* 写指针     */
    uint8_t cnt;             /* 已攒到的有效样本数（最多 FILTER_N） */
} Filter_T;
void     Filter_Init(Filter_T *f);
void     Filter_Push(Filter_T *f, int16_t v);
uint16_t Filter_Count(const Filter_T *f);
int16_t  Filter_Avg(const Filter_T *f);
#endif /* UTIL_FILTER_H */
