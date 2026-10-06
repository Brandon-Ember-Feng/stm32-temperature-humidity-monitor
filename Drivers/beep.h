#ifndef BEEP_H
#define BEEP_H
/* =============================================================
 * beep.h —— 蜂鸣器（3 针有源模块：VCC / IO / GND，IO 接 PB5）
 * 本模块丝印「低电触发」-> BEEP_ACTIVE_HIGH = 0U
 * 极性与接线对不对，上电短鸣 200ms 就能定案；配反只会响反，不烧东西。
 * ============================================================= */
void Buzzer_Init(void);
void BEEP_On(void);
void BEEP_Off(void);
extern const char BEEP_TRIG_TXT[];   /* 中文描述，串口日志用 */
extern const char BEEP_TRIG_STR[];   /* 固定宽度，OLED 自检页用 */
#define BEEP_ACTIVE_HIGH   0U   /* ★ 低电平触发填 0；高电平触发填 1U */
#endif /* BEEP_H */
