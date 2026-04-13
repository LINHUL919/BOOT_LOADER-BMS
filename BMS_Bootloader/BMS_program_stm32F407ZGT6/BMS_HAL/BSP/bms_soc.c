/**
 * @file    bms_soc.c
 * @brief   库仑计SOC + 容量学习 + SOH + 循环计数 + Flash持久化
 *
 *  【算法流程 (每500ms调用一次)】
 *  ┌─────────────────────────────────────────────┐
 *  │ 1. 库仑积分: soc_mAh -= I * dt / 3600000   │
 *  │ 2. 循环计数: 累积|充电量| ≥ nominal → +1    │
 *  │ 3. OCV校准:  静置30s→电压查表渐进10%修正    │
 *  │ 4. 满充校准:  V≥4150 & |I|<200 → SOC=100%   │
 *  │ 5. 空电检测:  V≤2800 → SOC=0%, arm学习      │
 *  │ 6. 容量学习:  空→满 记录Ah, EMA更新 full_cap │
 *  │ 7. 限幅 & 计算百分比                        │
 *  │ 8. 定时EEPROM保存(变化≥1%才真正写入)   │
 *  └─────────────────────────────────────────────┘
 *
 *  【EEPROM存储结构】 (共20字节, W24C02 @ I2C1)
 *    magic(4) + soc_mAh(4) + full_cap_mAh(4) + cycle_x10(2)
 *    + pad(2) + checksum(4)
 *    存储地址: 0x20~0x33 (避开Bootloader使用的0x10~0x12)
 *
 *  【EEPROM优势】
 *    W24C02支持~100万次擦写, 远优于STM32 Flash的~10000次.
 *    无需擦除操作, 直接覆盖写入, 简化代码逻辑.
 */

/* Includes ------------------------------------------------------------------*/
#include "bms_soc.h"
#include "i2c.h"                  /* F407 HAL I2C1 句柄 (hi2c1) */
#include <string.h>
#include <stddef.h>               /* offsetof */

/* ================================================================
 *  OCV查找表 (单体电压 → SOC百分比)
 *  适用于三元锂电池(NMC), 根据实际曲线调整
 * ================================================================ */
static const struct {
    int     mv;
    uint8_t soc;
} s_ocv[] = {
    {4150, 100},
    {4100,  95},
    {4050,  90},
    {4000,  85},
    {3950,  80},
    {3900,  75},
    {3850,  70},
    {3800,  65},
    {3750,  60},
    {3700,  50},
    {3650,  40},
    {3600,  30},
    {3500,  20},
    {3400,  15},
    {3300,  10},
    {3200,   5},
    {3100,   3},
    {3000,   1},
    {2800,   0},
};
#define OCV_TABLE_SIZE  (sizeof(s_ocv) / sizeof(s_ocv[0]))

/* ================================================================
 *  Flash 存储布局 (结构体复用, 存储介质改为EEPROM)
 * ================================================================ */
typedef struct {
    uint32_t magic;               /* SOC_FLASH_MAGIC */
    float    soc_mAh;             /* 剩余容量 */
    float    full_cap_mAh;        /* 学习到的满充容量 */
    uint16_t cycle_count_x10;     /* 循环×10 */
    uint16_t _pad;                /* 对齐填充 */
    uint32_t checksum;            /* 异或校验 */
} SOC_Flash_t;                    /* 共20字节 */

/* ================================================================
 *  内部变量
 * ================================================================ */
static SOC_State_t s_soc;

/* ================================================================
 *  OCV 查表
 * ================================================================ */
uint8_t SOC_OcvLookup(int cell_mv)
{
    for (uint32_t i = 0; i < OCV_TABLE_SIZE; i++) {
        if (cell_mv >= s_ocv[i].mv)
            return s_ocv[i].soc;
    }
    return 0;
}

/* ================================================================
 *  Flash 校验和 — 对 checksum 字段之前的数据做半字异或
 * ================================================================ */
static uint32_t flash_checksum(const SOC_Flash_t *p)
{
    const uint16_t *hw = (const uint16_t *)p;
    uint32_t n = offsetof(SOC_Flash_t, checksum) / 2;
    uint32_t cs = 0;
    for (uint32_t i = 0; i < n; i++)
        cs ^= hw[i];
    return cs ^ 0xA5A5A5A5u;
}

/* ================================================================
 *  EEPROM 读取 — 从W24C02恢复SOC数据
 *  @return 0=成功, -1=无效(首次使用或数据损坏)
 * ================================================================ */
static int eeprom_load(void)
{
    extern I2C_HandleTypeDef hi2c1;
    SOC_Flash_t tmp;

    /* 从EEPROM地址0x20读取20字节 */
    if (HAL_I2C_Mem_Read(&hi2c1, SOC_EEPROM_DEV_ADDR,
                         SOC_EEPROM_BASE_ADDR, I2C_MEMADD_SIZE_8BIT,
                         (uint8_t *)&tmp, sizeof(tmp),
                         100) != HAL_OK)
        return -1;

    /* 校验魔数 */
    if (tmp.magic != SOC_FLASH_MAGIC)
        return -1;

    /* 校验数据完整性 */
    if (tmp.checksum != flash_checksum(&tmp))
        return -1;

    /* 合理性检查 */
    if (tmp.soc_mAh < 0.0f || tmp.full_cap_mAh < 1000.0f)
        return -1;

    /* 恢复数据 */
    s_soc.soc_mAh         = tmp.soc_mAh;
    s_soc.full_cap_mAh    = tmp.full_cap_mAh;
    s_soc.cycle_count_x10 = tmp.cycle_count_x10;
    return 0;
}

/* ================================================================
 *  EEPROM 写入 — 将SOC数据写入W24C02
 *  W24C02页大小8字节, 跨页需分段写+5ms延时
 * ================================================================ */
void SOC_SaveToEEPROM(void)
{
    extern I2C_HandleTypeDef hi2c1;
    SOC_Flash_t tmp;
    tmp.magic           = SOC_FLASH_MAGIC;
    tmp.soc_mAh         = s_soc.soc_mAh;
    tmp.full_cap_mAh    = s_soc.full_cap_mAh;
    tmp.cycle_count_x10 = s_soc.cycle_count_x10;
    tmp._pad            = 0;
    tmp.checksum        = flash_checksum(&tmp);

    /* W24C02 页大小8字节, 20字节需要3次页写入:
     * 页1: addr 0x20~0x27 (8B)
     * 页2: addr 0x28~0x2F (8B)
     * 页3: addr 0x30~0x33 (4B)
     */
    const uint8_t *src = (const uint8_t *)&tmp;
    uint8_t addr = SOC_EEPROM_BASE_ADDR;
    uint16_t remaining = sizeof(tmp);
    uint16_t offset = 0;

    while (remaining > 0) {
        /* 计算当前页内剩余空间 */
        uint8_t page_space = 8 - (addr % 8);
        uint8_t write_len = (remaining < page_space) ? remaining : page_space;

        HAL_I2C_Mem_Write(&hi2c1, SOC_EEPROM_DEV_ADDR,
                          addr, I2C_MEMADD_SIZE_8BIT,
                          (uint8_t *)&src[offset], write_len,
                          100);
        HAL_Delay(5);  /* W24C02 写周期 ~5ms */

        addr      += write_len;
        offset    += write_len;
        remaining -= write_len;
    }

    /* 更新保存基准 */
    s_soc.last_saved_mAh = s_soc.soc_mAh;
}

/* ================================================================
 *  SOC_Init — 模块初始化
 *  优先从Flash恢复, 失败则设为默认值(50%)
 * ================================================================ */
void SOC_Init(void)
{
    memset(&s_soc, 0, sizeof(s_soc));

    /* 默认值 */
    s_soc.full_cap_mAh = (float)SOC_NOMINAL_CAP_MAH;
    s_soc.soc_mAh      = (float)SOC_NOMINAL_CAP_MAH * 0.5f;   /* 默认50% */

    /* 尝试EEPROM恢复 */
    if (eeprom_load() == 0) {
        /* 恢复成功 — soc_mAh / full_cap_mAh / cycle_count_x10 已加载 */
    }

    /* 限幅 */
    if (s_soc.soc_mAh < 0.0f)
        s_soc.soc_mAh = 0.0f;
    if (s_soc.soc_mAh > s_soc.full_cap_mAh)
        s_soc.soc_mAh = s_soc.full_cap_mAh;

    /* 计算百分比 */
    s_soc.soc_percent = (uint8_t)(s_soc.soc_mAh * 100.0f
                                  / s_soc.full_cap_mAh + 0.5f);
    if (s_soc.soc_percent > 100) s_soc.soc_percent = 100;

    /* SOH */
    s_soc.soh_percent = (uint8_t)(s_soc.full_cap_mAh * 100.0f
                                  / (float)SOC_NOMINAL_CAP_MAH + 0.5f);
    if (s_soc.soh_percent > 100) s_soc.soh_percent = 100;

    /* 保存基准 */
    s_soc.last_saved_mAh = s_soc.soc_mAh;
}

/* ================================================================
 *  SOC_Update — 主更新函数 (每500ms调用一次)
 * ================================================================ */
void SOC_Update(int current_mA, int pack_mv, int avg_cell_mv,
                uint8_t chg_on, uint32_t dt_ms)
{
    (void)pack_mv;  /* 当前仅使用 avg_cell_mv, pack_mv 保留备用 */

    /* -------- 1. 库仑积分 --------
     * current_mA: 正=放电, 负=充电
     * 放电时 soc_mAh 减少, 充电时增加
     */
    float delta_mAh = -(float)current_mA * (float)dt_ms / 3600000.0f;
    s_soc.soc_mAh += delta_mAh;

    /* -------- 2. 循环计数 --------
     * 累积充电量 ≥ 标称容量 → 计1次循环
     */
    if (current_mA < 0) {
        float chg_mAh = (float)(-current_mA) * (float)dt_ms / 3600000.0f;
        s_soc.chg_accum_mAh += chg_mAh;
        while (s_soc.chg_accum_mAh >= (float)SOC_NOMINAL_CAP_MAH) {
            s_soc.chg_accum_mAh -= (float)SOC_NOMINAL_CAP_MAH;
            s_soc.cycle_count_x10 += 10;   /* +1.0 次循环 */
        }
    }

    /* -------- 3. OCV校准 (静置时) --------
     * |I| < 50mA 持续 30s → 用电压查表渐进修正
     */
    if (current_mA > -(int)SOC_IDLE_CURRENT_MA &&
        current_mA <  (int)SOC_IDLE_CURRENT_MA) {
        s_soc.idle_timer_ms += dt_ms;
        if (s_soc.idle_timer_ms >= SOC_IDLE_TIME_MS) {
            uint8_t ocv_soc = SOC_OcvLookup(avg_cell_mv);
            float ocv_mAh = s_soc.full_cap_mAh * (float)ocv_soc / 100.0f;
            /* 每次修正10%, 避免阶跃 */
            s_soc.soc_mAh += (ocv_mAh - s_soc.soc_mAh) * 0.1f;
            /* 钳位计时器, 不再继续累加 */
            s_soc.idle_timer_ms = SOC_IDLE_TIME_MS;
        }
    } else {
        s_soc.idle_timer_ms = 0;   /* 有电流, 重置计时 */
    }

    /* -------- 4. 满充检测 + 容量学习 --------
     * 条件: 充电MOS开, |I| < 200mA, V_cell ≥ 4150mV
     */
    if (chg_on && current_mA < 0
        && (-current_mA) < (int)SOC_FULL_CURRENT_MA
        && avg_cell_mv >= (int)SOC_FULL_CELL_MV) {

        /* 容量学习: 空电→满充 期间累积的库仑量 */
        if (s_soc.cap_learn_armed) {
            float learned = s_soc.soc_mAh - s_soc.cap_learn_start_mAh;
            /* 合理性: 0.5×~1.5× 标称容量 */
            if (learned > (float)SOC_NOMINAL_CAP_MAH * 0.5f &&
                learned < (float)SOC_NOMINAL_CAP_MAH * 1.5f) {
                /* EMA更新: 80%旧值 + 20%新值 */
                s_soc.full_cap_mAh = s_soc.full_cap_mAh * 0.8f
                                   + learned * 0.2f;
                s_soc.soh_percent = (uint8_t)(s_soc.full_cap_mAh * 100.0f
                                              / (float)SOC_NOMINAL_CAP_MAH
                                              + 0.5f);
                if (s_soc.soh_percent > 100) s_soc.soh_percent = 100;
            }
            s_soc.cap_learn_armed = 0;
        }

        /* 满充校准: SOC钳至100% */
        s_soc.soc_mAh = s_soc.full_cap_mAh;
    }

    /* -------- 5. 空电检测 + 容量学习准备 --------
     * V_cell ≤ 2800mV → SOC=0%, 准备下次学习
     */
    if (avg_cell_mv <= (int)SOC_EMPTY_CELL_MV && avg_cell_mv > 0) {
        s_soc.soc_mAh = 0.0f;
        s_soc.cap_learn_armed = 1;
        s_soc.cap_learn_start_mAh = 0.0f;
    }

    /* -------- 6. 限幅 & 百分比 -------- */
    if (s_soc.soc_mAh < 0.0f) s_soc.soc_mAh = 0.0f;
    if (s_soc.soc_mAh > s_soc.full_cap_mAh)
        s_soc.soc_mAh = s_soc.full_cap_mAh;

    s_soc.soc_percent = (uint8_t)(s_soc.soc_mAh * 100.0f
                                  / s_soc.full_cap_mAh + 0.5f);
    if (s_soc.soc_percent > 100) s_soc.soc_percent = 100;

    /* -------- 7. 定时EEPROM保存 --------
     * 每60s检查, 变化 ≥ 1% full_cap 才真正写入
     */
    s_soc.eeprom_timer_ms += dt_ms;
    if (s_soc.eeprom_timer_ms >= (uint32_t)SOC_EEPROM_SAVE_INTERVAL_S * 1000u) {
        s_soc.eeprom_timer_ms = 0;
        float diff = s_soc.soc_mAh - s_soc.last_saved_mAh;
        if (diff < 0.0f) diff = -diff;
        if (diff >= s_soc.full_cap_mAh * 0.01f) {
            SOC_SaveToEEPROM();
        }
    }
}

/* ================================================================
 *  获取状态结构指针
 * ================================================================ */
SOC_State_t *SOC_GetState(void)
{
    return &s_soc;
}
