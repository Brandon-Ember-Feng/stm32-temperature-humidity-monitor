#include "bsp_gpio.h"
#include "bsp_reg.h"
/* ==========================================================================
 * 5. GPIO 初始化
 *
 *    STM32F1 每个引脚占 CRL/CRH 里的 4 个位，格式为（这是本工程踩过的最大的坑）：
 *        【位3位2 = CNF(输出形式)，位1位0 = MODE(模式/速度)】  —— 顺序绝不可记反
 *        即 nibble = CNF<<2 | MODE
 *        MODE：00 输入；01 输出10MHz；10 输出2MHz；11 输出50MHz
 *        CNF（输出模式）：00 通用推挽；01 通用开漏；10 复用推挽；11 复用开漏
 *        CNF（输入模式）：00 模拟；01 浮空；10 上拉/下拉；11 保留
 *        常用值速查：浮空输入=0x4  推挽输出2MHz=0x2  通用开漏50MHz=0x7
 *                    复用推挽50MHz=0xB  复用开漏50MHz=0xF
 *        （复位默认 0x44444444 = 全部浮空输入，可用来对照校验）
 * ========================================================================== */

/* ---------------------------------------------------------------------------
 * 【引脚角色开关】I2C_SWAP_DIAG
 *   0 = 正常映射：PB6 = SCL、PB7 = SDA（本项目默认）
 *   1 = 交换映射：PB7 = SCL、PB6 = SDA
 *
 *   用途：屏幕有供电、两线也都有上拉，却完全不应答时，用来验证
 *        "模块的 SCL/SDA 是不是被接反了"。
 *   ※ PB6/PB7 两个引脚在 CRL 里的配置是一样的（都是通用开漏 0x7），
 *     所以交换角色不需要动 GPIO 配置，只换"谁当时钟、谁当数据"。
 *   正常运行时必须是 0。
 * ------------------------------------------------------------------------- */
#define I2C_SWAP_DIAG   0U

#if (I2C_SWAP_DIAG != 0U)
  #define PIN_SCL         7U       /* 交换：PB7 当时钟 */
  #define PIN_SDA         6U       /* 交换：PB6 当数据 */
#else
  #define PIN_SCL         6U       /* 正常：PB6 = SCL */
  #define PIN_SDA         7U       /* 正常：PB7 = SDA */
#endif




void GPIO_Init(void)
{
    RCC_APB2ENR |= (1U << 3) | (1U << 4);      /* 使能 GPIOB、GPIOC 时钟 */

    /* PB6(SCL)、PB7(SDA)：通用开漏输出 50MHz
     *   CNF=01(通用开漏), MODE=11(50MHz) -> 01<<2|11 = 0111b = 0x7，两个引脚 => 0x77
     *   【踩过的坑】原先写的 0xD = 1101b 其实是"复用开漏"：引脚输出交给片上 I2C1 外设控制，
     *   而 I2C1 的时钟从未使能（RCC_APB1ENR=0），软件写 ODR 完全不起作用 ——
     *   实测现象：ODR 写 1（释放）而 IDR 始终读到 0，OLED 必不可能亮。 */
    GPIOB_CRL &= ~(0xFFU << 24);
    GPIOB_CRL |=  (0x77U << 24);

    /* PC13(LED)：推挽输出 2MHz
     *   CNF=00(通用推挽), MODE=10(2MHz) -> 00<<2|10 = 0010b = 0x2
     * 注意：数据手册规定 PC13/PC14/PC15 最高只能 2MHz，不能配 50MHz
     *   【踩过的坑】原先写的 0x8 = 1000b 其实是"上拉/下拉输入"：
     *   引脚不主动驱动，LED 只能靠内部约 40k 的上/下拉通过微安级电流，
     *   表现为"能看出在闪、但远比正常暗"。 */
    GPIOC_CRH &= ~(0x0FU << 20);
    GPIOC_CRH |=  (0x02U << 20);

    /* PB0(DHT11 的 DATA 线)：上拉输入
     *   CNF=10(上拉/下拉输入), MODE=00(输入) -> 10<<2|00 = 1000b = 0x8
     *   输入模式下，是靠 ODR 这一位来决定"上拉"还是"下拉"：
     *       对应 ODR 位 = 1 -> 启用上拉（大约是 40kΩ 接到 3.3V）
     *       对应 ODR 位 = 0 -> 启用下拉
     *   平时让总线保持高电平（空闲态），读取时也靠它把线拉回去。
     *   注意：STM32F1 内部上拉只在"输入模式"下才有，输出模式没有。 */
    GPIOB_ODR |=  (1U << 0);
    GPIOB_CRL &= ~(0x0FU << 0);
    GPIOB_CRL |=  (0x08U << 0);

    /* PB1(按键 K1)：上拉输入
     *   CNF=10(上拉/下拉输入), MODE=00(输入) -> 10<<2|00 = 1000b = 0x8
     *   和 PB0 同理，输入模式下 ODR 的这一位决定上拉还是下拉：
     *       ODR 位 = 1 -> 上拉（约 40k 接 3.3V）  <- 按键要的就是这个
     *       ODR 位 = 0 -> 下拉
     *   所以按键可以直接一端接引脚、一端接 GND，不用焊外部电阻；
     *   没按的时候读 1，按下被拉到 GND 读 0。 */
    GPIOB_ODR |=  (1U << 1);
    GPIOB_CRL &= ~(0x0FU << 4);
    GPIOB_CRL |=  (0x08U << 4);

    BSP_Pin_Write(BSP_PORT_B, PIN_SCL, 1U);     /* I2C 总线空闲态：两线置高 */
    BSP_Pin_Write(BSP_PORT_B, PIN_SDA, 1U);
    LED_Off();
}

/* ---------------- 引脚模式配置 ----------------
 * F1 每个引脚占 CRL/CRH 里的 4 个位：pin 0~7 在 CRL，pin 8~15 在 CRH。
 * 这里只做「读-改-写」的位域更新，不动同一寄存器的其他引脚。 */
void BSP_Pin_SetMode(uint8_t port, uint8_t pin, uint8_t cfg)
{
    volatile uint32_t *reg;
    uint32_t shift;
    uint32_t mask;

    if (pin < 8U)
    {
        reg = (port == BSP_PORT_A) ? &GPIOA_CRL
            : (port == BSP_PORT_B) ? &GPIOB_CRL : &GPIOC_CRL;
    }
    else
    {
        reg = (port == BSP_PORT_A) ? &GPIOA_CRH
            : (port == BSP_PORT_B) ? &GPIOB_CRH : &GPIOC_CRH;
    }

    shift = (uint32_t)(pin & 0x07U) * 4U;
    mask  = 0x0FU << shift;
    *reg  = (*reg & ~mask) | ((uint32_t)cfg << shift);
}

/* ---------------- 通用引脚读写 ---------------- */
void BSP_Pin_Write(uint8_t port, uint8_t pin, uint8_t level)
{
    uint32_t bit = (level != 0U) ? (1U << pin) : (1U << (pin + 16U));

    if      (port == BSP_PORT_A) GPIOA_BSRR = bit;
    else if (port == BSP_PORT_B) GPIOB_BSRR = bit;
    else if (port == BSP_PORT_C) GPIOC_BSRR = bit;
}

uint8_t BSP_Pin_Read(uint8_t port, uint8_t pin)
{
    uint32_t idr = (port == BSP_PORT_A) ? GPIOA_IDR
                 : (port == BSP_PORT_B) ? GPIOB_IDR : GPIOC_IDR;

    return (uint8_t)((idr >> pin) & 0x01U);
}

/* ---------------- 板载 LED（PC13，低电平点亮） ---------------- */
void LED_On(void)     { GPIOC_ODR &= ~(1U << 13); }
void LED_Off(void)    { GPIOC_ODR |=  (1U << 13); }
void LED_Toggle(void) { GPIOC_ODR ^=  (1U << 13); }

/* ---------------- 按键 K1（PB1，上拉输入） ---------------- */
uint8_t BSP_Key_Read(void) { return (uint8_t)((GPIOB_IDR >> 1) & 0x01U); }
