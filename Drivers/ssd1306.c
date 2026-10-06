#include "ssd1306.h"
#include "font8x16.h"
#include "bsp_soft_i2c.h"
#include "bsp_time.h"
/* ==========================================================================
 * 7. OLED 驱动（SSD1306 控制器，128x64 像素）
 *
 *    每个 I2C 事务的格式：[起始][从机地址][控制字节][数据...][停止]
 *      控制字节 0x00 -> 后面跟的是"命令"
 *      控制字节 0x40 -> 后面跟的是"显示数据"
 *    显存 GDDRAM 组织方式：分成 8 页(page0~7)，每页 8 行高、128 列宽，
 *      所以一个 8x16 的字符正好横跨相邻两页、占 8 列。
 * ========================================================================== */
#define OLED_ADDR_1     0x78U    /* 7 位地址 0x3C 左移一位，市面上最常见的 */
#define OLED_ADDR_2     0x7AU    /* 少数模块是 0x3D，即 0x7A */

/* 列地址偏移量。SSD1306 显存就是 128 列，偏移 0；
 * 而 SH1106 显存有 132 列，同样是 128 列的内容必须整体右移 2 列才对齐。
 * 如果屏幕能亮、但文字偏左且右边有竖条串到左边，把这里改成 2U */
#define OLED_COL_OFFSET 0U

static uint8_t g_oled_addr = OLED_ADDR_1;    /* 探测成功后保存实际地址 */

static void OLED_WrCmd(uint8_t cmd)
{
    I2C_Start();
    I2C_SendByte(g_oled_addr);
    I2C_WaitAck();
    I2C_SendByte(0x00);         /* 命令 */
    I2C_WaitAck();
    I2C_SendByte(cmd);
    I2C_WaitAck();
    I2C_Stop();
}

/* 探测某个地址上有没有器件应答 */
uint8_t OLED_Probe(uint8_t addr)
{
    uint8_t ack;

    I2C_Start();
    I2C_SendByte(addr);
    ack = I2C_WaitAck();
    I2C_Stop();

    return (ack == 0U) ? 1U : 0U;     /* 1 = 有应答 */
}

/* 设定写显存的起始位置：page=0~7 页，col=0~127 列 */
void OLED_SetPos(uint8_t page, uint8_t col)
{
    col = (uint8_t)(col + OLED_COL_OFFSET);

    OLED_WrCmd(0xB0U + page);            /* 设置页地址 */
    OLED_WrCmd(col & 0x0FU);             /* 列地址低 4 位  */
    OLED_WrCmd(0x10U | (col >> 4));      /* 列地址高 4 位  */
}

/* 连续填满一整页（128 列），用"一次起始 + 连续写"的方式加快速度 */
void OLED_FillPage(uint8_t page, uint8_t dat)
{
    OLED_SetPos(page, 0);

    I2C_Start();
    I2C_SendByte(g_oled_addr);
    I2C_WaitAck();
    I2C_SendByte(0x40);                  /* 后面是显示数据（页地址模式下列指针自增） */
    I2C_WaitAck();
    for (uint8_t i = 0; i < 128U; i++)
    {
        I2C_SendByte(dat);
        I2C_WaitAck();
    }
    I2C_Stop();
}

void OLED_FillAll(uint8_t dat)    /* 全屏填充：0xFF 全亮，0x00 全黑 */
{
    for (uint8_t page = 0; page < 8U; page++)
    {
        OLED_FillPage(page, dat);
    }
}

void OLED_Clear(void)
{
    OLED_FillAll(0x00U);
}

/* 初始化：返回 1 表示成功找到 OLED，返回 0 表示没有应答 */
uint8_t OLED_Init(void)
{
    delay_ms(100U);          /* 等 OLED 内部上电复位完成 */

    if (OLED_Probe(OLED_ADDR_1))
    {
        g_oled_addr = OLED_ADDR_1;
    }
    else if (OLED_Probe(OLED_ADDR_2))
    {
        g_oled_addr = OLED_ADDR_2;
    }
    else
    {
        return 0U;           /* 两个地址都没人应答：接线或供电有问题 */
    }

    OLED_WrCmd(0xAE);        /* 关闭显示               */
    OLED_WrCmd(0xD5); OLED_WrCmd(0x80);   /* 时钟分频/振荡频率 */
    OLED_WrCmd(0xA8); OLED_WrCmd(0x3F);   /* 多路复用率 = 64 行 */
    OLED_WrCmd(0xD3); OLED_WrCmd(0x00);   /* 显示垂直偏移 = 0  */
    OLED_WrCmd(0x40);        /* 显示起始行 = 0         */
    OLED_WrCmd(0x8D); OLED_WrCmd(0x14);   /* 电荷泵使能（模块用 3.3V 供电必须开） */
    /* 注：SSD1306 复位后默认就是"页寻址模式"，所以这里不必再发 0x20 0x02；
     *     SH1106 控制器不认识这条命令，去掉它两种屏都能用 */
    OLED_WrCmd(0xA1);        /* 左右方向：列 127 映射到 SEG0 */
    OLED_WrCmd(0xC8);        /* 上下方向：从 COM63 扫描到 COM0 */
    OLED_WrCmd(0xDA); OLED_WrCmd(0x12);   /* COM 引脚硬件配置       */
    OLED_WrCmd(0x81); OLED_WrCmd(0xCF);   /* 对比度（亮度）         */
    OLED_WrCmd(0xD9); OLED_WrCmd(0xF1);   /* 预充电周期             */
    OLED_WrCmd(0xDB); OLED_WrCmd(0x30);   /* VCOMH 电压             */
    OLED_WrCmd(0xA4);        /* 显示内容跟随显存       */
    OLED_WrCmd(0xA6);        /* 正常显示（0xA7 为反白）*/
    OLED_WrCmd(0xAF);        /* 开启显示               */

    OLED_Clear();
    return 1U;
}

/* ==========================================================================
 * 9. 字符显示
 *
 *    在"直接画字符"之上，又加了几个"拼字符串"的小工具：
 *    先用 sprintf 的思路把数字转成文本、再整行显示。
 *    好处是每行长度固定（末尾补空格），新数据比旧数据短时不会留下残影。
 * ========================================================================== */

/* 自定义字符：度数符号 °
 * 字库里只有 ASCII 32~127，没有"度"这个符号，所以单独画一个点阵。
 * 点阵规则和字库一致：数组 16 字节 = 16 列，每字节是一列 8 个像素，bit0 在最上面。
 * 下面这个小圆环画在单元格的第 1~5 行、第 1~5 列，和数字的顶部对齐。 */
#define CH_DEGREE       0x7FU    /* 借用 ASCII 127(DEL) 当"度数符号"的代号 */

static const uint8_t GLYPH_DEGREE[16] = {
    0x00, 0x1C, 0x22, 0x22, 0x22, 0x1C, 0x00, 0x00,   /* 上半页：小圆环 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00    /* 下半页：空     */
};

/* 显示一个字符：page 0~6（要占两页，所以最大只能从第 6 页开始），col 0~120 */
void OLED_ShowChar(uint8_t page, uint8_t col, char ch)
{
    const uint8_t *p;

    if (page > 6U || col > 120U) return;

    if ((uint8_t)ch == CH_DEGREE)
    {
        p = GLYPH_DEGREE;                   /* 度数符号走自定义点阵 */
    }
    else
    {
        if (ch < 32 || ch > 127) ch = ' ';  /* 字库外的字符统一显示空格 */
        p = &F8X16[(uint8_t)ch - 32U][0];
    }

    /* 上半页 */
    OLED_SetPos(page, col);
    I2C_Start();
    I2C_SendByte(g_oled_addr);  I2C_WaitAck();
    I2C_SendByte(0x40);         I2C_WaitAck();
    for (uint8_t i = 0; i < 8U; i++) { I2C_SendByte(p[i]); I2C_WaitAck(); }
    I2C_Stop();

    /* 下半页 */
    OLED_SetPos(page + 1U, col);
    I2C_Start();
    I2C_SendByte(g_oled_addr);  I2C_WaitAck();
    I2C_SendByte(0x40);         I2C_WaitAck();
    for (uint8_t i = 8U; i < 16U; i++) { I2C_SendByte(p[i]); I2C_WaitAck(); }
    I2C_Stop();
}

/* 显示字符串：一行 128 像素，8x16 字符每字占 8 列，所以一行最多 16 个字符 */
void OLED_ShowStr(uint8_t page, uint8_t col, const char *str)
{
    while (*str != '\0')
    {
        OLED_ShowChar(page, col, *str);
        str++;
        col += 8U;
        if (col > 120U) break;
    }
}

/* 探测到的实际 I2C 地址（0x78 或 0x7A）。只读，写入点仅在 OLED_Init() 内部 */
uint8_t OLED_Addr(void)
{
    return g_oled_addr;
}
