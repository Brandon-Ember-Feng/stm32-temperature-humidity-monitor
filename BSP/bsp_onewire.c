#include "bsp_onewire.h"
#include "bsp_reg.h"
/* PB0 -> 推挽输出 50MHz（用来主动拉低总线）
 *   通用推挽 50MHz：CNF=00, MODE=11 -> 00<<2|11 = 0011b = 0x3 */
void OW_PinOut(void)
{
    GPIOB_ODR &= ~(1U << 0);                             /* 先定好输出电平：低 */
    GPIOB_CRL  = (GPIOB_CRL & ~0x0FU) | 0x03U;
}
/* PB0 -> 上拉输入（松手不驱动，同时开内部上拉，并可供读电平）
 *   上拉输入：CNF=10, MODE=00 -> 10<<2|00 = 1000b = 0x8 */
void OW_PinIn(void)
{
    GPIOB_ODR |=  (1U << 0);                             /* ODR=1 表示输入时启用上拉 */
    GPIOB_CRL  = (GPIOB_CRL & ~0x0FU) | 0x08U;
}
/* 读 PB0 的实际电平 */
uint8_t OW_Read(void)
{
    return (uint8_t)((GPIOB_IDR >> 0) & 0x01U);
}
/* 等 PB0 变成 want 这个电平。返回 1 = 等到了，返回 0 = 超时
 * 超时保护是必须的：假如传感器没接、线断了，没有超时的话程序会永远
 * 卡在这个 while 里，整块板子看起来就像死机（LED 也不闪了）。
 *
 * 超时上限：一轮循环约 8 个内核时钟（64MHz 下约 0.125us），
 * 0x0008_0000 轮 ≈ 1.05 秒。正常时序里任何电平都不会超过 80us，
 * 所以这个上限对正常工作毫无影响；但一旦传感器掉线，主循环最多
 * 卡 1 秒就会自己出来并报告 err，而不是永久卡死。
 *
 * 【这里踩过的坑】原值写 30000：64MHz 下只有约 3.7ms，对 DHT11
 * 完全没有余量（"等应答"那几步传感器响应稍慢就会误报 err=1）。 */
uint8_t OW_WaitLevel(uint8_t want)
{
    uint32_t t = OW_TIMEOUT;

    while (((GPIOB_IDR >> 0) & 0x01U) != want)
    {
        if (--t == 0U) return 0U;
    }
    return 1U;
}
