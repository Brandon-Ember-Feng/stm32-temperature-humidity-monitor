#ifndef UI_PAGES_H
#define UI_PAGES_H
/* =============================================================
 * ui_pages.h —— OLED 各显示画面
 * 画面 0 温湿度 / 1 时间 / 2 极值 / 3 历史 / 4 WiFi
 * 短按 K1 循环切换；长按 1 秒直接回画面 0（逃生门）
 * ============================================================= */
#include <stdint.h>
#define MODE_NUM        5U          /* 一共几个显示画面 */
/* 历史记录每页显示几条。
 * 【为什么是 3 而不是 4 —— 这是踩过坑的数字】
 *   OLED 是 128x64，纵向 64 像素被切成 8 个 page（每 page 8 像素）。
 *   字库是 8x16 点阵，一个字符要占「上半 page + 下半 page」两格，
 *   所以整屏实际只有 4 行字符位置：page 0 / 2 / 4 / 6。
 *   历史画面需要 1 行标题，剩下的 3 行才是数据。
 *   【曾经写成 4 条的后果】数据行从 page 1 起排（1/3/5/7）：
 *   ① 第 1 条覆盖标题下半截；② 每条上下错开 8 像素、互相重叠；
 *   ③ 第 4 条落在 page 7，而 OLED_ShowChar 有 `page > 6 直接 return`
 *   的保护，整行一个字都不画。整页糊成一团。 */
#define HIST_PER_PAGE   3U          /* 每页 3 条（+1 行标题 = 4 行占满） */
#define HIST_ROW_TOP    2U          /* 第 1 条数据画在第 2 行，即 page 2 */
/* 历史画面只允许翻看「最近 HIST_MAX 条」，而不是全部 2728 条。
 * 原因：① g_hist_page 是 uint8_t，全部 2728 条 = 910 页 > 255 会回绕；
 *       ② 用户想看的就是「最近发生了什么」。200 条 = 67 页，刚好。 */
#define HIST_MAX        200U
/* ---- 界面状态 ---- */
extern uint8_t g_mode;        /* 当前画面：0=温湿度 1=时间 2=极值 3=历史 4=WiFi */
extern uint8_t g_hist_page;   /* 历史画面：当前看第几页 */
void OLED_Refresh(void);      /* 按 g_mode 重绘当前画面 */
void OLED_PageHistory(void);  /* 单独重绘历史页（翻页时用）*/
void OLED_PageTime(void);     /* 单独重绘时间页（每秒跳秒）*/
void OLED_PageWifi(void);     /* 单独重绘 WiFi 页 */
#endif /* UI_PAGES_H */
