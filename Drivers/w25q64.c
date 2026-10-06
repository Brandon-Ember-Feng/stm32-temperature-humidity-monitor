#include "w25q64.h"
#include "bsp_spi.h"
#include "bsp_time.h"
/* ==========================================================================
 * 11. W25Q64 SPI Flash 驱动（历史记录用，掉电不丢）
 *
 *    【为什么用 SPI 而不是 I2C？】
 *      SPI 没有"从机地址"这一套，靠一根独立的片选线（CS）点名，
 *      协议开销小、速度快（本项目跑 4.5MHz，是 I2C 的几十倍）。
 *      代价是线多：SCK / MOSI / MISO / CS 共 4 根。
 *      口诀：SPI 是"四线全双工"，I2C 是"两线半双工"。
 *
 *    【SPI 的四种模式（POL/PHA）】
 *      由 CR1 里的 CPOL（时钟极性）和 CPHA（时钟相位）组合出 4 种模式。
 *      W25Q64 要求「模式 0」：CPOL=0（空闲时 SCK 为低）、
 *                              CPHA=0（第一个时钟沿就采样数据）。
 *      模式不匹配的症状很有特点：读 ID 得到 0x000000 或 0xFFFFFF，
 *      因为数据在错误的边沿被采样，全都错位了。
 *
 *    【Flash 的三条铁律】
 *      ① 读：随便读，什么时候都能读，没有限制。
 *      ② 写：只能把位从 1 改成 0，不能从 0 改回 1。
 *         所以写之前必须先擦（擦完全是 0xFF，即全 1），才能往里写。
 *      ③ 擦：最小单位是「扇区」(4KB)，不能只擦一个字节。
 *         擦除后整个扇区变成 0xFF。
 *      这三条决定了"不能像写文件一样随便改一个字节"，
 *      必须用「双扇区乒乓 + 顺序追加」的方式来组织数据。
 *
 *    【关键时序（数据手册上的 t 参数）】
 *      擦除一个扇区：典型 45ms，最大 400ms —— 所以擦完必须"查忙"等它完
 *      页编程(256B)：典型 0.7ms，最大 3ms
 *      查忙：读状态寄存器 SR1 的 bit0（BUSY），为 0 表示空闲
 *
 *    【状态寄存器 SR1 的位（只看这几个）】
 *      bit0 BUSY ：1 = 正在擦/写，此时除了读状态寄存器，别的命令都不响应
 *      bit1 WEL  ：写使能锁存，每次擦/写之前都必须先发 0x06 把它置 1
 *                  （硬件出于安全考虑设计的：防止误擦写）
 *      bit2 BP0~BP2：块保护位，本项目不做保护，保持默认 0
 * ========================================================================== */

static uint8_t g_spi_ok;            /* 1 = W25Q64 识别成功 */

/* 等 Flash 把内部擦/写动作做完（查忙）。
 * 为什么必须查忙？擦一个扇区要 45~400ms，这期间芯片完全不响应别的命令。
 * 如果不查忙就直接读数据，读回来的是垃圾——这是新手最常踩的坑。
 * 超时保护 5 亿次循环（实际约 1 秒多），防止芯片坏掉时死等。 */
uint8_t W25_WaitBusy(void)
{
    uint32_t t = 0x20000000U;

    SPI1_CS_Low();
    SPI1_SwapByte(W25_CMD_READ_SR1);
    while (1)
    {
        uint8_t sr = SPI1_SwapByte(0xFFU);

        if ((sr & 0x01U) == 0U) break;      /* BUSY=0 -> 空闲了 */
        if (--t == 0U) { SPI1_CS_High(); return 0U; }   /* 超时 */
    }
    SPI1_CS_High();
    return 1U;
}

/* 发写使能（WEL）。每次擦除/写入之前都必须调用一次，
 * 因为硬件规定：擦/写命令执行完或执行失败后，WEL 会自动清零。
 * 漏掉这一步的症状：命令发出去了，但 Flash 毫无反应（数据没变）。 */
void W25_WriteEnable(void)
{
    SPI1_CS_Low();
    SPI1_SwapByte(W25_CMD_WRITE_EN);
    SPI1_CS_High();
}

/* 读 JEDEC ID，用来确认"芯片到底在不在、型号对不对"。
 * 返回 32 位：高 8 位无效，中间三字节是 厂商号 / 类型 / 容量。 */
uint32_t W25_ReadID(void)
{
    uint32_t id = 0U;

    SPI1_CS_Low();
    SPI1_SwapByte(W25_CMD_JEDEC_ID);
    id  = (uint32_t)SPI1_SwapByte(0xFFU) << 16;   /* 厂商号 */
    id |= (uint32_t)SPI1_SwapByte(0xFFU) << 8;    /* 类型   */
    id |= (uint32_t)SPI1_SwapByte(0xFFU);         /* 容量   */
    SPI1_CS_High();

    return id;
}

/* 读任意长度数据。只有读操作不需要先擦、也不需要写使能。
 * 发完 0x03 + 24 位地址后，Flash 会自动从该地址开始连续吐数据，
 * 地址到末尾会自动回卷到 0，所以可以一直读下去。 */
void W25_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    SPI1_CS_Low();
    SPI1_SwapByte(W25_CMD_READ_DATA);
    SPI1_SwapByte((uint8_t)((addr >> 16) & 0xFFU));   /* 地址是 24 位，高位在前 */
    SPI1_SwapByte((uint8_t)((addr >> 8)  & 0xFFU));
    SPI1_SwapByte((uint8_t)( addr        & 0xFFU));

    while (len-- > 0U)
    {
        *buf = SPI1_SwapByte(0xFFU);    /* 发 0xFF 只是为了让时钟跑起来，发什么无所谓 */
        buf++;
    }
    SPI1_CS_High();
}

/* 页编程：往指定地址写 1~256 字节。
 * 【硬约束】一次写入不能跨过 256 字节的页边界！
 *   跨页时多余的数据会被"卷回"到本页开头，覆盖掉本页已有的内容 ——
 *   这是静默的数据损坏，不会报错，只会让记录莫名其妙变错。
 *
 * 【本项目确实会踩到，不是纸上谈兵】
 *   记录起始地址 = 4 + 3k（k 是第几条）。要跨页，需要 (4+3k) mod 256 == 254 或 255，
 *   因为每条只有 3 字节，落在这两个余数上就刚好被页边界切开。
 *   由于 3 和 256 互质，这个余数序列会遍历所有值，所以**一定会周期性地撞上**：
 *   实测第 169、254、425、510、681、766、937、1022、1193、1278 条都在边界上
 *   （每隔 85 条出现一次，一个扇区里出现 10 次）。
 *   所以下面的自动拆分逻辑是必需的，不是多余的保险。
 *
 * 拆分方法：每次只写到"本页剩余空间"为止（room = 256 - 地址低8位），
 * 写完一段再发起一次新的 PageProgram，直到写完。 */
void W25_PageProgram(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    while (len > 0U)
    {
        /* 本页还能写几个字节：256 - (地址的低 8 位) */
        uint32_t room  = 256U - (addr & 0xFFU);
        uint32_t chunk = (len < room) ? len : room;

        W25_WriteEnable();
        SPI1_CS_Low();
        SPI1_SwapByte(W25_CMD_PAGE_PROGRAM);
        SPI1_SwapByte((uint8_t)((addr >> 16) & 0xFFU));
        SPI1_SwapByte((uint8_t)((addr >> 8)  & 0xFFU));
        SPI1_SwapByte((uint8_t)( addr        & 0xFFU));
        for (uint32_t i = 0U; i < chunk; i++)
        {
            SPI1_SwapByte(buf[i]);
        }
        SPI1_CS_High();
        W25_WaitBusy();                  /* 等这次写完成，才能发起下一次 */

        addr += chunk;
        buf  += chunk;
        len  -= chunk;
    }
}

/* 扇区擦除（4KB）。擦完之后这个扇区的每一个字节都变成 0xFF。
 * 这是写新数据的前置动作：只有 0xFF 才能被写成任意值。 */
void W25_SectorErase(uint32_t addr)
{
    W25_WriteEnable();                   /* ① 先发写使能 */
    SPI1_CS_Low();
    SPI1_SwapByte(W25_CMD_SECTOR_ERASE);
    /* 擦除命令只认地址的高位，低 12 位被忽略 —— 也就是说，
     * 给 0x0000_1234 和给 0x0000_1000 擦的是同一个扇区。
     * 这里主动把低位清掉，让调用者一眼看出擦的是哪个扇区。 */
    addr &= 0xFFFFF000U;
    SPI1_SwapByte((uint8_t)((addr >> 16) & 0xFFU));
    SPI1_SwapByte((uint8_t)((addr >> 8)  & 0xFFU));
    SPI1_SwapByte((uint8_t)( addr        & 0xFFU));
    SPI1_CS_High();
    W25_WaitBusy();                      /* ② 等擦完（最长 400ms） */
}

/* --------------------------------------------------------------------------
 * 上电探测：读 JEDEC ID，同时把「这片 Flash 到底在不在」记在驱动内部。
 *
 * 原来这个判断在 Core/main.c 里做，判完顺手写 g_spi_ok —— 上层去写驱动
 * 标志位，既违反分层，也让「谁是识别结果的唯一权威」变得模糊。
 * 现在权威只有这一个函数，上层问 W25_IsOk() 即可。
 * -------------------------------------------------------------------------- */
uint32_t W25_Probe(void)
{
    uint32_t id = W25_ReadID();

    g_spi_ok = (((id >> 16) & 0xFFU) == W25_ID_MANU) ? 1U : 0U;
    return id;
}

uint8_t W25_IsOk(void)
{
    return g_spi_ok;
}
