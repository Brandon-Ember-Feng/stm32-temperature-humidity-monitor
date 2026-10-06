#ifndef TEST_SUITES_H
#define TEST_SUITES_H
/* =============================================================
 * suites.h —— 各测试套件的对外入口
 * 每个 suite 文件对应一个被测模块，函数名就是 suite_<模块名>。
 * ============================================================= */

void suite_filter(void);        /* Util/filter.c       滑动平均 */
void suite_fixed_str(void);     /* Util/fixed_str.c    定点转字符串 */
void suite_dht11_frame(void);   /* Drivers/dht11_frame.c  帧解码 */
void suite_hist_index(void);    /* Drivers/hist_index.c   记录寻址 */
void suite_esp_parse(void);     /* Drivers/esp_parse.c    AT 应答匹配 */

#endif /* TEST_SUITES_H */
