#ifndef BSP_UART_H
#define BSP_UART_H
/* =============================================================
 * bsp_uart.h —— 两条串口，用途严格分离
 *   USART1 (PA9/PA10) : CH340 调试口，115200
 *   USART2 (PA2/PA3)  : ESP-01S 模块，115200，接收走中断
 * 分开的好处：能同时看到「主控发出去的指令」和「模块的回应」
 * ============================================================= */
#include <stdint.h>
/* ---- USART1：调试输出 ---- */
void UART_Init(void);
void UART_PutC(char c);
void UART_Puts(const char *s);
void UART_Putu(uint32_t v);
void UART_PutHex(uint8_t v);
void UART_PutFix1(int16_t v10);
/* ---- USART2：ESP-01S 收发 ---- */
#define BSP_UART2_BAUD     115200U
#define BSP_UART2_RX_BUF   192U
void BSP_Uart2_Init(void);
void BSP_Uart2_PutC(char c);
void BSP_Uart2_Puts(const char *s);
/* 安全网：把已在 DR 里的字节收进缓冲（正常由 RX 中断完成） */
void BSP_Uart2_RxPoll(void);
/* ---- USART2 接收缓冲的访问接口 ----
 * 缓冲由 BSP 独占（中断里写入），协议层只能通过下面这几个函数读和游标移动，
 * 不允许直接拿到数组本身 —— 否则「谁写的」就说不清了。
 * 游标语义是「协议层已经处理到第几个字节」，借放在 BSP 是为了
 * 缓冲被丢弃时能连游标一起复位（见 BSP_Uart2_RxReset）。 */
uint16_t BSP_Uart2_RxLen(void);             /* 缓冲里已有多少字节 */
char     BSP_Uart2_RxAt(uint16_t idx);      /* 取第 idx 个字节（越界安全返回 '\0'） */
uint16_t BSP_Uart2_ScanPos(void);           /* 协议层扫描游标 */
void     BSP_Uart2_ScanMove(uint16_t pos);  /* 移动扫描游标 */
void     BSP_Uart2_RxReset(void);           /* 缓冲 + 游标 一起清空 */
#endif /* BSP_UART_H */
