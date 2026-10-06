#include "fixed_str.h"
/* ---- 下面四个是"拼字符串"的小工具：都返回新的"末尾下标"，方便连续调用 ----
 * 用法：
 *      uint8_t n = 0U;
 *      n = StrAppendStr(line, n, "Temp: ");
 *      n = StrAppendFix1(line, n, g_temp_avg);
 *      n = StrPadTo(line, n, 16U);
 *      OLED_ShowStr(0, 0, line);
 * 为什么不用 sprintf？标准库会把一大坨格式化代码和浮点支持链接进来，
 * 单片机 Flash 只有 64KB，能省就省，自己写几行反而更快更小。
 */

/* 追加一个字符串 */
uint8_t StrAppendStr(char *buf, uint8_t n, const char *s)
{
    while (*s != '\0')
    {
        buf[n] = *s;
        n++;
        s++;
    }
    buf[n] = '\0';
    return n;
}

/* 追加一个无符号整数（自己取余拆位） */
uint8_t StrAppendU32(char *buf, uint8_t n, uint32_t v)
{
    char    tmp[11];
    uint8_t k = 0U;

    if (v == 0U)
    {
        buf[n] = '0';
        n++;
        buf[n] = '\0';
        return n;
    }
    while ((v > 0U) && (k < 10U))     /* 末位先进数组，所以是倒序 */
    {
        tmp[k] = (char)('0' + (v % 10U));
        v /= 10U;
        k++;
    }
    while (k > 0U)                    /* 倒着取出来就是正序 */
    {
        k--;
        buf[n] = tmp[k];
        n++;
    }
    buf[n] = '\0';
    return n;
}

/* 追加"带一位小数的数值"。v10 是放大 10 倍后的整数：-105 显示成 "-10.5" */
uint8_t StrAppendFix1(char *buf, uint8_t n, int16_t v10)
{
    uint16_t a;

    if (v10 < 0) { buf[n] = '-'; n++; a = (uint16_t)(-v10); }
    else         { a = (uint16_t)v10; }

    n = StrAppendU32(buf, n, (uint32_t)(a / 10U));   /* 整数部分 */
    buf[n] = '.';
    n++;
    buf[n] = (char)('0' + (a % 10U));                /* 小数部分 */
    n++;
    buf[n] = '\0';
    return n;
}

/* 末尾补空格到指定长度。作用：同一行新旧内容长度不一样时，
 * 短的那一次会把上一次多出来的字一起擦掉，屏幕上不留残影 */
uint8_t StrPadTo(char *buf, uint8_t n, uint8_t width)
{
    while (n < width)
    {
        buf[n] = ' ';
        n++;
    }
    buf[n] = '\0';
    return n;
}

/* 追加一个两位数字，不足两位时前面补 '0'（显示 "10:05:03" 这种时间要用）
 * 不做这个补零的话，9 点 5 分 3 秒会显示成 "9:5:3"，看上去不像时间 */
uint8_t StrAppendPad2(char *buf, uint8_t n, uint32_t v)
{
    if (v > 99U) v = 99U;
    buf[n] = (char)('0' + (v / 10U));  n++;
    buf[n] = (char)('0' + (v % 10U));  n++;
    buf[n] = '\0';
    return n;
}
