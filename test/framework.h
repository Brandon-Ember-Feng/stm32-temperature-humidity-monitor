#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H
/* =============================================================
 * framework.h —— 极简 PC 端单元测试框架
 *
 * 为什么自己写而不用 Unity / CMocka：
 *   那两个都是两个 .c 文件、零依赖，确实很好用；但自己写一遍的价值在于
 *   每一行都讲得清 —— 面试官追问"断言失败怎么定位"、"宏展开成什么"，
 *   答得上来比简历上写一句"使用 Unity 测试框架"更有说服力。
 *   整个框架不到 100 行，且不依赖被测工程的任何代码。
 *
 * 为什么输出用英文：
 *   Windows 控制台默认是 GBK 代码页，UTF-8 的中文会变成乱码；
 *   CI（Linux）又是 UTF-8。用英文可以两边都不出错。
 *
 * 用法：
 *      TEST_CASE(名字) { CHECK_EQ_INT(实际, 期望); }
 *      void suite_xxx(void) {
 *          tf_suite_begin("filter");
 *          RUN_CASE(名字);
 *      }
 *      最后 return tf_report();
 * ============================================================= */
#include <stdio.h>
#include <string.h>

/* 计数器（定义在 host_test_main.c） */
extern int         tf_cases_total;
extern int         tf_cases_failed;
extern int         tf_checks_total;
extern int         tf_checks_failed;
extern int         tf_case_failed_now;      /* 当前用例内已失败的断言数 */
extern const char *tf_first_fail_at;        /* 当前用例第一条失败的位置 */
extern char        tf_first_fail_msg[256];  /* 以及它的说明 */

void tf_run(const char *name, void (*fn)(void));
void tf_suite_begin(const char *suite);
int  tf_report(void);

#define TEST_CASE(name)  static void name(void)
#define RUN_CASE(name)   tf_run(#name, name)

/* 把 __LINE__ 展开成字符串，好拼出 "文件:行号" */
#define TF_STR2(x)  #x
#define TF_STR1(x)  TF_STR2(x)
#define TF_LOC      __FILE__ ":" TF_STR1(__LINE__)

/* 记一次失败。只保存第一条的详情（足够定位），后面几条只计数。 */
#define TF_FAIL(msg_text)                                             \
    do {                                                              \
        if (tf_case_failed_now == 0)                                  \
        {                                                             \
            tf_first_fail_at = TF_LOC;                                \
            snprintf(tf_first_fail_msg, sizeof(tf_first_fail_msg),    \
                     "%s", (msg_text));                               \
        }                                                             \
        tf_checks_failed++;                                           \
        tf_case_failed_now++;                                         \
    } while (0)

#define CHECK(cond)                                                   \
    do {                                                              \
        tf_checks_total++;                                            \
        if (!(cond)) TF_FAIL(#cond);                                  \
    } while (0)

#define CHECK_EQ_INT(actual, expect)                                  \
    do {                                                              \
        long long _tf_a = (long long)(actual);                        \
        long long _tf_e = (long long)(expect);                        \
        char      _tf_m[192];                                         \
        tf_checks_total++;                                            \
        if (_tf_a != _tf_e)                                           \
        {                                                             \
            snprintf(_tf_m, sizeof(_tf_m),                            \
                     "%s == %lld, got %lld", #actual, _tf_e, _tf_a);  \
            TF_FAIL(_tf_m);                                           \
        }                                                             \
    } while (0)

#define CHECK_EQ_STR(actual, expect)                                  \
    do {                                                              \
        const char *_tf_a = (actual);                                 \
        const char *_tf_e = (expect);                                 \
        char        _tf_m[256];                                       \
        tf_checks_total++;                                            \
        if (strcmp(_tf_a, _tf_e) != 0)                                \
        {                                                             \
            snprintf(_tf_m, sizeof(_tf_m),                            \
                     "%s -> \"%s\", got \"%s\"", #actual, _tf_e, _tf_a); \
            TF_FAIL(_tf_m);                                           \
        }                                                             \
    } while (0)

#endif /* TEST_FRAMEWORK_H */
