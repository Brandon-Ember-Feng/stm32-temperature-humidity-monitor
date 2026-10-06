#include "bsp_soft_i2c.h"
#include "bsp_gpio.h"
#include "bsp_reg.h"
#include "bsp_time.h"
/* I2C 两条线：空闲时都要为高，靠外部上拉电阻拉高，所以必须配成"开漏输出" */
#define SCL_HIGH()      (GPIOB_ODR |=  (1U << PIN_SCL))
#define SCL_LOW()       (GPIOB_ODR &= ~(1U << PIN_SCL))
#define SDA_HIGH()      (GPIOB_ODR |=  (1U << PIN_SDA))
#define SDA_LOW()       (GPIOB_ODR &= ~(1U << PIN_SDA))
#define SDA_READ()      ((GPIOB_IDR >> PIN_SDA) & 0x01U)   /* 读 SDA 实际电平 */
/* ==========================================================================
 * 6. 软件模拟 I2C（I2C 时序靠 GPIO 手动翻转实现）
 *
 *    协议要点：
 *      · SCL 全程由主机（STM32）驱动；SDA 双向，谁发数据谁驱动
 *      · 起始信号：SCL 为高时，SDA 由高变低
 *      · 停止信号：SCL 为高时，SDA 由低变高
 *      · 数据位：SCL 为低时改变 SDA，SCL 为高时数据有效
 *      · 第 9 个时钟是应答位 ACK：从机把 SDA 拉低表示"收到"
 * ========================================================================== */
#define I2C_DELAY()     delay_us(2)

void I2C_Start(void)
{
    SDA_HIGH();
    SCL_HIGH();
    I2C_DELAY();
    SDA_LOW();          /* SCL 为高时 SDA 下降沿 —— 起始信号 */
    I2C_DELAY();
    SCL_LOW();
    I2C_DELAY();
}

void I2C_Stop(void)
{
    SDA_LOW();
    SCL_HIGH();
    I2C_DELAY();
    SDA_HIGH();         /* SCL 为高时 SDA 上升沿 —— 停止信号 */
    I2C_DELAY();
}

/* 发送一个字节，高位在前；发完不处理应答，由调用者用 I2C_WaitAck 读 */
void I2C_SendByte(uint8_t dat)
{
    for (uint8_t i = 0; i < 8U; i++)
    {
        SCL_LOW();
        I2C_DELAY();

        if (dat & 0x80U) SDA_HIGH();     /* 先放好数据位，再抬高 SCL 让从机采样 */
        else             SDA_LOW();
        dat <<= 1;

        I2C_DELAY();
        SCL_HIGH();
        I2C_DELAY();
    }
    SCL_LOW();
    I2C_DELAY();
}

/* 读应答：返回 0 = ACK(从机应答)，返回 1 = NACK(无人应答) */
uint8_t I2C_WaitAck(void)
{
    uint8_t ack;

    SDA_HIGH();         /* 主机释放 SDA 线，交给从机控制 */
    I2C_DELAY();
    SCL_HIGH();         /* 第 9 个时钟 */
    I2C_DELAY();

    ack = (uint8_t)SDA_READ();      /* 从机拉低则为 0(ACK) */

    SCL_LOW();
    I2C_DELAY();
    return ack;
}

/* ==========================================================================
 * 6.5 总线诊断：读空闲电平 + 扫描所有地址
 *     OLED 不亮时，这两个函数是定位问题的关键：
 *       · 空闲电平读不到高 -> 上拉/接线/供电问题（跟程序无关）
 *       · 扫描到 0 个器件   -> 总线上什么都没接对
 *       · 扫到别的地址      -> 总线是通的，只是地址不是 0x78/0x7A
 * ========================================================================== */

/* 把 PB6/PB7 临时切成浮空输入，读总线空闲电平 */
void I2C_BusIdleCheck(uint8_t *scl_lv, uint8_t *sda_lv)
{
    uint32_t save = GPIOB_CRL;

    GPIOB_CRL = (GPIOB_CRL & ~(0xFFU << 24)) | (0x44U << 24);   /* 0x4 = 浮空输入 */
    delay_us(200U);                                             /* 等电平稳定   */

    *scl_lv = (uint8_t)((GPIOB_IDR >> PIN_SCL) & 0x01U);
    *sda_lv = (uint8_t)((GPIOB_IDR >> PIN_SDA) & 0x01U);

    GPIOB_CRL = save;                                           /* 恢复开漏输出 */
}

/* SDA 回读自检（判断"线到底有没有接上"最有效的一招）
 *   开漏输出的特点是：写 1 = 释放总线，电平由外部上拉电阻决定。
 *   所以"写 1 后回读到 1"说明线上有上拉、接线正常；
 *   如果写 1 后回读还是 0，说明这根线悬空、断线，或者模块根本没有上拉电阻。
 *   返回 1 = 通过，返回 0 = 异常。
 *
 *   为什么需要这一招？因为 SDA 悬空时电平不确定，I2C 读应答会读到随机的 0，
 *   于是程序误以为"从机应答了"，实际什么都没收到 —— 正是"LED 慢闪但屏幕全黑"的成因。 */
uint8_t I2C_SdaEchoTest(void)
{
    uint8_t ok = 1U;

    SDA_HIGH();                     /* 释放 */
    delay_us(200U);
    if (SDA_READ() != 1U) ok = 0U;  /* 释放后应为高电平 */

    SDA_LOW();                      /* 拉低 */
    delay_us(200U);
    if (SDA_READ() != 0U) ok = 0U;  /* 拉低后应为低电平，说明引脚可控 */

    SDA_HIGH();                     /* 恢复空闲态 */
    return ok;
}

/* ==========================================================================
 * 6.6 【接反判别】用任意两个引脚发一次 I2C 地址探测
 *
 *    为什么需要它？
 *      屏幕有供电、SCL/SDA 也都测到上拉（说明线确实插在模块上），
 *      但总线扫描一个器件都找不到 —— 这时最大的嫌疑就是
 *      "模块的 SCL 和 SDA 两根线接反了"。
 *      接反之所以难以察觉：两根线各自的电气特性完全正常
 *        （各自都有上拉、都能拉低、互不干扰），
 *        唯独时钟和数据角色互换，从机永远等不到合法的起始条件 -> 必然零应答。
 *
 *    判据：
 *      映射A(SCL=6,SDA=7) 无应答、映射B(SCL=7,SDA=6) 有应答
 *          -> 两根线接反了，对调即可
 *      两种映射都无应答
 *          -> 不是接反，去查模块 GND 是否虚接、模块本身是否损坏
 *
 *    返回 0 = 收到 ACK（找到了器件），返回 1 = 无应答
 * ========================================================================== */
uint8_t I2C_ProbePin(uint8_t scl_bit, uint8_t sda_bit, uint8_t addr8)
{
    uint8_t i;
    uint8_t ack;

#define _SCL_H()   (GPIOB_BSRR = (1U << scl_bit))
#define _SCL_L()   (GPIOB_BSRR = (1U << (scl_bit + 16U)))
#define _SDA_H()   (GPIOB_BSRR = (1U << sda_bit))
#define _SDA_L()   (GPIOB_BSRR = (1U << (sda_bit + 16U)))
#define _SDA_R()   ((uint8_t)((GPIOB_IDR >> sda_bit) & 0x01U))

    /* 两脚在 GPIO_Init 里已配成通用开漏输出，此处只交换"角色" */

    _SDA_H();  _SCL_H();  delay_us(2);
    _SDA_L();  delay_us(2);          /* SCL 为高时 SDA 下降沿 = 起始信号 */
    _SCL_L();  delay_us(2);

    for (i = 0U; i < 8U; i++)        /* 高位在前，逐位送出 */
    {
        _SCL_L();  delay_us(2);
        if ((addr8 & 0x80U) != 0U) { _SDA_H(); } else { _SDA_L(); }
        addr8 = (uint8_t)(addr8 << 1);
        delay_us(2);
        _SCL_H();  delay_us(2);
    }

    _SCL_L();  delay_us(2);
    _SDA_H();  delay_us(2);          /* 主机释放 SDA，交给从机应答 */
    _SCL_H();  delay_us(2);
    ack = _SDA_R();                  /* 0 = 从机把它拉低 = ACK */
    _SCL_L();  delay_us(2);

    _SDA_L();  _SCL_H();  delay_us(2);  _SDA_H();  delay_us(2);   /* 停止信号，放开总线 */

#undef _SCL_H
#undef _SCL_L
#undef _SDA_H
#undef _SDA_L
#undef _SDA_R

    return ack;
}

/* 扫描 0x03~0x77 全部 7 位地址，有应答的按 8 位形式记入 found，返回器件个数 */
uint8_t I2C_Scan(uint8_t *found, uint8_t max)
{
    uint8_t n = 0U;

    for (uint8_t addr = 0x03U; addr <= 0x77U; addr++)
    {
        I2C_Start();
        I2C_SendByte((uint8_t)(addr << 1));
        if (I2C_WaitAck() == 0U)                    /* 0 = 收到应答 */
        {
            if (n < max) found[n] = (uint8_t)(addr << 1);
            n++;
        }
        I2C_Stop();
    }
    return n;
}
