#include "ui_pages.h"
#include "ssd1306.h"
#include "fixed_str.h"
#include "hist_store.h"
#include "dht11.h"
#include "app.h"
#include "esp8266_at.h"
#include "bsp_uart.h"
#include "bsp_time.h"
uint8_t g_mode;          /* 当前画面 */
uint8_t g_hist_page;     /* 历史画面当前页 */

/* 第 3 行（画面 0/1 共用）：状态提示
 *   本次读取失败 -> FAIL(err 编号)，这样不用连串口也能知道错在哪一步
 *   正常         -> ALARM / Normal */
static void OLED_ShowStatus(void)
{
    char    line[32];
    uint8_t n;

    n = StrAppendStr(line, 0U, "Status: ");
    if (DHT_Error() != 0U)
    {
        n = StrAppendStr(line, n, "FAIL(");
        n = StrAppendU32(line, n, DHT_Error());
        n = StrAppendStr(line, n, ")");
    }
    else
    {
        n = StrAppendStr(line, n, (g_alarm != 0U) ? "ALARM" : "Normal");
    }
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(4, 0, line);
}

/* 第 4 行（画面 0/1 共用）：采样统计 + 当前画面号
 * 显示 "M2/4" 是为了让人知道现在在看第几个画面、一共几个，方便按键切换 */
static void OLED_ShowFooter(void)
{
    char    line[32];
    uint8_t n;

    n = StrAppendStr(line, 0U, "OK:");
    n = StrAppendU32(line, n, DHT_OkCount());
    n = StrAppendStr(line, n, " F:");
    n = StrAppendU32(line, n, DHT_FailCount());
    n = StrAppendStr(line, n, " M");
    n = StrAppendU32(line, n, (uint32_t)g_mode + 1U);   /* 从 1 开始数，看着自然 */
    n = StrAppendStr(line, n, "/");
    n = StrAppendU32(line, n, MODE_NUM);
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(6, 0, line);
}

/* 画面 0：温湿度 —— 画第 1、2 行 */
static void OLED_PageData(void)
{
    char    line[32];
    uint8_t n;

    n = StrAppendStr(line, 0U, "Temp: ");
    n = StrAppendFix1(line, n, g_temp_avg);
    line[n] = (char)CH_DEGREE;  n++;
    line[n] = 'C';              n++;
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(0, 0, line);

    n = StrAppendStr(line, 0U, "Humi: ");
    n = StrAppendFix1(line, n, g_humi_avg);
    line[n] = '%';              n++;
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(2, 0, line);
}

/* 画面 1：时间 —— 画第 1、2 行。
 * 时间从开机算起（开机 = 00:00:00），由 TIM2 中断累加的 BSP_Millis() 换算而来。
 * 这个函数在主循环里每秒被调用一次，所以秒位是"跳着走"的，不会卡住。 */
void OLED_PageTime(void)
{
    char     line[32];
    uint8_t  n;
    uint32_t hh, mm, ss;

    Time_Get(&hh, &mm, &ss);

    n = StrAppendStr(line, 0U, "Time: ");
    n = StrAppendPad2(line, n, hh);  line[n] = ':';  n++;
    n = StrAppendPad2(line, n, mm);  line[n] = ':';  n++;
    n = StrAppendPad2(line, n, ss);
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(0, 0, line);

    /* 第二行顺便显示总秒数：它单调递增，一眼就能看出计时真的在走，
     * 比只显示"时:分:秒"更有说服力 */
    n = StrAppendStr(line, 0U, "Uptime: ");
    n = StrAppendU32(line, n, BSP_Millis() / 1000U);
    n = StrAppendStr(line, n, "s");
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(2, 0, line);
}

/* 画面 2：历史极值 —— 4 行全部用来放极值（所以这一页不画状态行和统计行，
 * 想看状态按一下键切回画面 0 即可）。
 * 演示技巧：对着传感器哈一口气，Tmax/Hmax 会立刻往上走、且不会掉回来。 */
static void OLED_PageMinMax(void)
{
    char    line[32];
    uint8_t n;

    n = StrAppendStr(line, 0U, "Tmax: ");
    n = StrAppendFix1(line, n, g_temp_max);
    line[n] = (char)CH_DEGREE;  n++;
    line[n] = 'C';              n++;
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(0, 0, line);

    n = StrAppendStr(line, 0U, "Tmin: ");
    n = StrAppendFix1(line, n, g_temp_min);
    line[n] = (char)CH_DEGREE;  n++;
    line[n] = 'C';              n++;
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(2, 0, line);

    n = StrAppendStr(line, 0U, "Hmax: ");
    n = StrAppendFix1(line, n, g_humi_max);
    line[n] = '%';              n++;
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(4, 0, line);

    n = StrAppendStr(line, 0U, "Hmin: ");
    n = StrAppendFix1(line, n, g_humi_min);
    line[n] = '%';              n++;
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(6, 0, line);
}

/* 画面 3：历史记录 —— 从 W25Q64 里按顺序读出，每页 3 条。
 * 版面：  第 1 行  H第几页/共几页 N=总条数
 *         第 2~4 行  序号 温度 湿度
 *                     例：12 25.6 60.2
 * 翻页技巧：在这个画面上再按一下 K1 就往后翻一页（由主循环处理）。
 * 这里每次都现场从 Flash 读，虽然慢一点（读 3 条约几毫秒），
 * 但保证显示的一定是最新的数据，不会出现"内存缓存和 Flash 不一致"的问题。 */
void OLED_PageHistory(void)
{
    char     line[32];
    uint8_t  n, row;
    int16_t  t10, h10;
    uint32_t base, first;      /* first = 本页第一条记录在"总记录"里的绝对序号 */
    uint32_t show_cnt;

    if (!Hist_IsReady())
    {
        OLED_ShowStr(0, 0, "History: (no    ");
        OLED_ShowStr(2, 0, " W25Q64 module )");
        OLED_ShowStr(4, 0, " Check SPI wire:");
        OLED_ShowStr(6, 0, " CS4 CLK5 DO6 DI7");
        return;
    }

    if (Hist_Count() == 0U)
    {
        OLED_ShowStr(0, 0, "History: empty  ");
        OLED_ShowStr(2, 0, " Need 1 min to  ");
        OLED_ShowStr(4, 0, " record 1st one.");
        OLED_ShowStr(6, 0, " Total: 0       ");
        return;
    }

    /* 只展示最近的 HIST_MAX 条：超出部分从前面丢掉。
     * first 是"本页第 1 条"在总记录里的绝对下标；
     * 如果总记录数超过 HIST_MAX，就把起点往后推，只保留最新的那些。 */
    show_cnt = (Hist_Count() > HIST_MAX) ? HIST_MAX : Hist_Count();

    /* 页号从"最新的那些记录"的末尾往回算：
     *   第 0 页 = 最旧的可见记录，最后一页 = 最新的记录
     * base 是相对 show_cnt 的下标，加上 first 才是绝对下标 */
    base  = (uint32_t)g_hist_page * HIST_PER_PAGE;
    first = Hist_Count() - show_cnt;

    /* 第 1 行当标题栏：显示 "Hxx/yy  N=总数"，
     * 告诉用户"现在在第几页、一共几页、总共存了多少条"。
     * 没有这一行的话，翻页时完全不知道自己翻到哪了。 */
    {
        uint32_t pages = (show_cnt + HIST_PER_PAGE - 1U) / HIST_PER_PAGE;
        if (pages == 0U) pages = 1U;

        n = StrAppendStr(line, 0U, "H");
        n = StrAppendU32(line, n, (uint32_t)g_hist_page + 1U);
        line[n] = '/';  n++;
        n = StrAppendU32(line, n, pages);
        n = StrAppendStr(line, n, " N=");
        n = StrAppendU32(line, n, Hist_Count());
        n = StrPadTo(line, n, 16U);
        OLED_ShowStr(0, 0, line);
    }

    for (row = 0U; row < HIST_PER_PAGE; row++)
    {
        uint32_t rel  = base + row;                          /* 相对 show_cnt 的下标 */
        /* 行号换算成 SSD1306 的 page：第 1 行(page0+1)留给标题，
         * 数据从第 2 行(page 2)开始，每行占两个 page，所以是 2/4/6。
         * 千万不能再写成 row*2+1 —— 那样每条都会压住上一行的下半截。 */
        uint8_t  rpag = (uint8_t)(HIST_ROW_TOP + row * 2U);

        if (rel >= show_cnt)
        {
            n = StrAppendStr(line, 0U, "  --            ");   /* 空位用横线占位 */
            n = StrPadTo(line, n, 16U);
            OLED_ShowStr(rpag, 0, line);
            continue;
        }

        if (!W25_RecRead(first + rel, &t10, &h10))
        {
            n = StrAppendStr(line, 0U, "  read err     ");
            n = StrPadTo(line, n, 16U);
            OLED_ShowStr(rpag, 0, line);
            continue;
        }

        /* 格式："1234 25.6 60.2"（序号 + 温度 + 湿度），正好 16 格以内。
         * 不写单位符号是为了省宽度——这一页有标题栏提示，单位不会误解。
         * 序号从 1 开始数（first + rel 是 0 起算的下标，显示时 +1），
         * 否则第一条记录会显示成 "0"，看着像程序算错了。
         * 【怎么换算时间】每 1 分钟存一条，所以"序号差 = 分钟差"：
         * 最后一条是刚才，往上数 5 条就是大约 5 分钟前。 */
        n = StrAppendU32(line, 0U, first + rel + 1U);
        line[n] = ' ';  n++;
        n = StrAppendFix1(line, n, t10);
        line[n] = ' ';  n++;
        n = StrAppendFix1(line, n, h10);
        n = StrPadTo(line, n, 16U);
        OLED_ShowStr(rpag, 0, line);
    }
}
/* 画面 4（WiFi）的函数体定义在第 15 章的 ESP8266 部分 —— 它要用到那一章里
 * 定义的 ESP_IsReady() / ESP_IsLinked() / ESP_SentCount() 三个状态变量，所以跟着放在一起。
 * 这里先声明一下，让本文件前面的调用合法（C 语言：先声明后使用）。 */
void OLED_PageWifi(void);

void OLED_Refresh(void)
{
    if (g_mode == 0U)
    {
        OLED_PageData();
        OLED_ShowStatus();
        OLED_ShowFooter();
    }
    else if (g_mode == 1U)
    {
        OLED_PageTime();
        OLED_ShowStatus();
        OLED_ShowFooter();
    }
    else if (g_mode == 2U)
    {
        OLED_PageMinMax();     /* 这一页 4 行都是极值 */
    }
    else if (g_mode == 3U)
    {
        OLED_PageHistory();    /* 这一页 4 行是历史记录 */
    }
    else
    {
        OLED_PageWifi();       /* 画面 4：WiFi 热点状态 */
    }
}

/* ---------------- 15.7 画面 4：WiFi 状态 ---------------- */

/* 这一页专门给演示用：一眼看出"热点起了没 / 手机连上了没 / 发了多少帧"。
 * 现场演示时这比看串口日志直观得多。 */
void OLED_PageWifi(void)
{
    char    line[32];
    uint8_t n;

    n = StrAppendStr(line, 0U, (ESP_IsReady() != 0U) ? "WiFi: AP OK" : "WiFi: FAIL");
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(0, 0, line);

    n = StrAppendStr(line, 0U, "SSID:");
    n = StrAppendStr(line, n, (ESP_IsReady() != 0U) ? ESP_AP_SSID : "-------");
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(2, 0, line);

    n = StrAppendStr(line, 0U, "IP: 192.168.4.1");
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(4, 0, line);

    n = StrAppendStr(line, 0U, (ESP_IsLinked() != 0U) ? "Client:1" : "Client:0");
    n = StrAppendStr(line, n, " TX:");
    n = StrAppendU32(line, n, ESP_SentCount());
    n = StrPadTo(line, n, 16U);
    OLED_ShowStr(6, 0, line);
}
