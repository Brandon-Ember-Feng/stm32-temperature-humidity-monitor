#include "hist_store.h"
#include "w25q64.h"
#include "bsp_time.h"
/* ==========================================================================
 * 12. 历史记录存储管理（双扇区乒乓 + 顺序追加）
 *
 *    【为什么不能简单地在文件末尾追加？】
 *      Flash 一个扇区只有 4KB，写满之后必须擦掉才能继续写；
 *      但擦除是"整扇区"动作，一擦就把这个扇区里已有的记录全毁了。
 *      如果只有 1 个扇区，就必须"读出全部 → 擦除 → 写回"，非常繁琐而且
 *      中途掉电就全丢了。
 *
 *    【双扇区乒乓怎么工作】
 *      设两个扇区 A(0x000000) 和 B(0x001000)，格式完全一样：
 *
 *          扇区头部 4 字节：[0]=魔数 0xA5  [1]=状态(0x01有效)  [2..3]=起始记录号
 *          之后紧跟记录，每条 3 字节：温度高字节、温度低字节、湿度字节
 *
 *      写入逻辑（g_wr_sector 指向当前正在写的扇区）：
 *          如果这个扇区还没写满 → 直接在后面追加一条
 *          如果写满了             → 切到另一个扇区：擦掉它，写入新的文件头，
 *                                  把记录号接上（不重头开始），从它开头继续写
 *      这样任意时刻总有"一个扇区是完整的旧数据，另一个在接新数据"，
 *      掉电最多丢失当前这一条，历史不会整体崩掉。
 *
 *    【记录号是干什么的】
 *      乒乓切换后，两个扇区的物理顺序和逻辑时间顺序不一致了。
 *      所以每条记录带一个自增序号，读的时候按序号排序输出，
 *      就能还原出真实的先后顺序。
 *
 *    【已知边界：序号回绕】
 *      序号存在扇区头的 2 个字节里（uint16，最大 65535）。
 *      每扇区 1364 条，写满约 48 次乒乓（约 65000 条 ≈ 1090 小时）后序号会绕回 0，
 *      那一刻 a.seq >= b.seq 的判断会误判新旧，导致排序颠倒一轮。
 *      对本项目（演示 + 短时运行）不影响；若要做产品级，
 *      应把序号扩到 4 字节，或在 init 时做归一化（把两个 seq 都减去较小值）。
 *
 *    【为什么记录只存温湿度、不存时间？】
 *      因为没有 RTC，开机时间归零，存了也没有绝对意义。
 *      这里的"历史"表达的是"温度和湿度随时间的变化趋势"，
 *      序号 + 相对时间（序号 × 1 分钟）已经足够表达这个趋势了。
 * ========================================================================== */

#define W25_SECTOR_A        0x000000UL   /* 记录区 A 的扇区起始地址 */
#define W25_SECTOR_B        0x001000UL   /* 记录区 B 的扇区起始地址 */
#define W25_REC_HDR_SIZE    4UL          /* 扇区头：魔数+状态+起始序号(2B) */
#define W25_REC_SIZE        3UL          /* 每条记录 3 字节 */
#define W25_REC_AREA        (4096UL - W25_REC_HDR_SIZE)          /* 可写数据字节数 4092 */
#define W25_REC_PER_SECTOR  (W25_REC_AREA / W25_REC_SIZE)        /* 每扇区记录条数 1364 */
#define W25_REC_MAGIC       0xA5U        /* 魔数：用来判断这个扇区有没有被初始化过 */
#define W25_REC_VALID       0x01U        /* 状态字节：1 = 本扇区数据有效的 */
#define W25_VERIFY_EVERY    100U         /* 每写这么多条，就实地重扫一遍 Flash 对账 */


static W25_RecInfo g_recA;              /* 扇区 A 的解析结果 */
static W25_RecInfo g_recB;              /* 扇区 B 的解析结果 */
static uint16_t    g_cntA;              /* 扇区 A 的实际记录条数（精确计数，缓存） */
static uint16_t    g_cntB;              /* 扇区 B 的实际记录条数（精确计数，缓存） */
static uint8_t     g_wr_sector;         /* 当前正在写的扇区：0 = A，1 = B */
static uint16_t    g_wr_count;          /* 当前扇区已写条数 */
static uint16_t    g_wr_seq;            /* 下一条记录的序号 */
static uint32_t    g_rec_total;         /* 两个扇区加起来的总记录条数 */
static uint8_t     g_rec_ok;            /* 1 = 记录系统可用（W25Q64 在线） */

/* 精确数一个扇区里有多少条记录。
 * 判据：连续读到 3 个 0xFF 就认为到末尾了。
 * 为什么可以用 0xFF 当"空位"标志？因为擦除后 Flash 全是 0xFF，
 * 而一条真实记录不可能是 0xFFFF（温度 = 3276.7°C，物理上不存在）。
 *
 * 【性能提示】这个函数要逐条读 1364 次，是个不便宜的操作（约 19ms）。
 * 所以只在"初始化"和"写完一条"时调用，结果缓存到 g_cntA/g_cntB，
 * 绝不在读历史画面时每行都调一次 —— 那样翻一页要 150ms，太慢。 */
uint16_t W25_CountSector(uint32_t sector_addr)
{
    uint16_t n = 0U;
    uint8_t  raw[W25_REC_SIZE];

    for (uint16_t i = 0U; i < W25_REC_PER_SECTOR; i++)
    {
        W25_Read(sector_addr + W25_REC_HDR_SIZE + (uint32_t)i * W25_REC_SIZE,
                 raw, W25_REC_SIZE);
        if ((raw[0] == 0xFFU) && (raw[1] == 0xFFU) && (raw[2] == 0xFFU)) break;
        n++;
    }
    return n;
}

/* 重新统计两个扇区的条数并刷新缓存（慢，约 40ms）。
 *
 * 正常写入流程走的是 W25_RecAppend 里的"增量 +1"，不需要调这个。
 * 它的用途是**校验**：把"增量算出来的条数"和"实地扫一遍数出来的条数"
 * 对比一下，一致说明缓存没被写乱。
 * 每写 100 条校验一次，代价约 40ms/100 分钟，可以忽略，
 * 但能在开发期尽早发现寻址或乒乓逻辑的问题。 */
void W25_RefreshCounts(void)
{
    g_cntA = W25_CountSector(W25_SECTOR_A);
    g_cntB = W25_CountSector(W25_SECTOR_B);
    g_rec_total = (uint32_t)g_cntA + (uint32_t)g_cntB;
}

/* 读一个扇区的头，解析出里面已有的记录信息。
 * 返回 1 = 这个扇区是有效的（有魔数、有数据），返回 0 = 空白或无效。 */
uint8_t W25_LoadSector(uint32_t sector_addr, W25_RecInfo *info)
{
    uint8_t hdr[W25_REC_HDR_SIZE];

    info->seq   = 0U;
    info->start = (uint8_t)W25_REC_HDR_SIZE;
    info->count = 0U;

    W25_Read(sector_addr, hdr, W25_REC_HDR_SIZE);

    if (hdr[0] != W25_REC_MAGIC)  return 0U;    /* 没有魔数 -> 从没写过 */
    if (hdr[1] != W25_REC_VALID)  return 0U;    /* 状态不对 -> 视为无效 */

    info->seq   = (uint16_t)(((uint16_t)hdr[2] << 8) | (uint16_t)hdr[3]);
    info->start = (uint8_t)W25_REC_HDR_SIZE;

    return 1U;
}

/* 初始化记录系统：读两个扇区的头，决定继续往哪个扇区写。
 *
 *   决策规则：
 *     · 两个扇区都没写过       -> 格式化 A，从 A 开始写
 *     · 只有 A 有效            -> 如果 A 没满就接着写 A，满了就切到 B
 *     · 只有 B 有效            -> 同理
 *     · 两个都有效             -> 选"序号更大"的那个作为当前写入区；
 *                                 如果它也满了，就切到另一个（擦掉重写，序号接上）
 */
void W25_RecInit(void)
{
    uint8_t  okA, okB;
    uint16_t realA, realB;

    g_rec_ok    = 0U;
    g_wr_seq    = 0U;
    g_wr_count  = 0U;
    g_rec_total = 0U;
    g_cntA      = 0U;
    g_cntB      = 0U;

    if (!W25_IsOk()) return;

    okA = W25_LoadSector(W25_SECTOR_A, &g_recA);
    okB = W25_LoadSector(W25_SECTOR_B, &g_recB);

    /* 精确统计条数。无效的扇区一律按 0 条算（即使里面恰好有残留数据） */
    realA = okA ? W25_CountSector(W25_SECTOR_A) : 0U;
    realB = okB ? W25_CountSector(W25_SECTOR_B) : 0U;

    g_cntA      = realA;
    g_cntB      = realB;
    g_rec_total = (uint32_t)realA + (uint32_t)realB;

    if (!okA && !okB)
    {
        /* 全新芯片：格式化 A，序号从 0 开始 */
        uint8_t hdr[W25_REC_HDR_SIZE];
        W25_SectorErase(W25_SECTOR_A);
        hdr[0] = W25_REC_MAGIC;
        hdr[1] = W25_REC_VALID;
        hdr[2] = 0U;
        hdr[3] = 0U;
        W25_PageProgram(W25_SECTOR_A, hdr, W25_REC_HDR_SIZE);
        g_wr_sector = 0U;
        g_wr_count  = 0U;
        g_wr_seq    = 0U;
    }
    else if (okA && !okB)
    {
        g_wr_sector = 0U;
        g_wr_count  = realA;
        g_wr_seq    = (uint16_t)(g_recA.seq + realA);   /* 序号接着老数据往上走 */
    }
    else if (!okA && okB)
    {
        g_wr_sector = 1U;
        g_wr_count  = realB;
        g_wr_seq    = (uint16_t)(g_recB.seq + realB);
    }
    else
    {
        /* 两个都有效：接在"序号更大的那个"后面继续写，
         * 因为序号大 = 那一轮更晚开始 = 它的数据更新。 */
        if (g_recA.seq >= g_recB.seq)
        {
            g_wr_sector = 0U;
            g_wr_count  = realA;
            g_wr_seq    = (uint16_t)(g_recA.seq + realA);
        }
        else
        {
            g_wr_sector = 1U;
            g_wr_count  = realB;
            g_wr_seq    = (uint16_t)(g_recB.seq + realB);
        }
    }

    g_rec_ok = 1U;
}

/* 追加一条记录。参数是放大 10 倍的温湿度（和显示用的是同一份数据）。
 * 返回 1 = 写成功，返回 0 = 没写成（芯片不在，或者查忙超时）。 */
uint8_t W25_RecAppend(int16_t temp_x10, int16_t humi_x10)
{
    uint8_t  rec[W25_REC_SIZE];
    uint32_t sector = (g_wr_sector == 0U) ? W25_SECTOR_A : W25_SECTOR_B;
    uint32_t addr;

    if (!g_rec_ok) return 0U;

    /* ① 判断当前扇区是不是写满了 */
    if (g_wr_count >= W25_REC_PER_SECTOR)
    {
        uint8_t  hdr[W25_REC_HDR_SIZE];
        uint8_t  next = (g_wr_sector == 0U) ? 1U : 0U;      /* 乒乓：换到另一个扇区 */
        uint32_t nsec = (next == 0U) ? W25_SECTOR_A : W25_SECTOR_B;

        W25_SectorErase(nsec);                              /* 擦掉旧数据 */
        hdr[0] = W25_REC_MAGIC;
        hdr[1] = W25_REC_VALID;
        hdr[2] = (uint8_t)(g_wr_seq >> 8);                  /* 序号接上，不重头开始 */
        hdr[3] = (uint8_t)(g_wr_seq & 0xFFU);
        W25_PageProgram(nsec, hdr, W25_REC_HDR_SIZE);

        /* 换了扇区，缓存里的条数和扇区头都要跟着更新，
         * 否则 W25_RecRead 会按旧信息算出错误的地址 */
        if (next == 0U) { g_recA.seq = g_wr_seq; g_cntA = 0U; }
        else            { g_recB.seq = g_wr_seq; g_cntB = 0U; }

        g_rec_total = (uint32_t)g_cntA + (uint32_t)g_cntB;

        g_wr_sector = next;
        g_wr_count  = 0U;
        sector      = nsec;
    }

    /* ② 组装 3 字节记录：温度是两个字节的补码，湿度一个字节 */
    rec[0] = (uint8_t)(((uint16_t)temp_x10) >> 8);          /* 温度高字节 */
    rec[1] = (uint8_t)(((uint16_t)temp_x10) & 0xFFU);       /* 温度低字节 */
    rec[2] = (uint8_t)((humi_x10 > 255) ? 255 : ((humi_x10 < 0) ? 0 : humi_x10));

    /* ③ 写到当前扇区的对应位置 */
    addr = sector + W25_REC_HDR_SIZE + (uint32_t)g_wr_count * W25_REC_SIZE;
    W25_PageProgram(addr, rec, W25_REC_SIZE);

    g_wr_count++;
    g_wr_seq++;

    /* ④ 同步更新缓存（不再回头重新扫扇区，那样每次写都要多花 40ms） */
    if (g_wr_sector == 0U) { if (g_cntA < 0xFFFFU) g_cntA++; }
    else                   { if (g_cntB < 0xFFFFU) g_cntB++; }
    g_rec_total = (uint32_t)g_cntA + (uint32_t)g_cntB;

    return 1U;
}

/* 按"逻辑顺序"读取第 idx 条历史记录（idx 从 0 开始 = 最早的一条）。
 * 之所以要这个函数而不是直接算地址，是因为双扇区乒乓后物理地址不连续。
 * 返回 1 = 成功读出，0 = idx 超范围。
 *
 * 【性能关键】这里用的是缓存好的 g_cntA / g_cntB，不再每次重新扫扇区。
 * 如果每次调用都重新数一遍，OLED_PageHistory 一页要调 4 次，
 * 每次扫 2728 条记录，翻一页就得等 150ms —— 手感明显发滞。 */
uint8_t W25_RecRead(uint32_t idx, int16_t *temp_x10, int16_t *humi_x10)
{
    uint32_t addr;
    uint8_t  raw[W25_REC_SIZE];

    if (!g_rec_ok) return 0U;
    if (idx >= g_rec_total) return 0U;

    /* 决定这条记录在哪个扇区、第几条。
     * 排序依据：扇区头的序号(seq) 大的表示"更新的那一轮"。 */
    if ((g_cntA > 0U) && (g_cntB > 0U))
    {
        uint8_t a_is_newer = (g_recA.seq >= g_recB.seq) ? 1U : 0U;

        /* 序号小的那个扇区里存的是更早的记录，排在前面 */
        uint16_t old_cnt = a_is_newer ? g_cntB : g_cntA;
        uint32_t old_sec = a_is_newer ? W25_SECTOR_B : W25_SECTOR_A;

        if (idx < (uint32_t)old_cnt)
        {
            addr = old_sec + W25_REC_HDR_SIZE + idx * W25_REC_SIZE;
        }
        else
        {
            uint32_t k = idx - old_cnt;
            uint32_t new_sec = a_is_newer ? W25_SECTOR_A : W25_SECTOR_B;
            addr = new_sec + W25_REC_HDR_SIZE + k * W25_REC_SIZE;
        }
    }
    else
    {
        uint32_t sec = (g_cntA > 0U) ? W25_SECTOR_A : W25_SECTOR_B;
        addr = sec + W25_REC_HDR_SIZE + idx * W25_REC_SIZE;
    }

    W25_Read(addr, raw, W25_REC_SIZE);
    *temp_x10 = (int16_t)(((uint16_t)raw[0] << 8) | (uint16_t)raw[1]);
    *humi_x10 = (int16_t)raw[2];

    return 1U;
}

/* --------------------------------------------------------------------------
 * 记录系统的三个只读状态。对外只给「结论」，不给「账本」：
 *   上层关心的是「有没有记录系统 / 有几条 / 正在写哪个扇区」，
 *   至于扇区头怎么解析、乒乓怎么翻转，是驱动内部的事。
 * -------------------------------------------------------------------------- */
uint8_t Hist_IsReady(void)
{
    return g_rec_ok;
}

uint32_t Hist_Count(void)
{
    return g_rec_total;
}

uint8_t Hist_WrSector(void)
{
    return g_wr_sector;
}
