#include "filter.h"
void Filter_Init(Filter_T *f)
{
    uint8_t i;

    for (i = 0U; i < FILTER_N; i++) f->buf[i] = 0;
    f->idx = 0U;
    f->cnt = 0U;
}
/* 存入一个样本：写满一圈就从 0 覆盖，永远保留最近的 FILTER_N 次 */
void Filter_Push(Filter_T *f, int16_t v)
{
    f->buf[f->idx] = v;
    f->idx++;
    if (f->idx >= FILTER_N) f->idx = 0U;
    if (f->cnt < FILTER_N) f->cnt++;
}
uint16_t Filter_Count(const Filter_T *f) { return f->cnt; }
/* 求平均。刚上电时样本还没攒够，就按实际的个数算 */
int16_t Filter_Avg(const Filter_T *f)
{
    int32_t sum = 0;
    uint8_t i;

    if (f->cnt == 0U) return 0;
    for (i = 0U; i < f->cnt; i++) sum += (int32_t)f->buf[i];
    return (int16_t)(sum / (int32_t)f->cnt);
}
