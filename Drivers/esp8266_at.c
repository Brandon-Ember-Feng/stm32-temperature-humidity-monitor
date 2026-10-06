#include "esp8266_at.h"
#include "esp_parse.h"
#include "bsp_uart.h"
#include "bsp_time.h"
#include "fixed_str.h"
/* ==========================================================================
 * 15. ESP8266（ESP-01S）—— USART2 + AT 指令 + 自建热点推数据
 *
 *  【为什么用 AT 指令，而不是直接给 ESP8266 写固件】
 *     ESP8266 本身是一颗"带 WiFi 的单片机"，可以往里烧自己写的程序；
 *     但那样就要再学一套工具链，两颗芯片的程序还会互相耦合、一起调试。
 *     这里把 ESP8266 当成一个"串口转 WiFi 的模块"用：STM32 只发文本命令、
 *     读文本应答，职责清晰 —— 而且模块本身可以用电脑串口助手单独验证，
 *     一次只留一个未知数。这是嵌入式里非常常见的分工。
 *
 *  【接线】PA2(USART2_TX) -> 模块 RXD      PA3(USART2_RX) <- 模块 TXD  （交叉！）
 *    CH_PD 必须接 3.3V（不接模块毫无反应），RST/GPIO0/GPIO2 悬空，VCC 只认 3.3V。
 *
 *  【为什么单独占一条 USART2】
 *    USART1 留给 CH340 打调试日志。两条串口独立，就能同时看到
 *    "STM32 发了什么 AT 指令 / 模块回了什么" 和 "温湿度业务日志"，
 *    排查时不必靠猜 —— 这是本方案最重要的一个设计决定。
 *
 *  【数据通路】
 *    手机 --连热点 ESP_TEMP--> ESP-01S（内置 AP + TCP Server:8080）
 *         --USART2（AT 指令）--> STM32 每 2 秒推一行温湿度
 *
 *  【本模块实测参数（脱机测出来的，不是抄文档）】
 *    AT 版本 1.1.0.0 / SDK 1.5.4，出厂波特率 115200；
 *    AT+CWSAP 用 **4 个参数**；没有客户端时 CIPSEND 回 "link is not valid"；
 *    用 ATE0 关掉回显后应答最干净。
 * ========================================================================== */

#define BSP_UART2_BAUD        115200U     /* 实测值：这块模块出厂就是 115200 */
#define ESP_AP_SSID     "ESP_TEMP"
#define ESP_AP_PWD      "12345678"
#define ESP_TCP_PORT    8080U

#define BSP_UART2_RX_BUF      192U        /* 收模块应答的缓冲区，够放下一条完整回显 */
#define ESP_PROMPT_MS   1000U       /* 等 '>' 提示符的上限。2026-09-21 实测：连发 12 次，
                                     * 提示符耗时 208~219ms（抖动仅 11ms）。留 4.5 倍余量，
                                     * 因为重连后第一次发送、或模块繁忙时可能更慢；
                                     * 提示符按时到达时这个上限根本不会消耗到，所以给宽无害。 */
#define ESP_SENDOK_MS   1000U       /* 等 SEND OK 的上限 */
#define ESP_KA_MS       30000U      /* 心跳周期：每 30 秒确认一次模块还活着 */

/* 是否把"每 2 秒一帧"的数据交互也打到 CH340 上？
 *   0 = 只打启动配置和出错（默认）。否则每 2 秒两行 AT 日志会把业务日志淹掉。
 *   排查发送链路时改成 1，就能看到完整的 AT 一来一回。 */
#define ESP_LOG_FRAMES  0U

static uint8_t  g_esp_ready;        /* 1 = 热点 + TCP 服务器已就绪 */
static uint8_t  g_esp_link;         /* 1 = 已有 TCP 客户端连着 */
static uint32_t g_esp_sent;         /* 成功推送的数据帧数 */
static uint32_t g_esp_fail;         /* 推送失败次数 */
static uint32_t g_esp_last_ka;      /* 上次心跳的时刻 */
static char     g_esp_cmd[48];          /* 拼 AT 指令用 */
/* ---------------- 15.2 关键词查找 ---------------- */

/* 判断缓冲里从 pos 开始是不是给定字符串 */
/* 下面两个是本驱动对「纯解析函数」的适配层：把 BSP 的接收缓冲喂给
 * Drivers/esp_parse.c 里的纯函数。这样匹配逻辑能在 PC 上单测，
 * 而调用点（ESP_TrackLink / ESP_WaitFor / ESP_SendFrame）一行都不用改。 */
static uint8_t ESP_MatchAt(uint16_t pos, const char *t)
{
    return ESP_MatchAtIn(BSP_Uart2_RxBuf(), BSP_Uart2_RxLen(), pos, t);
}

static uint8_t ESP_Has(const char *t)
{
    return ESP_FindToken(BSP_Uart2_RxBuf(), BSP_Uart2_RxLen(), t);
}

/* 解析模块**主动**上报的连接状态：0,CONNECT / 0,CLOSED
 *
 * 【为什么要专门写这一段】
 *   老固件（这版 1.1.0.0）的 AT+CIPSTATUS 只回一个 STATUS:5，
 *   根本看不出"有没有客户端连着"。想知道客户端上下线，
 *   只能靠模块自己吐出来的这两条消息。这是本方案里唯一的事件驱动部分。
 *
 * 【扫描游标 BSP_Uart2_ScanPos 的作用】只处理"新到的"字节，避免同一条 CONNECT 被反复判定。 */
static void ESP_TrackLink(void)
{
    uint16_t i;
    uint16_t limit;

    if (BSP_Uart2_RxLen() < 7U) return;

    /* 末尾 6 个字节留到下一轮再扫：关键词可能正好被切在两批数据中间 */
    limit = (uint16_t)(BSP_Uart2_RxLen() - 6U);

    for (i = BSP_Uart2_ScanPos(); i < limit; i++)
    {
        if (ESP_MatchAt(i, "CONNECT"))
        {
            BSP_Uart2_ScanMove((uint16_t)(i + 7U));
            if (g_esp_link == 0U)
            {
                g_esp_link = 1U;
                UART_Puts("[ESP] 手机已连上，开始推送数据\r\n");
            }
        }
        else if (ESP_MatchAt(i, "CLOSED"))
        {
            BSP_Uart2_ScanMove((uint16_t)(i + 6U));
            if (g_esp_link != 0U)
            {
                g_esp_link = 0U;
                UART_Puts("[ESP] 客户端断开（热点还在，等它重新连）\r\n");
            }
        }
    }

    if (BSP_Uart2_ScanPos() < limit) BSP_Uart2_ScanMove(limit);
}

/* ---------------- 15.3 发指令 / 等应答 ---------------- */

/* 等某个关键词出现，超时返回 0。
 * 等待期间也在收数据，所以模块中途上报 CONNECT/CLOSED 不会丢。 */
static uint8_t ESP_WaitFor(const char *token, uint32_t timeout_ms)
{
    uint32_t t0 = BSP_Millis();

    while ((BSP_Millis() - t0) < timeout_ms)
    {
        BSP_Uart2_RxPoll();
        ESP_TrackLink();
        if (ESP_Has(token)) return 1U;
        delay_ms(1U);
    }

    BSP_Uart2_RxPoll();                    /* 超时前再收一次，不然最后几个字节会漏 */
    ESP_TrackLink();
    return (ESP_Has(token) != 0U) ? 1U : 0U;
}

/* 把模块回显打成一整行：换行换成空格，否则一条应答会把日志撑成十几行 */
static void ESP_LogResp(void)
{
    uint16_t i;

    for (i = 0U; i < BSP_Uart2_RxLen(); i++)
    {
        char c = BSP_Uart2_RxAt(i);

        if ((c == '\r') || (c == '\n'))            UART_PutC(' ');
        else if ((c >= 0x20) && (c < 0x7F))        UART_PutC(c);
        else                                       UART_PutC('?');
    }
    UART_Puts("\r\n");
}

/* 发一条 AT 指令并等期望应答。log_it=1 时把一来一回都打到 CH340。 */
static uint8_t ESP_Cmd(const char *cmd, const char *expect, uint32_t timeout_ms, uint8_t log_it)
{
    uint8_t ok;

    /* 清空缓冲：上一次的应答不能混进来。
     * 关中断再清 —— 否则接收中断可能正好插在这三行中间把新字节写进来，
     * 出现"计数被清零、字节却已写入"的错位。几周期的关中断，代价可忽略。 */
    __asm volatile ("cpsid i" ::: "memory");
    BSP_Uart2_RxReset();
    __asm volatile ("cpsie i" ::: "memory");

    BSP_Uart2_Puts(cmd);
    BSP_Uart2_Puts("\r\n");
    if (log_it != 0U) { UART_Puts("[ESP>] "); UART_Puts(cmd); UART_Puts("\r\n"); }

    ok = ESP_WaitFor(expect, timeout_ms);

    if (log_it != 0U)
    {
        UART_Puts(ok ? "[ESP<] " : "[ESP<] (没等到期望应答) ");
        ESP_LogResp();
    }
    return ok;
}

/* ---------------- 15.4 上电配置：建热点 + 开 TCP 服务器 ---------------- */

static uint8_t ESP_Setup(void)
{
    uint8_t try;

    UART_Puts("[ESP] 开始配置：AP 模式 + 热点 + TCP 服务器\r\n");

    /* ① 关回显。模块默认把收到的指令原样回显一遍，关掉以后应答解析干净很多 */
    for (try = 0U; try < 3U; try++)
    {
        if (ESP_Cmd("ATE0", "OK", 800U, 1U)) break;
        delay_ms(200U);
    }

    /* ② 一条一条来。这四条的顺序不能换：
     *    先切 AP 模式 -> 才能建热点 -> 再开多连接(CIPMUX，AP 模式必须开)
     *    -> 最后开 TCP 服务器 */
    if (!ESP_Cmd("AT+CWMODE=2", "OK", 2000U, 1U))
    {
        UART_Puts("[ESP] AT+CWMODE=2 失败，模块可能没在 AT 状态\r\n");
        return 0U;
    }
    if (!ESP_Cmd("AT+CWSAP=\"" ESP_AP_SSID "\",\"" ESP_AP_PWD "\",5,3", "OK", 3000U, 1U))
    {
        UART_Puts("[ESP] 建热点失败（AT+CWSAP）\r\n");
        return 0U;
    }
    if (!ESP_Cmd("AT+CIPMUX=1", "OK", 2000U, 1U))
    {
        UART_Puts("[ESP] AT+CIPMUX=1 失败（AP 模式必须开多连接）\r\n");
        return 0U;
    }
    if (!ESP_Cmd("AT+CIPSERVER=1,8080", "OK", 3000U, 1U))
    {
        UART_Puts("[ESP] 开 TCP 服务器失败（AT+CIPSERVER）\r\n");
        return 0U;
    }

    /* ③ 清掉可能残留的 0 号链路。
     *    为什么需要：如果模块在 STM32 上电前就已经建过连接、而那条连接被对端
     *    粗暴掐断（RST），模块内部会留下一条"僵尸链路"——此后所有
     *    AT+CIPSEND=0 都回 `link is not valid`，而且 AT+CIPSTATUS 照样报
     *    STATUS:5 看着像连着。2026-09-21 实测踩到过这个坑。
     *    没有链路时这条指令回 `UNLINK` + `ERROR`，属正常提示，所以只看不断言。 */
    (void)ESP_Cmd("AT+CIPCLOSE=0", "OK", 1500U, 1U);

    /* ④ 回读一次 IP 当证据（回显里有 +CIFSR:APIP,"192.168.4.1"）。
     *    查询失败不算错误，只当参考 —— 老固件对查询指令支持不全。 */
    (void)ESP_Cmd("AT+CIFSR", "OK", 1500U, 1U);

    g_esp_ready = 1U;
    g_esp_link  = 0U;

    UART_Puts("[ESP] 就绪：热点 SSID=" ESP_AP_SSID "  密码=" ESP_AP_PWD "\r\n");
    UART_Puts("[ESP] 手机连上热点后，建 TCP Client 连 192.168.4.1:8080\r\n");
    return 1U;
}

/* 上电初始化：配置串口 -> 跑一遍 AT 配置（失败重试一次） */
uint8_t ESP_Init(void)
{
    BSP_Uart2_Init();
    delay_ms(500U);              /* 等模块上电稳定：它上电会先吐一串启动信息 */

    if (ESP_Setup()) return 1U;

    UART_Puts("[ESP] 第一轮配置失败，隔 300ms 重试一次\r\n");
    delay_ms(300U);
    if (ESP_Setup()) return 1U;

    g_esp_ready = 0U;
    return 0U;
}

/* ---------------- 15.5 推送一帧数据 ---------------- */

/* 把当前温湿度推给 0 号客户端。
 *
 * 【AT+CIPSEND 的三步握手，一步都不能省】
 *    ① 发 "AT+CIPSEND=0,<字节数>"      声明"我要发 N 字节"
 *    ② 模块回一个 '>' 提示符            必须等到它，才能发内容
 *    ③ 发 N 字节内容，等 "SEND OK"       长度必须和声明的一模一样
 *  这中间不能插别的指令，所以整段是阻塞的（有超时兜底）。 */
uint8_t ESP_SendFrame(int16_t temp_x10, int16_t humi_x10, uint32_t seq, uint8_t alarm)
{
    char     pay[64];
    uint8_t  n = 0U;
    uint32_t len;

    /* 组装数据帧：T=29.3C H=50.0% #12 A=0
     * 用"键=值"的紧凑文本，手机端随便一个网络调试助手就能读，不用解析库；
     * 每帧以 \r\n 结尾，一条一行，肉眼和脚本都好处理。 */
    n = StrAppendStr(pay, n, "T=");
    n = StrAppendFix1(pay, n, temp_x10);
    n = StrAppendStr(pay, n, "C H=");
    n = StrAppendFix1(pay, n, humi_x10);
    n = StrAppendStr(pay, n, "% #");
    n = StrAppendU32(pay, n, seq);
    n = StrAppendStr(pay, n, " A=");
    n = StrAppendU32(pay, n, (uint32_t)alarm);
    n = StrAppendStr(pay, n, "\r\n");
    len = (uint32_t)n;

    /* ① 拼 "AT+CIPSEND=0,<长度>" */
    n = 0U;
    n = StrAppendStr(g_esp_cmd, n, "AT+CIPSEND=0,");
    n = StrAppendU32(g_esp_cmd, n, len);
    g_esp_cmd[n] = '\0';

    /* 同样关中断清缓冲，避免和接收中断抢同一个计数器 */
    __asm volatile ("cpsid i" ::: "memory");
    BSP_Uart2_RxReset();
    __asm volatile ("cpsie i" ::: "memory");

    BSP_Uart2_Puts(g_esp_cmd);
    BSP_Uart2_Puts("\r\n");
    if (ESP_LOG_FRAMES != 0U) { UART_Puts("[ESP>] "); UART_Puts(g_esp_cmd); UART_Puts("\r\n"); }

    /* ② 等 '>' —— 模块只在这个字符出现之后才收内容 */
    if (!ESP_WaitFor(">", ESP_PROMPT_MS))
    {
        g_esp_fail++;
        UART_Puts("[ESP] 没等到 '>' 提示符，模块回显: ");
        ESP_LogResp();

        /* 模块没上报 CLOSED、但已经发不出去了 —— 靠这句错误文本补判 */
        if (ESP_Has("link is not valid"))
        {
            g_esp_link = 0U;
            UART_Puts("[ESP] 客户端其实已断开（link is not valid）\r\n");
        }
        return 0U;
    }

    /* ③ 发内容本体（不能多一个字节、也不能少一个） */
    if (ESP_LOG_FRAMES != 0U) { UART_Puts("[ESP>] "); UART_Puts(pay); }

    BSP_Uart2_Puts(pay);

    /* ④ 等 SEND OK */
    if (!ESP_WaitFor("SEND OK", ESP_SENDOK_MS))
    {
        g_esp_fail++;
        UART_Puts("[ESP] 没收到 SEND OK，模块回显: ");
        ESP_LogResp();
        return 0U;
    }

    g_esp_sent++;
    if (ESP_LOG_FRAMES != 0U) UART_Puts("[ESP<] SEND OK\r\n");
    return 1U;
}

/* ---------------- 15.6 心跳与自动恢复 ---------------- */

/* 每 30 秒确认模块还在。
 *
 * 【为什么必须做这件事（断线重连）】
 *   ESP-01S 峰值电流 300mA+，供电一瞬跌（比如手机刚连上、发射突然变强）
 *   就可能自己复位。复位后**热点和 TCP 服务器全部丢失**，但 STM32 这边
 *   毫无察觉 —— 代码看起来一切正常，用户那边却是"怎么突然连不上了"。
 *   定期发一条 AT，不通就重跑一遍初始化，它就能自己爬起来。 */
void ESP_KeepAlive(void)
{
    if ((BSP_Millis() - g_esp_last_ka) < ESP_KA_MS) return;
    g_esp_last_ka = BSP_Millis();

    if (!ESP_Cmd("AT", "OK", 500U, 0U))
    {
        UART_Puts("[ESP] 模块无应答（很可能复位了），重新初始化...\r\n");
        g_esp_ready = 0U;
        g_esp_link  = 0U;
        delay_ms(500U);

        if (ESP_Setup()) UART_Puts("[ESP] 已恢复：热点重新建立\r\n");
        else             UART_Puts("[ESP] 恢复失败，30 秒后再试\r\n");
    }
    else if (g_esp_link == 0U)
    {
        UART_Puts("[ESP] 心跳正常，等待手机连接（SSID " ESP_AP_SSID "）\r\n");
    }
}

/* 主循环每 10ms 调一次：收上报 + 心跳。全程非阻塞，不影响按键响应 */
void ESP_RxService(void)
{
    if (g_esp_ready == 0U) return;

    BSP_Uart2_RxPoll();
    ESP_TrackLink();
    ESP_KeepAlive();
}
/* ---- 状态查询：把内部状态用只读接口暴露出去，避免上层直接摸变量 ---- */
uint8_t  ESP_IsReady(void)  { return g_esp_ready; }
uint8_t  ESP_IsLinked(void) { return g_esp_link;  }
uint32_t ESP_SentCount(void){ return g_esp_sent;  }
