#include "bsp_spi.h"
#include "bsp_reg.h"
/* =============================================================
 * SPI1：W25Q64 用，PA4=CS / PA5=SCK / PA6=MISO / PA7=MOSI
 * 模式 0（CPOL=0 CPHA=0），时钟 PCLK2/16 = 4MHz
 * ============================================================= */
void SPI1_Init(void)
{
    RCC_APB2ENR |= (1U << 2) | (1U << 12);   /* 使能 GPIOA、SPI1 时钟 */

    /* PA5(SCK) / PA7(MOSI)：复用推挽输出 50MHz
     *   复用推挽：CNF=10, MODE=11 -> 10<<2|11 = 1011b = 0xB
     *   这两个脚由 SPI 外设自己驱动，所以必须配成"复用"而不是"通用输出"——
     *   配成通用输出的症状是：软件怎么写 ODR 都没用，时钟线一动不动。
     *   nibble 布局：引脚 5 在第 20~23 位，引脚 7 在第 28~31 位 */
    GPIOA_CRL &= ~(0x0FU << 20);
    GPIOA_CRL |=  (0x0BU << 20);
    GPIOA_CRL &= ~(0x0FU << 28);
    GPIOA_CRL |=  (0x0BU << 28);

    /* PA6(MISO)：浮空输入
     *   浮空输入：CNF=01, MODE=00 -> 01<<2|00 = 0100b = 0x4
     *   为什么不配上拉？因为 Flash 的 DO 脚是主动驱动（推挽），
     *   不需要外部上拉来"扶"住电平；悬空时读到的噪声也不会误判成有效数据。
     *   引脚 6 在第 24~27 位 */
    GPIOA_CRL &= ~(0x0FU << 24);
    GPIOA_CRL |=  (0x04U << 24);

    /* PA4(CS)：通用推挽输出 50MHz
     *   通用推挽：CNF=00, MODE=11 -> 00<<2|11 = 0011b = 0x3
     *   注意这里是"通用"不是"复用"——CS 由软件手动控制，
     *   因为一条 SPI 总线上可以挂多个从机，各自用一根 CS 点名，
     *   硬件 NSS 只有一个，不够用。
     *   引脚 4 在第 16~19 位 */
    GPIOA_CRL &= ~(0x0FU << 16);
    GPIOA_CRL |=  (0x03U << 16);

    SPI1_CS_High();                     /* 空闲时片选拉高（不选中任何器件） */

    /* SPI1 控制寄存器 1 配置：
     *   BR[2:0] = 011 -> 分频 16，PCLK2=64MHz / 16 = 4MHz
     *   （W25Q64 最高支持 80MHz，4MHz 属于很保守的速度，稳定优先）
     *   MSTR=1 主模式   CPOL=0 CPHA=0 模式0   DFF=0 8 位   SSM+SSI 软件管理 NSS
     *   SPE=1 最后使能（要先配好其他位再打开，否则配置期间会误发时钟） */
    SPI1_CR1 = (0x03U << 3)    /* BR   = 011 -> 4MHz  */
             | (1U    << 2)    /* MSTR = 1   -> 主机  */
             | (1U    << 8)    /* SSI  = 1   -> 内部 NSS 拉高（主机才不会被当成从机） */
             | (1U    << 9)    /* SSM  = 1   -> NSS 软件管理，忽略 PA4 的硬件 NSS 功能 */
             | (1U    << 6);   /* SPE  = 1   -> 使能 SPI */
}

/* 收发一个字节（SPI 是全双工：发的同时也在收，所以叫"交换"）
 * 流程：等 TXE（发送缓冲区空）-> 写入要发的数据 -> 等 RXNE（收到了）-> 读出来 */
uint8_t SPI1_SwapByte(uint8_t dat)
{
    while ((SPI1_SR & (1U << 1)) == 0U);   /* TXE = 1 表示可以写了 */
    SPI1_DR = (uint32_t)dat;               /* 写 DR 就自动开始发 8 个时钟 */

    while ((SPI1_SR & (1U << 0)) == 0U);   /* RXNE = 1 表示收到了一个字节 */
    return (uint8_t)SPI1_DR;               /* 读 DR 会把 RXNE 自动清掉 */
}
/* ---- 软件片选（PA4，低电平选中） ---- */
void SPI1_CS_Low(void)  { GPIOA_BSRR = (1U << (4U + 16U)); }
void SPI1_CS_High(void) { GPIOA_BSRR = (1U << 4U); }
