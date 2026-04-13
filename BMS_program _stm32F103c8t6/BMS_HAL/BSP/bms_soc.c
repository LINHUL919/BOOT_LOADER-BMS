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
 *  │ 8. 定时Flash保存(变化≥1%才真正写入)         │
 *  └─────────────────────────────────────────────┘
 *
 *  【Flash存储结构】 (共20字节, STM32F103 以半字编程)
 *    magic(4) + soc_mAh(4) + full_cap_mAh(4) + cycle_x10(2)
 *    + pad(2) + checksum(4)
 *
 *  【Flash磨损控制】
 *    每60s检查一次, 仅当 soc_mAh 变化 ≥ 1% full_cap 时才擦写.
 *    STM32F103 Flash 耐久 ~10000 次; 若电池1C放电(20A),
 *    SOC每分钟变约1.67%, 约每36s触发一次写入 → ~10000次
 *    ≈ 100小时. 若0.5C放电, ~200小时. 生产环境建议外挂EEPROM.
 */

/* Includes ------------------------------------------------------------------*/
#include "bms_soc.h"
#include "stm32f1xx_hal.h"
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
 *  Flash 存储布局
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
 *  Flash 读取 — 从最后页恢复SOC数据
 *  @return 0=成功, -1=无效(首次使用或数据损坏)
 * ================================================================ */
static int flash_load(void)
{
    const SOC_Flash_t *fp = (const SOC_Flash_t *)SOC_FLASH_ADDR;

    /* 校验魔数 */
    if (fp->magic != SOC_FLASH_MAGIC)
        return -1;

    /* 校验数据完整性 */
    if (fp->checksum != flash_checksum(fp))
        return -1;

    /* 合理性检查 */
    if (fp->soc_mAh < 0.0f || fp->full_cap_mAh < 1000.0f)
        return -1;

    /* 恢复数据 */
    s_soc.soc_mAh         = fp->soc_mAh;
    s_soc.full_cap_mAh    = fp->full_cap_mAh;
    s_soc.cycle_count_x10 = fp->cycle_count_x10;
    return 0;
}

/* ================================================================
 *  Flash 写入 — 擦除最后页并编程
 * ================================================================ */
void SOC_SaveToFlash(void)
{
    SOC_Flash_t tmp;
    tmp.magic           = SOC_FLASH_MAGIC;
    tmp.soc_mAh         = s_soc.soc_mAh;
    tmp.full_cap_mAh    = s_soc.full_cap_mAh;
    tmp.cycle_count_x10 = s_soc.cycle_count_x10;
    tmp._pad            = 0;
    tmp.checksum        = flash_checksum(&tmp);

    HAL_FLASH_Unlock();

    /* 擦除页 */
    FLASH_EraseInitTypeDef erase;
    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = SOC_FLASH_ADDR;
    erase.NbPages     = 1;
    uint32_t page_err = 0;
    HAL_FLASHEx_Erase(&erase, &page_err);

    /* 以半字(16bit)写入 */
    const uint16_t *src = (const uint16_t *)&tmp;
    uint32_t addr = SOC_FLASH_ADDR;
    for (uint32_t i = 0; i < sizeof(SOC_Flash_t) / 2; i++) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, src[i]);
        addr += 2;
    }

    HAL_FLASH_Lock();

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

    /* 尝试Flash恢复 */
    if (flash_load() == 0) {
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

    /* -------- 7. 定时Flash保存 --------
     * 每60s检查, 变化 ≥ 1% full_cap 才真正写入
     */
    s_soc.flash_timer_ms += dt_ms;
    if (s_soc.flash_timer_ms >= (uint32_t)SOC_FLASH_SAVE_INTERVAL_S * 1000u) {
        s_soc.flash_timer_ms = 0;
        float diff = s_soc.soc_mAh - s_soc.last_saved_mAh;
        if (diff < 0.0f) diff = -diff;
        if (diff >= s_soc.full_cap_mAh * 0.01f) {
            SOC_SaveToFlash();
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
