/* =============================================================
 * host_test_main.c —— 测试运行器：框架实现 + 入口
 *
 * 编译方式见 Makefile 的 test 目标。要点是**只给 -IUtil -IDrivers**，
 * 不给 -IBSP —— 一旦有被测文件偷偷依赖了 BSP 头，PC 端编译会立刻失败。
 * 这条边界由构建配置守着，不需要额外写检查规则。
 * ============================================================= */
#include "framework.h"
#include "suites.h"

int         tf_cases_total;
int         tf_cases_failed;
int         tf_checks_total;
int         tf_checks_failed;
int         tf_case_failed_now;
const char *tf_first_fail_at;
char        tf_first_fail_msg[256];

void tf_suite_begin(const char *suite)
{
    printf("\n-- %s\n", suite);
}

void tf_run(const char *name, void (*fn)(void))
{
    int before = tf_checks_failed;

    tf_cases_total++;
    tf_case_failed_now = 0;
    tf_first_fail_at  = "";
    tf_first_fail_msg[0] = '\0';

    fn();

    if (tf_checks_failed > before)
    {
        tf_cases_failed++;
        printf("  FAIL  %s\n", name);
        printf("        %s  %s\n", tf_first_fail_at, tf_first_fail_msg);
    }
    else
    {
        printf("  ok    %s\n", name);
    }
}

int tf_report(void)
{
    printf("\n");
    printf("==================================================\n");
    printf("cases : %d run, %d failed\n", tf_cases_total, tf_cases_failed);
    printf("checks: %d run, %d failed\n", tf_checks_total, tf_checks_failed);
    printf("%s\n", (tf_cases_failed == 0) ? "RESULT: PASS" : "RESULT: FAIL");
    printf("==================================================\n");

    return (tf_cases_failed == 0) ? 0 : 1;
}

int main(void)
{
    printf("======================================================================\n");
    printf("PC-side unit tests - STM32 temperature/humidity monitor\n");
    printf("under test: Util/filter.c  Util/fixed_str.c  Drivers/dht11_frame.c\n");
    printf("            Drivers/hist_index.c  Drivers/esp_parse.c\n");
    printf("======================================================================\n");

    suite_filter();
    suite_fixed_str();
    suite_dht11_frame();
    suite_hist_index();
    suite_esp_parse();

    return tf_report();
}
