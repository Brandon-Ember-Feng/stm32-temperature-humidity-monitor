#include "dht11_frame.h"

/* =============================================================
 * dht11_frame.c —— DHT11 帧解析实现（纯逻辑）
 *
 * 本文件刻意不 include 任何 bsp_*.h：它只做位运算，不需要知道 GPIO 的
 * 存在。单元测试的编译命令只给 -IUtil -IDrivers，一旦有人往后往里加
 * 硬件依赖，PC 端编译会立刻失败 —— 等于用构建配置守住这条边界。
 * ============================================================= */

uint8_t DHT_CheckSum(const uint8_t raw[DHT_FRAME_LEN])
{
    /* 前 4 字节求和后只取最低 8 位（uint8_t 自然溢出即可，不用手动取模） */
    uint8_t sum = (uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]);

    return (sum == raw[4]) ? 1U : 0U;
}

int16_t DHT_DecodeTemp(const uint8_t raw[DHT_FRAME_LEN])
{
    int16_t t;

    if ((raw[2] & 0x80U) != 0U)
    {
        /* 最高位为 1 = 零下温度：取低 7 位算出绝对值，再取负 */
        t = (int16_t)(-((int16_t)(raw[2] & 0x7FU) * 10 + (int16_t)raw[3]));
    }
    else
    {
        t = (int16_t)((int16_t)raw[2] * 10 + (int16_t)raw[3]);
    }

    return t;
}

int16_t DHT_DecodeHumi(const uint8_t raw[DHT_FRAME_LEN])
{
    /* DHT11 的小数位通常为 0，这样写是为了兼容小数位非 0 的型号 */
    return (int16_t)((int16_t)raw[0] * 10 + (int16_t)raw[1]);
}
