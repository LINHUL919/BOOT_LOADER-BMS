/**
 * @file    bq76940.c
 * @brief   BQ76940电池管理芯片驱动实现 (HAL库版本)
 * @note    移植自原工程 BSP/BQ76930.c
 *
 *          【原代码数据采集流程】
 *          主循环中调用 Get_Update_Data(), 内部依次执行:
 *            Get_Battery1()~15()   -> 逐节读取15节电压
 *            Get_Update_ALL_Data() -> 累加总电压
 *            Get_BQ1_2_Temp()      -> 读NTC温度
 *            Get_BQ_Current()      -> 读库仑计电流
 *
 *          【电压计算公式】
 *          voltage_mV = (raw_adc * GAIN) / 1000 + ADC_offset
 *          GAIN = 365 + adc_gain_bits (uV), 典型值约377
 *
 *          【I2C通信协议】
 *          写(带CRC): START->0x10->reg->data->CRC8->STOP
 *          读(无CRC): START->0x10->reg->RESTART->0x11->读1字节->STOP
 *          CRC8多项式, key=7
 */

/* Includes ------------------------------------------------------------------*/
#include "bq76940.h"
#include "bms_soc.h"
#include "sw_i2c.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* FreeRTOS — 用于 vTaskDelay 和xTaskNotifyFromISR */
#include "FreeRTOS.h"
#include "task.h"

/* 全局BMS数据结构 */
BQ76940_Data_t bms_data;

/* ==================== ALERT 中断通知 ==================== */
volatile uint8_t g_bq_alert_flag = 0;
static TaskHandle_t s_alert_task = NULL;  /* BMS_Task句柄, 用于ISR直接唤醒 */

/**
 * @brief  注册接收ALERT通知的任务
 * @param  handle  BMS_Task的任务句柄(在main.c中创建后调用)
 */
void BQ76940_SetAlertTask(void *handle)
{
    s_alert_task = (TaskHandle_t)handle;
}

/**
 * @brief  智能延时: 调度器运行时用vTaskDelay释放CPU, 否则用HAL_Delay
 * @note   BQ76940_Init()在调度器启动前/后都可能被调用(重初始化),,
 *         此函数自动选择合适的延时方式
 */
static void BQ_Delay(uint32_t ms)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
        vTaskDelay(pdMS_TO_TICKS(ms));
    else
        HAL_Delay(ms);
}

/**
 * @brief  ALERT中断回调 — 置标志+ 任务通知
 * @note   软件I2C不能在中断中使用, 实际处理在BMS_Task
 */
void BQ76940_AlertCallback(void)
{
    g_bq_alert_flag = 1;

    /* 如果已注册BMS_Task, 通过任务通知立即唤醒 (无需等500ms轮询) */
    if (s_alert_task != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(s_alert_task, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/**
 * @brief  HAL GPIO EXTI回调 (覆盖 __weak 版本)
 *         PB2 下降沿 →BQ76940 ALERT触发
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_2) {
        BQ76940_AlertCallback();
    }
}

/* ===================== I2C通信健康统计 ===================== */
static volatile uint32_t g_bq_ack_fail_total = 0;
static volatile uint16_t g_bq_ack_fail_continuous = 0;
static volatile uint8_t g_bq_last_transfer_ok = 1;

static uint32_t g_bq_last_reinit_tick = 0; /* 上次重初始化的时刻(ms) */
static uint16_t g_bq_reinit_count = 0;     /* 累计重初始化次数 */

/* 前置声明: 避免在首次调用时出现隐式声明 */
static void BQ_WriteReg(uint8_t reg, uint8_t data);
static uint8_t BQ_ReadReg(uint8_t reg);
static void BQ_CellFilterReset(uint8_t idx, int seed_mv);
static void BQ_TempFilterReset(float seed_temp);

static void BQ76940_CommRecord(uint8_t ok)
{
    if (ok)
    {
        g_bq_last_transfer_ok = 1;
        g_bq_ack_fail_continuous = 0;
    }
    else
    {
        g_bq_last_transfer_ok = 0;
        g_bq_ack_fail_total++;
        if (g_bq_ack_fail_continuous < 0xFFFFU)
        {
            g_bq_ack_fail_continuous++;
        }
    }
}

void BQ76940_CommHealthReset(void)
{
    g_bq_ack_fail_total = 0;
    g_bq_ack_fail_continuous = 0;
    g_bq_last_transfer_ok = 1;
}

void BQ76940_CommHealthGet(BQ76940_CommHealth_t *info)
{
    if (info == NULL)
        return;
    info->ack_fail_total = g_bq_ack_fail_total;
    info->ack_fail_continuous = g_bq_ack_fail_continuous;
    info->last_transfer_ok = g_bq_last_transfer_ok;
    info->reinit_count = g_bq_reinit_count; /* 新增 */
}

/**
 * @brief  通信健康检查+ 自动恢复
 * @note   在主循环, 每次调用 BQ76940_UpdateAll() 之前调用本函数
 *
 *         工作逻辑:
 *           1. 读取当前连续失败次数
 *           2. 如果 < BQ_COMM_FAIL_THRESHOLD, 返回1(正常)
 *           3. 如果 >= 阈值 检查距上次重初始化是否超过最小间隔
 *           4. 超过间隔 -> 执行完整重初始化流程 -> 返回0(本轮跳过)
 *           5. 未超过间隔-> 返回0(等待, 不重复重启)
 *
 *         重初始化流程与 BQ76940_Init() 完全一致
 *           唤醒脉冲 -> 寄存器写入 -> 读校准 -> 设保护 -> 清状态
 *
 *         对应原代码中没有的功能 这是全新增加的安全机制
 */
uint8_t BQ76940_CommCheck(void)
{
    /* 连续失败次数未达阈值 通信正常 */
    if (g_bq_ack_fail_continuous < BQ_COMM_FAIL_THRESHOLD)
    {
        return 1;
    }

    /* 达到阈值 检查是否允许重初始化(防止高频反复重启) */
    uint32_t now = HAL_GetTick();
    if ((now - g_bq_last_reinit_tick) < BQ_REINIT_MIN_INTERVAL)
    {
        return 0; /* 距上次重初始化太近, 等一等*/
    }

    /* ---- 执行重初始化 ---- */
    g_bq_last_reinit_tick = now;
    g_bq_reinit_count++;

    /* 先重置统计 避免重初始化过程中再次触发*/
    BQ76940_CommHealthReset();

    /* 完整重初始化流程 (BQ76940_Init 一致 */
    BQ76940_Init();

    return 0; /* 本轮刚重初始化, 跳过采集, 下轮再读数据 */
}

/* ===================== MOS管控制 =====================
 * 对应原代码BQ76930.c 中的:
 *   BMS_STA()         -> 读SYS_CTRL2获取当前MOS状态
 *   Only_Open_CHG()   -> 开充电
 *   Only_Close_CHG()  -> 关充电
 *   Only_Open_DSG()   -> 开放电
 *   Only_Close_DSG()  -> 关放电
 *   Open_DSG_CHG()    -> 全开
 *   Close_DSG_CHG()   -> 全关
 *
 * 原代码每次操作前调用BMS_STA()读寄存器获取当前状态
 * 然后根据另一个MOS的状态决定写入值 保证不互相影响
 *
 * 这里统一用一个内部函数读状态 6个外部函数分别操作
 * ===================================================== */

/**
 * @brief  读取SYS_CTRL2, 更新bms_data中的MOS状态
 * @note   对应原代码BMS_STA()
 *         原代码 BMS_sta = IIC1_read_one_byte(SYS_CTRL2);
 *                 DSG_STA = BMS_sta & 0x02;
 *                 CHG_STA = BMS_sta & 0x01;
 */
static void BQ_ReadMosStatus(void)
{
    uint8_t reg = BQ_ReadReg(REG_SYS_CTRL2);
    bms_data.dsg_on = (reg & 0x02) ? 1 : 0;
    bms_data.chg_on = (reg & 0x01) ? 1 : 0;
}

/**
 * @brief  打开充电MOS, 保持放电MOS不变
 * @note   对应原代码Only_Open_CHG():
 *           if(DSG_STA!=0) write(0x43); else write(0x41);
 */
void BQ76940_OpenCHG(void)
{
    BQ_WriteReg(REG_SYS_STAT, 0xFF);   /* 清除告警标志, 否则BQ76940拒绝开FET */
    BQ_ReadMosStatus();
    if (bms_data.dsg_on)
    {
        BQ_WriteReg(REG_SYS_CTRL2, 0x43); /* DSG=1 CHG=1 */
    }
    else
    {
        BQ_WriteReg(REG_SYS_CTRL2, 0x41); /* DSG=0 CHG=1 */
    }
}

/**
 * @brief  关闭充电MOS, 保持放电MOS不变
 * @note   对应原代码Only_Close_CHG():
 *           if(DSG_STA!=0) write(0x42); else write(0x40);
 */
void BQ76940_CloseCHG(void)
{
    BQ_ReadMosStatus();
    if (bms_data.dsg_on)
    {
        BQ_WriteReg(REG_SYS_CTRL2, 0x42); /* DSG=1 CHG=0 */
    }
    else
    {
        BQ_WriteReg(REG_SYS_CTRL2, 0x40); /* DSG=0 CHG=0 */
    }
    /* 不再清均衡手动关CHG不应影响均衡, 保护清均衡在BQ_CheckXX*/
}

/**
 * @brief  打开放电MOS, 保持充电MOS不变
 * @note   对应原代码Only_Open_DSG():
 *           if(CHG_STA!=0) write(0x43); else write(0x42);
 */
void BQ76940_OpenDSG(void)
{
    BQ_WriteReg(REG_SYS_STAT, 0xFF);   /* 清除告警标志, 否则BQ76940拒绝开FET */
    BQ_ReadMosStatus();
    if (bms_data.chg_on)
    {
        BQ_WriteReg(REG_SYS_CTRL2, 0x43); /* DSG=1 CHG=1 */
    }
    else
    {
        BQ_WriteReg(REG_SYS_CTRL2, 0x42); /* DSG=1 CHG=0 */
    }
}

/**
 * @brief  关闭放电MOS, 保持充电MOS不变
 * @note   对应原代码Only_Close_DSG():
 *           if(CHG_STA!=0) write(0x41); else write(0x40);
 */
void BQ76940_CloseDSG(void)
{
    BQ_ReadMosStatus();
    if (bms_data.chg_on)
    {
        BQ_WriteReg(REG_SYS_CTRL2, 0x41); /* DSG=0 CHG=1 */
    }
    else
    {
        BQ_WriteReg(REG_SYS_CTRL2, 0x40); /* DSG=0 CHG=0 */
    }
    /* 不再清均衡手动关DSG不应影响均衡, 保护清均衡在BQ_CheckXX*/
}

/**
 * @brief  同时打开充电+放电MOS
 * @note   对应原代码Open_DSG_CHG(): write(0x43)
 */
void BQ76940_OpenAll(void)
{
    BQ_WriteReg(REG_SYS_STAT, 0xFF);   /* 清除告警标志, 否则BQ76940拒绝开FET */
    BQ_WriteReg(REG_SYS_CTRL2, 0x43); /* CC_EN=1 DSG=1 CHG=1 */
}

/**
 * @brief  同时关闭充电+放电MOS
 * @note   对应原代码Close_DSG_CHG(): write(0x40)
 */
void BQ76940_CloseAll(void)
{
    BQ_WriteReg(REG_SYS_CTRL2, 0x40); /* CC_EN=1 DSG=0 CHG=0 */
    /* 不再清均衡手动MOS全关不应影响均衡, 保护清均衡在BQ_CheckXX*/
}

/* ==================== 保护逻辑 ====================
 * 对应原代码main.c 主循环中的保护判断
 *
 * 原代码保护逻辑特点:
 *   1. 过压: 只检查部分电压0,1,4,5,6,9,10,11,14), 不是全部15节
 *      -> 这里改进为检查全部15节, 更安
 *   2. 恢复判断: 原代码也只检查部分电压
 *      -> 这里改进为全5节都满足才恢复
 *   3. 过温: 原代码从Flash读阈 这里先用宏定
 *      -> 并增加回OT_THRESHOLD和OT_RECOVER不同), 防止温度抖动反复触发
 *   4. 每次保护动作后都清SYS_STAT, 与原代码一致
 * ===================================================== */

static BQ76940_Protection_t g_prot = {0};

/* ==================== 简单滑动平均去抖状态====================
 * 这些变量仅用于滤波内 不对外暴露
 * ============================================================ */
static int g_cell_hist[CELL_COUNT][BQ_FILTER_WINDOW_SIZE];
static uint8_t g_cell_hist_cnt[CELL_COUNT];
static uint8_t g_cell_hist_widx[CELL_COUNT];

static float g_temp_hist[BQ_FILTER_WINDOW_SIZE];
static uint8_t g_temp_hist_cnt = 0;
static uint8_t g_temp_hist_widx = 0;

/**
 * @brief  过压保护检查
 * @note   对应原代码
 *           if((Batteryval[0]>4200)||...多个电芯...) {
 *               Only_Close_CHG();
 *               IIC1_write_one_byte_CRC(SYS_STAT,0xFF);
 *               OV_FLAG=1;
 *           }
 *           if(OV_FLAG==1) {
 *               if((Batteryval[0]<4100)&&...全部电芯...) {
 *                   Only_Open_CHG(); OV_FLAG=0;
 *               }
 *           }
 */
static void BQ_CheckOV(void)
{
    /* --- 触发检查: 任一电芯超过阈值--- */
    if (!g_prot.ov_flag)
    {
        for (int i = 0; i < CELL_COUNT; i++)
        {
            if (bms_data.cell_mv[i] > BQ_OV_THRESHOLD)
            {
                BQ76940_CloseCHG();              /* 关充电MOS */
                 BQ76940_ClearBalance();   /* 保护触发时停止均衡 */
                BQ_WriteReg(REG_SYS_STAT, 0xFF); /* 清状态 */
                g_prot.ov_flag = 1;
                return;
            }
        }
    }

    /* --- 恢复检查: 全部电芯低于恢复阈值--- */
    if (g_prot.ov_flag)
    {
        for (int i = 0; i < CELL_COUNT; i++)
        {
            if (bms_data.cell_mv[i] >= BQ_OV_RECOVER)
            {
                return; /* 还有电芯没降下来, 继续保护 */
            }
        }
        /* 全部低于恢复阈值 解除保护 */
        BQ76940_OpenCHG();
        BQ_WriteReg(REG_SYS_STAT, 0xFF);
        g_prot.ov_flag = 0;
    }
}

/**
 * @brief  欠压保护检查
 * @note   对应原代码
 *           if((Batteryval[0]<2800)||...) { Only_Close_DSG(); UV_FLAG=1; }
 *           if(UV_FLAG==1) { if(全部>2800) { Only_Open_DSG(); UV_FLAG=0; } }
 */
static void BQ_CheckUV(void)
{
    if (!g_prot.uv_flag)
    {
        for (int i = 0; i < CELL_COUNT; i++)
        {
            if (bms_data.cell_mv[i] < BQ_UV_THRESHOLD)
            {
                BQ76940_CloseDSG();
                 BQ76940_ClearBalance();   /* 保护触发时停止均衡 */
                BQ_WriteReg(REG_SYS_STAT, 0xFF);
                g_prot.uv_flag = 1;
                return;
            }
        }
    }

    if (g_prot.uv_flag)
    {
        for (int i = 0; i < CELL_COUNT; i++)
        {
            if (bms_data.cell_mv[i] <= BQ_UV_RECOVER)
            {
                return; /* 还有电芯没恢复*/
            }
        }
        BQ76940_OpenDSG();
        BQ_WriteReg(REG_SYS_STAT, 0xFF);
        g_prot.uv_flag = 0;
    }
}

/**
 * @brief  过流保护检查
 * @note   对应原代码
 *           if(Batteryval[17]>5000) { Close_DSG_CHG(); OC_FLAG=1; }
 *           if(OC_FLAG==1) { if(<5000) { Open_DSG_CHG(); OC_FLAG=0; } }
 *
 *         注意: 原代码用绝对值判断 current_mA可能为负(充电)
 *         这里取绝对比 充放电都保护
 */
static void BQ_CheckOC(void)
{
    int abs_current = bms_data.current_mA;
    if (abs_current < 0)
        abs_current = -abs_current;

    if (!g_prot.oc_flag)
    {
        if (abs_current > BQ_OC_THRESHOLD)
        {
            BQ76940_CloseAll();
             BQ76940_ClearBalance();   /* 保护触发时停止均衡 */
            BQ_WriteReg(REG_SYS_STAT, 0xFF);
            g_prot.oc_flag = 1;
        }
    }

    if (g_prot.oc_flag)
    {
        if (abs_current < BQ_OC_RECOVER)
        {
            BQ76940_OpenAll();
            BQ_WriteReg(REG_SYS_STAT, 0xFF);
            g_prot.oc_flag = 0;
        }
    }
}

/**
 * @brief  过温保护检查
 * @note   对应原代码
 *           if(Batteryval[18]>Read_Flash(Temp_up)) { Close_DSG_CHG(); Temp_up_flag=1; }
 *           if(Temp_up_flag==1) { if(<阈值 { Open_DSG_CHG(); Temp_up_flag=0; } }
 *
 *         改进: 触发阈值60°C)和恢复阈55°C)不同, 加入5°C回差
 *         防止温度在阈值附近抖动导致MOS反复开关
 */
static void BQ_CheckOT(void)
{
    if (!g_prot.ot_flag)
    {
        if (bms_data.temp_degC > (float)BQ_OT_THRESHOLD)
        {
            BQ76940_CloseAll();
             BQ76940_ClearBalance();   /* 保护触发时停止均衡 */
            BQ_WriteReg(REG_SYS_STAT, 0xFF);
            g_prot.ot_flag = 1;
        }
    }

    if (g_prot.ot_flag)
    {
        if (bms_data.temp_degC < (float)BQ_OT_RECOVER)
        {
            BQ76940_OpenAll();
            BQ_WriteReg(REG_SYS_STAT, 0xFF);
            g_prot.ot_flag = 0;
        }
    }
}

/**
 * @brief  低温充电保护检查
 * @note   锂电池在0°C以下充电会导致锂枝晶析出, 严重损害电池安全
 *         触发: 温度 < 0°C → 关闭充电MOS (放电不受影响)
 *         恢复: 温度 > 5°C → 恢复充电MOS
 *         回差5°C防止温度在0°C附近抖动导致MOS反复开关
 */
static void BQ_CheckLT(void)
{
    if (!g_prot.lt_flag)
    {
        if (bms_data.temp_degC < (float)BQ_LT_THRESHOLD)
        {
            BQ76940_CloseCHG();              /* 仅关充电MOS, 放电不受影响 */
            BQ76940_ClearBalance();          /* 低温时也停止均衡 */
            g_prot.lt_flag = 1;
        }
    }

    if (g_prot.lt_flag)
    {
        if (bms_data.temp_degC > (float)BQ_LT_RECOVER)
        {
            BQ76940_OpenCHG();
            g_prot.lt_flag = 0;
        }
    }
}

/**
 * @brief  执行全部保护检查
 * @note   在主循环 BQ76940_UpdateAll() 之后调用
 *         依次棢 过压 -> 欠压 -> 过流 -> 过温 -> 低温充电
 */
void BQ76940_ProtectionCheck(void)
{
    BQ_CheckOV();
    BQ_CheckUV();
    BQ_CheckOC();
    BQ_CheckOT();
    BQ_CheckLT();
}

/**
 * @brief  读取BQ76940硬件报警标志
 * @note   对应原代码ALERT_1_Recognition()
 *         读SYS_STAT寄存器
 *           bit[3]=UV  bit[2]=OV  bit[1]=SCD  bit[0]=OCD
 */
void BQ76940_ReadAlerts(void)
{
    uint8_t sys_stat = BQ_ReadReg(REG_SYS_STAT);

    g_prot.hw_uv = (sys_stat & 0x08) ? 1 : 0;
    g_prot.hw_ov = (sys_stat & 0x04) ? 1 : 0;
    g_prot.hw_scd = (sys_stat & 0x02) ? 1 : 0;
    g_prot.hw_ocd = (sys_stat & 0x01) ? 1 : 0;
}

void BQ76940_ProtectionGet(BQ76940_Protection_t *prot)
{
    if (prot == NULL)
        return;
    *prot = g_prot;
}

void BQ76940_ProtectionReset(void)
{
    memset(&g_prot, 0, sizeof(g_prot));
    BQ_WriteReg(REG_SYS_STAT, 0xFF); /* 同时清硬件标志*/
}

/**
 * @brief  CRC8校验计算
 * @note   完全复制自原代码 CRC8(), 多项式key=7
 *         BQ769x0写操作必须附带此CRC
 * @param  ptr: 数据指针
 * @param  len: 数据长度
 * @param  key: CRC多项式, 固定
 * @retval CRC8
 */
static uint8_t CRC8(uint8_t *ptr, uint8_t len, uint8_t key)
{
    uint8_t i;
    uint8_t crc = 0;

    while (len-- != 0)
    {
        for (i = 0x80; i != 0; i /= 2)
        {
            if ((crc & 0x80) != 0)
            {
                crc *= 2;
                crc ^= key;
            }
            else
            {
                crc *= 2;
            }
            if ((*ptr & i) != 0)
            {
                crc ^= key;
            }
        }
        ptr++;
    }
    return crc;
}

/**
 * @brief  写BQ76940寄存器(带CRC8)
 * @note   对应原代码IIC1_write_one_byte_CRC()
 *         发送序列: START->0x10->reg->data->CRC->STOP
 *         CRC覆盖 [0x10, reg, data] 三个字节
 * @param  reg:  寄存器地址
 * @param  data: 要写入的数据
 */
static void BQ_WriteReg(uint8_t reg, uint8_t data)
{
    uint8_t crc_buf[3];
    uint8_t crc;

    crc_buf[0] = BQ76940_WRITE_ADDR;
    crc_buf[1] = reg;
    crc_buf[2] = data;
    crc = CRC8(crc_buf, 3, 7);

    if (!SW_I2C_Start())
    {
        BQ76940_CommRecord(0);
        return;
    }

    SW_I2C_SendByte(BQ76940_WRITE_ADDR);
    if (!SW_I2C_WaitAck())
        goto i2c_fail;

    SW_I2C_SendByte(reg);
    if (!SW_I2C_WaitAck())
        goto i2c_fail;

    SW_I2C_SendByte(data);
    if (!SW_I2C_WaitAck())
        goto i2c_fail;

    SW_I2C_SendByte(crc);
    if (!SW_I2C_WaitAck())
        goto i2c_fail;

    SW_I2C_Stop();
    BQ76940_CommRecord(1);
    BQ_Delay(10);
    return;

i2c_fail:
    SW_I2C_Stop();
    BQ76940_CommRecord(0);
}

/**
 * @brief  读BQ76940寄存器(无CRC)
 * @note   对应原代码IIC1_read_one_byte()
 *         序列: START->0x10->reg->RESTART->0x11->读1字节->STOP
 * @param  reg: 寄存器地址
 * @retval 读到的数据
 */
static uint8_t BQ_ReadReg(uint8_t reg)
{
    uint8_t val = 0;

    if (!SW_I2C_Start())
    {
        BQ76940_CommRecord(0);
        return 0;
    }

    SW_I2C_SendByte(BQ76940_WRITE_ADDR);
    if (!SW_I2C_WaitAck())
        goto i2c_fail;

    SW_I2C_SendByte(reg);
    if (!SW_I2C_WaitAck())
        goto i2c_fail;

    if (!SW_I2C_Start())
        goto i2c_fail;

    SW_I2C_SendByte(BQ76940_READ_ADDR);
    if (!SW_I2C_WaitAck())
        goto i2c_fail;

    val = SW_I2C_ReceiveByte();
    SW_I2C_NAck();
    SW_I2C_Stop();
    BQ76940_CommRecord(1);
    return val;

i2c_fail:
    SW_I2C_Stop();
    BQ76940_CommRecord(0);
    return 0;
}

/**
 * @brief  读ADC校准参数 (GAIN和OFFSET)
 * @note   对应原代码Get_offset()
 *         GAIN = 365 + ((GAIN1[3:2]<<1) | (GAIN2[7:5]>>5))
 *         OFFSET = 有符号bit (mV)
 */
static void BQ_ReadCalibration(void)
{
    uint8_t gain1, gain2, offset;

    gain1 = BQ_ReadReg(REG_ADCGAIN1);   /* 0x50 */
    gain2 = BQ_ReadReg(REG_ADCGAIN2);   /* 0x59 */
    offset = BQ_ReadReg(REG_ADCOFFSET); /* 0x51 */

    int adc_gain_bits = ((gain1 & 0x0C) << 1) + ((gain2 & 0xE0) >> 5);

    bms_data.adc_gain = 365 + adc_gain_bits;
    bms_data.adc_offset = (int8_t)offset;
}

/**
 * @brief  初始化BQ76940
 * @note   对应原代码BQ76930_config() 完整流程:
 *         1. WAKE_ALL_DEVICE()   -> PA8脉冲唤醒
 *         2. BQ_1_config()       -> 12个寄存器写入
 *         3. Get_offset()        -> 读ADC校准
 *         4. OV_UV_1_PROTECT()   -> 设OV/UV阈值
 *         5. OCD_SCD_PROTECT()   -> 设保护
 *         6. 清SYS_STAT
 */
void BQ76940_Init(void)
{
    BQ76940_CommHealthReset(); /* 新增: 初始化时清零通信统计 */
    /*  唤醒BQ76940
     * 对应原代码WAKE_ALL_DEVICE():
     *   MCU_WAKE_BQ_ONOFF(1); delay_ms(100); MCU_WAKE_BQ_ONOFF(0);
     * BQ76940上电后在SHIP模式, PA8脉冲唤醒到NORMAL模式 */
    HAL_GPIO_WritePin(BQ_WAKE_PORT, BQ_WAKE_PIN, GPIO_PIN_SET);
    BQ_Delay(100);
    HAL_GPIO_WritePin(BQ_WAKE_PORT, BQ_WAKE_PIN, GPIO_PIN_RESET);
    BQ_Delay(200);

    /*  寄存器初始化
     * 对应原代码BQ_1_config() 循环写入:
     *   [0] SYS_STAT  =0xFF  清除所有标志
     *   [1] CELLBAL1  =0x00  关均衡~5
     *   [2] CELLBAL2  =0x00  关均衡~10
     *   [3] CELLBAL3  =0x00  关均衡1~15
     *   [4] SYS_CTRL1 =0x18  ADC_EN=1, TEMP_SEL=1
     *   [5] SYS_CTRL2 =0x43  CC_EN=1, DSG_ON=1, CHG_ON=1
     *   [6] PROTECT1  =0x00
     *   [7] PROTECT2  =0x00
     *   [8] PROTECT3  =0x00
     *   [9] OV_TRIP   =0x00
     *  [10] UV_TRIP   =0x00
     *  [11] CC_CFG    =0x19  库仑计配置*/
    uint8_t init_reg[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                          0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B};
    uint8_t init_data[] = {0xFF, 0x00, 0x00, 0x00, 0x18, 0x43,
                           0x00, 0x00, 0x00, 0x00, 0x00, 0x19};

    for (int i = 0; i < 12; i++)
    {
        BQ_Delay(10);
        BQ_WriteReg(init_reg[i], init_data[i]);
    }

    /*  读ADC校准, 对应原代码Get_offset() */
    BQ_ReadCalibration();

    /*  设过欠压阈值
     * 对应原代码OV_UV_1_PROTECT():
     *   OVPThreshold=4400mV, UVPThreshold=2400mV
     *   t=0.377 (GAIN/1000近似值)
     *   Trip = ((Threshold - offset) / t + 0.5) >> 4 */
    float t = 0.377f;
    uint8_t ov_trip = (uint8_t)((((uint16_t)((4400 - bms_data.adc_offset) / t + 0.5f)) >> 4) & 0xFF);
    uint8_t uv_trip = (uint8_t)((((uint16_t)((2400 - bms_data.adc_offset) / t + 0.5f)) >> 4) & 0xFF);
    BQ_WriteReg(REG_OV_TRIP, ov_trip);
    BQ_WriteReg(REG_UV_TRIP, uv_trip);

    /*  SCD/OCD保护
     * 对应原代码OCD_SCD_PROTECT():
     *   PROTECT1=0xFF  SCD延时400us, 阈值00mV
     *   PROTECT2=0xFF  OCD延时1280ms, 阈值00mV */
    BQ_WriteReg(REG_PROTECT1, 0xFF);
    BQ_WriteReg(REG_PROTECT2, 0xFF);

    /*  清状态寄存器 */
    BQ_WriteReg(REG_SYS_STAT, 0xFF);

    /* 初始化过滤相关状 避免上电随机制*/
    for (int i = 0; i < CELL_COUNT; i++)
    {
        bms_data.cell_raw_mv[i] = 0;
        bms_data.cell_valid[i] = 1;
        bms_data.cell_invalid_cnt[i] = 0;
        bms_data.cell_mv[i] = 3600; /* 给一个安全中间值 等待首次有效采样覆盖 */
    }
    /* 初始化温度过滤状*/
    bms_data.temp_raw_degC = 0.0f;
    bms_data.temp_valid = 1;
    bms_data.temp_invalid_cnt = 0;
    bms_data.temp_degC = 25.0f; /* 上电默认25°C, 防止首轮误触发*/

    /* 初始化电芯滤波窗口*/
    for (int i = 0; i < CELL_COUNT; i++)
    {
        BQ_CellFilterReset((uint8_t)i, bms_data.cell_mv[i]);
    }

    /* 初始化温度滤波窗口*/
    BQ_TempFilterReset(bms_data.temp_degC);

    bms_data.soc_percent = 0;

     /* 均衡初始 关闭所有均衡道 (对应原BSP BQ_1_configCELLBAL1/2/3=0x00) */
    BQ76940_ClearBalance();
}

/**
 * @brief  判断单体电压是否在有效范围内
 * @param  mv: 待判断的电压mV)
 * @retval 1=有效, 0=越界无效
 * @note   小步仅做"越界过滤"
 *         后续/小步会加温度过滤和滑动平均
 */
static uint8_t BQ_IsCellVoltageValid(int mv)
{
    if (mv < BQ_CELL_VALID_MIN_MV)
    {
        return 0;
    }
    if (mv > BQ_CELL_VALID_MAX_MV)
    {
        return 0;
    }
    return 1;
}

/**
 * @brief  取int绝对值
 */
static int BQ_AbsInt(int v)
{
    return (v < 0) ? (-v) : v;
}

/**
 * @brief  取float绝对值
 */
static float BQ_AbsFloat(float v)
{
    return (v < 0.0f) ? (-v) : v;
}

/**
 * @brief  重置某节电芯滤波窗口
 * @param  idx: 电芯索引
 * @param  seed_mv: 初始填充电
 */
static void BQ_CellFilterReset(uint8_t idx, int seed_mv)
{
    g_cell_hist_cnt[idx] = BQ_FILTER_WINDOW_SIZE;
    g_cell_hist_widx[idx] = 0;

    for (int k = 0; k < BQ_FILTER_WINDOW_SIZE; k++)
    {
        g_cell_hist[idx][k] = seed_mv;
    }
}

/**
 * @brief  电芯电压滑动平均 + 去抖
 * @param  idx: 电芯索引
 * @param  sample_mv: 本轮有效采样mV)
 * @retval 过滤后的输出值mV)
 * @note   处理流程:
 *         1) 写入滑动窗口
 *         2) 计算窗口平均
 *         3) 与当前输出比 若变化=死区则保持原值
 */
static int BQ_CellFilterUpdate(uint8_t idx, int sample_mv)
{
    int sum = 0;
    int avg;

    /* 1) 写入环形缓冲 */
    g_cell_hist[idx][g_cell_hist_widx[idx]] = sample_mv;
    g_cell_hist_widx[idx]++;
    if (g_cell_hist_widx[idx] >= BQ_FILTER_WINDOW_SIZE)
    {
        g_cell_hist_widx[idx] = 0;
    }

    if (g_cell_hist_cnt[idx] < BQ_FILTER_WINDOW_SIZE)
    {
        g_cell_hist_cnt[idx]++;
    }

    /* 2) 计算平均衡 */
    for (int k = 0; k < g_cell_hist_cnt[idx]; k++)
    {
        sum += g_cell_hist[idx][k];
    }
    avg = sum / g_cell_hist_cnt[idx];

    /* 3) 去抖: 小变化不更新 */
    if (BQ_AbsInt(avg - bms_data.cell_mv[idx]) <= BQ_CELL_DEBOUNCE_MV)
    {
        return bms_data.cell_mv[idx];
    }

    return avg;
}

/**
 * @brief  重置温度滤波窗口
 * @param  seed_temp: 初始温度值
 */
static void BQ_TempFilterReset(float seed_temp)
{
    g_temp_hist_cnt = BQ_FILTER_WINDOW_SIZE;
    g_temp_hist_widx = 0;

    for (int k = 0; k < BQ_FILTER_WINDOW_SIZE; k++)
    {
        g_temp_hist[k] = seed_temp;
    }
}

/**
 * @brief  温度滑动平均 + 去抖
 * @param  sample_temp: 本轮有效温度值°C)
 * @retval 过滤后的温度值°C)
 */
static float BQ_TempFilterUpdate(float sample_temp)
{
    float sum = 0.0f;
    float avg;

    /* 1) 写入环形缓冲 */
    g_temp_hist[g_temp_hist_widx] = sample_temp;
    g_temp_hist_widx++;
    if (g_temp_hist_widx >= BQ_FILTER_WINDOW_SIZE)
    {
        g_temp_hist_widx = 0;
    }

    if (g_temp_hist_cnt < BQ_FILTER_WINDOW_SIZE)
    {
        g_temp_hist_cnt++;
    }

    /* 2) 计算平均衡 */
    for (int k = 0; k < g_temp_hist_cnt; k++)
    {
        sum += g_temp_hist[k];
    }
    avg = sum / (float)g_temp_hist_cnt;

    /* 3) 去抖 */
    if (BQ_AbsFloat(avg - bms_data.temp_degC) <= BQ_TEMP_DEBOUNCE_C)
    {
        return bms_data.temp_degC;
    }

    return avg;
}

/**
 * @brief  应用单体电压过滤
 * @param  idx: 电芯索引(0~CELL_COUNT-1)
 * @param  sample_mv: 本轮原始采样mV)
 * @note   过滤策略:
 *         1) 有效 覆盖 cell_mv[idx], 并标记valid=1
 *         2) 无效 不覆盖cell_mv[idx] (保留上次有效, 标记valid=0
 *         3) 若第一次上电尚无有效历史默认0), 且本轮无
 *            则给一个安全占位值600mV, 防止保护逻辑mV误触发
 */
static uint8_t s_jump_cnt[CELL_COUNT] = {0};  /* 跳变连续计数 */

static void BQ_FilterAndUpdateCell(uint8_t idx, int sample_mv)
{
    bms_data.cell_raw_mv[idx] = sample_mv; /* 先记录原始值 便于调试 */

    if (BQ_IsCellVoltageValid(sample_mv))
    {
        /* 跳变限幅: 已有有效历史非初600占位)
         * 单次偏差>30mV计为1次跳 连续<3次则丢弃.
         * 连续>=3次说明是真实变化, 接受并重置滤波窗口*/
        if (bms_data.cell_valid[idx] && bms_data.cell_mv[idx] > 0
            && bms_data.cell_mv[idx] != 3600)
        {
            int delta = BQ_AbsInt(sample_mv - bms_data.cell_mv[idx]);
            if (delta > 30)
            {
                s_jump_cnt[idx]++;
                if (s_jump_cnt[idx] < 3)
                {
                    /* 跳变次数不够, 丢弃 */
                    return;
                }
                /* 连续3次跳 认为是真实变化 重置滤波窗口 */
                BQ_CellFilterReset(idx, sample_mv);
                s_jump_cnt[idx] = 0;
            }
            else
            {
                s_jump_cnt[idx] = 0;  /* 正常采样, 清零计数 */
            }
        }

        /* 有效采样: 先做滑动平均和去 再更新最终输*/
        int filtered_mv = BQ_CellFilterUpdate(idx, sample_mv);
        bms_data.cell_mv[idx] = filtered_mv;
        bms_data.cell_valid[idx] = 1;
        return;
    }

    /* 越界无效: 计数+置无效标志*/
    bms_data.cell_valid[idx] = 0;
    if (bms_data.cell_invalid_cnt[idx] < 0xFFFFU)
    {
        bms_data.cell_invalid_cnt[idx]++;
    }

    /* 首轮无历史有效时, 给一个中间占位值避免误保护 */
    if (bms_data.cell_mv[idx] == 0)
    {
        bms_data.cell_mv[idx] = 3600;
    }
}

/**
 * @brief  读取15节电池电压并计算总电压
 * @note   对应原代码Get_Battery1()~Get_Battery15() + Get_Update_ALL_Data()
 *         寄存器地址 (从原代码逐函数提取验:
 *           Cell1=0x0C  Cell2=0x0E  Cell3=0x10
 *           Cell4=0x12  Cell5=0x14  Cell6=0x16
 *           Cell7=0x18  Cell8=0x1A  Cell9=0x1C
 *           Cell10=0x1E Cell11=0x20 Cell12=0x22
 *           Cell13=0x24 Cell14=0x26 Cell15=0x28
 *         公式: HI地址 = 0x0C + (n-1)*2
 *         电压: mV = (raw * adc_gain) / 1000 + adc_offset
 */
void BQ76940_ReadAllCells(void)
{
    uint8_t hi, lo;
    int gain = bms_data.adc_gain;
    int offset = bms_data.adc_offset;
    int pack_sum = 0;

    /* 读电压前暂停均衡, 避免均衡电流导致采样偏低 */
    uint16_t saved_bal = bms_data.balance_mask;
    if (saved_bal) {
        BQ76940_ClearBalance();
        BQ_Delay(5);  /* 等均衡FET关断, ADC稳定 */
    }

    for (int i = 0; i < CELL_COUNT; i++)
    {
        uint8_t reg_hi = REG_VC1_HI + i * 2;
        uint8_t reg_lo = reg_hi + 1;

        /* 与原代码一致 先读HI再读LO */
        hi = BQ_ReadReg(reg_hi);
        lo = BQ_ReadReg(reg_lo);

        /* 原始计算 mV = (raw * gain)/1000 + offset */
        int16_t raw = ((int16_t)hi << 8) | lo;
        int sample_mv = (raw * gain) / 1000 + offset;

        /* 小步: 对单体电压做越界过滤 */
        BQ_FilterAndUpdateCell((uint8_t)i, sample_mv);

        /* 注意:
         * pack_sum使用"过滤后的cell_mv"进行累加,
         * 可避免某一节瞬时异常把总压拉飞 */
        pack_sum += bms_data.cell_mv[i];
    }

    /* 总电压过滤后各节之*/
    bms_data.pack_mv = pack_sum;

    /* 读完恢复均衡 */
    if (saved_bal) {
        BQ76940_SetBalance(saved_bal);
    }
}

/**
 * @brief  判断温度值是否在有效范围
 * @param  temp: 待判断的温度值°C)
 * @retval 1=有效, 0=越界无效
 * @note   BQ_TEMP_VALID_MIN_C / BQ_TEMP_VALID_MAX_C 均为可调
 *         NTC断线时temp接近0°C或负 短路时接近极 均在范围
 */
static uint8_t BQ_IsTempValid(float temp)
{
    if (temp < (float)BQ_TEMP_VALID_MIN_C)
    {
        return 0;
    }
    if (temp > (float)BQ_TEMP_VALID_MAX_C)
    {
        return 0;
    }
    return 1;
}

/**
 * @brief  读取温度 (TS1通道, NTC 10K/B3380)
 * @note   对应原代码Get_BQ1_2_Temp()
 *         原代码计算步骤
 *           1. raw = (HI<<8)|LO
 *           2. vtsx = raw*382/1000           (ADC转电压mV)
 *           3. Rt = 10000*vtsx/(3300-vtsx)   (分压算NTC阻值
 *           4. T = 1/(1/T2+ln(Rt/Rp)/B)-273.15  (B值法算温
 *         参数: T2=298.15K, Rp=10000, B=3380
 */
void BQ76940_ReadTemp(void)
{
    uint8_t hi, lo;

    hi = BQ_ReadReg(REG_TS1_HI); /* 0x2C */
    lo = BQ_ReadReg(REG_TS1_LO); /* 0x2D */

    int raw = ((int)hi << 8) | lo;

    /* ADC转电压(对应原代码 vtsx = raw * 382 / 1000) */
    int vtsx_mv = raw * 382 / 1000;
    if (vtsx_mv >= 3300)
    {
        vtsx_mv = 3299; /* 防除零, NTC短路时Vtsx接近3300, 截断处理 */
    }

    /* 分压公式算NTC阻值*/
    float Rt = 10000.0f * vtsx_mv / (3300 - vtsx_mv);

    /* B值法计算温度 (对应原代码参 T2=25°C+273.15, B=3380, Rp=10K) */
    float T2 = 273.15f + 25.0f;
    float Bx = 3380.0f;
    float Rp = 10000.0f;
    float Ka = 273.15f;

    float sample_temp;
    if (Rt > 0.0f)
    {
        sample_temp = 1.0f / (1.0f / T2 + logf(Rt / Rp) / Bx) - Ka + 0.5f;
    }
    else
    {
        sample_temp = -999.0f; /* NTC阻值异常, 先给无效占位*/
    }

    /* 记录原始值供调试观察 */
    bms_data.temp_raw_degC = sample_temp;

    /* 小步: 越界过滤 */
    if (BQ_IsTempValid(sample_temp))
    {
        /* 有效采样: 先滑动平均去抖, 再更新输*/
        float filtered_temp = BQ_TempFilterUpdate(sample_temp);
        bms_data.temp_degC = filtered_temp;
        bms_data.temp_valid = 1;
    }
    else
    {
        bms_data.temp_valid = 0; /* 无效, 不覆盖上一轮有效*/
        if (bms_data.temp_invalid_cnt < 0xFFFFU)
        {
            bms_data.temp_invalid_cnt++;
        }
        /* 上电首轮无历史有效时给安全占位值*/
        if (bms_data.temp_degC == 0.0f)
        {
            bms_data.temp_degC = 25.0f; /* 25°C中间隔 保证不误触发OT保护 */
        }
    }
}

/**
 * @brief  读取电流 (库仑计
 * @note   对应原代码Get_BQ_Current()
 *         raw<=0x7D00: 放电, I = raw * 2.11 mA
 *         raw> 0x7D00: 充电, I = -(0xFFFF-raw) * 2.11 mA
 *         2.11 = 8.44uV/LSB / 4mR采样电阻
 *         如果采样电阻不是4mR, 改系数 8.44/Rsense(mR)
 *
 *         优化: 1) 检查CC_READY确保读到新转换结果
 *               2) 4点移动平均滤波
 *               3) ±3mA死区消除零漂
 */
#define CURR_FILTER_LEN  4
static int s_curr_buf[CURR_FILTER_LEN] = {0};
static uint8_t s_curr_idx = 0;
static uint8_t s_curr_filled = 0;

void BQ76940_ReadCurrent(void)
{
    uint8_t hi, lo;

    /* 检查CC_READYSYS_STAT bit7), 未就绪则跳过本次(保留上次值 */
    uint8_t sys_stat = BQ_ReadReg(REG_SYS_STAT);
    if (!(sys_stat & 0x80))
    {
        return;  /* CC转换未完成 保留上次值*/
    }
    /* 清CC_READY标志 */
    BQ_WriteReg(REG_SYS_STAT, 0x80);

    hi = BQ_ReadReg(REG_CC_HI); /* 0x32 */
    lo = BQ_ReadReg(REG_CC_LO); /* 0x33 */

    uint16_t raw = ((uint16_t)hi << 8) | lo;
    int sample;

    if (raw <= 0x7D00)
    {
        sample = (int)(raw * 2.11f);
    }
    else
    {
        sample = -(int)((0xFFFF - raw) * 2.11f);
    }

    /* 移动平均滤波 */
    s_curr_buf[s_curr_idx] = sample;
    s_curr_idx = (s_curr_idx + 1) % CURR_FILTER_LEN;
    if (!s_curr_filled && s_curr_idx == 0)
        s_curr_filled = 1;

    int sum = 0;
    int n = s_curr_filled ? CURR_FILTER_LEN : s_curr_idx;
    if (n == 0) n = 1;
    for (int i = 0; i < n; i++)
        sum += s_curr_buf[i];
    bms_data.current_mA = sum / n;

    /* ±3mA 死区: 消除零漂 */
    if (bms_data.current_mA >= -3 && bms_data.current_mA <= 3)
    {
        bms_data.current_mA = 0;
    }
}

/* ==================== SOC估算(电压查表 ====================
 * 说明:
 *   - 15串压阈值= 单串阈值* 15
 *   - 表项按电压从高到低排 命中第一个阈值即返回对应SOC
 *
 * 注意:
 *   该方法与负载、电芯老化、温度相关性较大
 *   仅作为第一版可用方案。
 * ============================================================ */

typedef struct
{
    int pack_mv_threshold; /* 总压阈值mV), 大于等于该则命中 */
    uint8_t soc;           /* 对应SOC(%) */
} BQ_SocPoint_t;

/*
 * 兼容原BSP区间风格的映射表(做了轻微整理):
 *  4150*15 -> 100
 *  4100*15 -> 95
 *  4050*15 -> 90
 *  4000*15 -> 88
 *  3950*15 -> 87
 *  3900*15 -> 86
 *  3850*15 -> 84
 *  3800*15 -> 83
 *  3750*15 -> 82
 *  3700*15 -> 81
 *  3650*15 -> 80
 *  3600*15 -> 79
 *  3550*15 -> 78
 *  3500*15 -> 77
 *  3450*15 -> 40
 *  3400*15 -> 30
 *  3300*15 -> 20
 *  3200*15 -> 10
 *  3100*15 -> 5
 */
static const BQ_SocPoint_t g_soc_table[] = {
    {4150 * CELL_COUNT, 100},
    {4100 * CELL_COUNT, 95},
    {4050 * CELL_COUNT, 90},
    {4000 * CELL_COUNT, 88},
    {3950 * CELL_COUNT, 87},
    {3900 * CELL_COUNT, 86},
    {3850 * CELL_COUNT, 84},
    {3800 * CELL_COUNT, 83},
    {3750 * CELL_COUNT, 82},
    {3700 * CELL_COUNT, 81},
    {3650 * CELL_COUNT, 80},
    {3600 * CELL_COUNT, 79},
    {3550 * CELL_COUNT, 78},
    {3500 * CELL_COUNT, 77},
    {3450 * CELL_COUNT, 40},
    {3400 * CELL_COUNT, 30},
    {3300 * CELL_COUNT, 20},
    {3200 * CELL_COUNT, 10},
    {3100 * CELL_COUNT, 5}};

void BQ76940_UpdateSOC(void)
{
    int pack = bms_data.pack_mv;

    /* 默认最低SOC=0, 如果命中阈值会被覆盖*/
    bms_data.soc_percent = 0;

    for (uint32_t i = 0; i < (sizeof(g_soc_table) / sizeof(g_soc_table[0])); i++)
    {
        if (pack >= g_soc_table[i].pack_mv_threshold)
        {
            bms_data.soc_percent = g_soc_table[i].soc;
            return;
        }
    }
}

/* ==================== 均衡控制 ====================
 * 原BSP: Battery1_Balance() ... Battery10_Balance()
 *        只写个单 全部标记为UNUSED(从未被调
 * 本实 覆盖5个单 支持bitmask多路控制
 *
 * CELLBAL寄存器地址:
 *   REG_CELLBAL1 (0x01): bit[4:0] = 单体 1~5
 *   REG_CELLBAL2 (0x02): bit[4:0] = 单体 6~10
 *   REG_CELLBAL3 (0x03): bit[4:0] = 单体 11~15
 * =================================================== */

void BQ76940_SetBalance(uint16_t cell_mask)
{
    uint8_t cb1, cb2, cb3;

    /* 16-bit mask 拆分到三个寄存器的低5*/
    cb1 = (uint8_t)( cell_mask        & 0x1F);   /* bit[4:0] 单体1~5   */
    cb2 = (uint8_t)((cell_mask >> 5)  & 0x1F);   /* bit[9:5] 单体6~10  */
    cb3 = (uint8_t)((cell_mask >> 10) & 0x1F);   /* bit[14:10] 单体11~15 */

    BQ_WriteReg(REG_CELLBAL1, cb1);
    BQ_WriteReg(REG_CELLBAL2, cb2);
    BQ_WriteReg(REG_CELLBAL3, cb3);

    /* 同步到数据结果*/
    bms_data.balance_mask = cell_mask & 0x7FFF;
}

void BQ76940_ClearBalance(void)
{
    BQ76940_SetBalance(0x0000);
}

uint16_t BQ76940_GetBalanceMask(void)
{
    uint8_t cb1, cb2, cb3;
    uint16_t mask;

    cb1 = BQ_ReadReg(REG_CELLBAL1) & 0x1F;
    cb2 = BQ_ReadReg(REG_CELLBAL2) & 0x1F;
    cb3 = BQ_ReadReg(REG_CELLBAL3) & 0x1F;

    mask = (uint16_t)cb1
         | ((uint16_t)cb2 << 5)
         | ((uint16_t)cb3 << 10);

    bms_data.balance_mask = mask;
    return mask;
}

void BQ76940_UpdateAll(void)
{
    BQ76940_ReadAllCells();
    BQ76940_ReadTemp();
    BQ76940_ReadCurrent();

    /* 计算已连接电芯平均电压(>1000mV的视为有效) */
    int sum = 0, cnt = 0;
    for (int i = 0; i < CELL_COUNT; i++) {
        if (bms_data.cell_mv[i] > 1000) {
            sum += bms_data.cell_mv[i];
            cnt++;
        }
    }
    bms_data.connected_cells = (uint8_t)cnt;
    bms_data.avg_cell_mv = (cnt > 0) ? (sum / cnt) : 3600;

    /* 注意: SOC_Update() 由 main.c BMS_Task 统一调用(使用实际dt_ms),
     * 此处不再调用, 避免每周期重复积分导致SOC变化速度翻倍 */

    BQ_ReadMosStatus();
}

void BQ76940_PrintData(UART_HandleTypeDef *huart)
{
    (void)huart;  /* 改用UART_DMA_Send(), 不再直接使用huart */
    char buf[64];
    int len;

    len = snprintf(buf, sizeof(buf), "\r\n===== BMS Data =====\r\n");
    UART_DMA_Send((uint8_t *)buf, len);

    for (int i = 0; i < CELL_COUNT; i++)
    {
        len = snprintf(buf, sizeof(buf),
                       "Cell%02d: %d mV (raw=%d, ok=%d, bad=%u)\r\n",
                       i + 1,
                       bms_data.cell_mv[i],
                       bms_data.cell_raw_mv[i],
                       bms_data.cell_valid[i],
                       bms_data.cell_invalid_cnt[i]);
        UART_DMA_Send((uint8_t *)buf, len);
    }

    len = snprintf(buf, sizeof(buf), "Pack:   %d mV\r\n", bms_data.pack_mv);
    UART_DMA_Send((uint8_t *)buf, len);

    len = snprintf(buf, sizeof(buf), "SOC:    %u %%\r\n", bms_data.soc_percent);
    UART_DMA_Send((uint8_t *)buf, len);

    {
        SOC_State_t *ss = SOC_GetState();
        len = snprintf(buf, sizeof(buf), "SOH:    %u %%\r\n", ss->soh_percent);
        UART_DMA_Send((uint8_t *)buf, len);
        len = snprintf(buf, sizeof(buf), "Cap:    %.0f/%.0f mAh\r\n",
                       ss->soc_mAh, ss->full_cap_mAh);
        UART_DMA_Send((uint8_t *)buf, len);
        len = snprintf(buf, sizeof(buf), "Cycle:  %u.%u\r\n",
                       ss->cycle_count_x10 / 10, ss->cycle_count_x10 % 10);
        UART_DMA_Send((uint8_t *)buf, len);
    }

    /* 打印均衡状态 显示哪些单体正在均衡 */
    len = snprintf(buf, sizeof(buf), "Bal:    0x%04X", bms_data.balance_mask);
    UART_DMA_Send((uint8_t *)buf, len);
    if (bms_data.balance_mask == 0) {
        len = snprintf(buf, sizeof(buf), " (none)\r\n");
    } else {
        /* 逐位列出正在均衡的单体编号*/
        char cell_list[48] = " (";
        uint8_t first = 1;
        for (int ci = 0; ci < 15; ci++) {
            if (bms_data.balance_mask & (1u << ci)) {
                char tmp[6];
                if (!first) { strncat(cell_list, ",", sizeof(cell_list) - strlen(cell_list) - 1); }
                snprintf(tmp, sizeof(tmp), "%d", ci + 1);
                strncat(cell_list, tmp, sizeof(cell_list) - strlen(cell_list) - 1);
                first = 0;
            }
        }
        strncat(cell_list, ")\r\n", sizeof(cell_list) - strlen(cell_list) - 1);
        len = snprintf(buf, sizeof(buf), "%s", cell_list);
    }
    UART_DMA_Send((uint8_t *)buf, len);

    len = snprintf(buf, sizeof(buf),
                   "Temp:   %.1f C (raw=%.1f, ok=%d, bad=%u)\r\n",
                   bms_data.temp_degC,
                   bms_data.temp_raw_degC,
                   bms_data.temp_valid,
                   bms_data.temp_invalid_cnt);
    UART_DMA_Send((uint8_t *)buf, len);

    len = snprintf(buf, sizeof(buf), "Curr:   %d mA\r\n", bms_data.current_mA);
    UART_DMA_Send((uint8_t *)buf, len);

    len = snprintf(buf, sizeof(buf), "CHG: %s  DSG: %s\r\n",
                   bms_data.chg_on ? "ON" : "OFF",
                   bms_data.dsg_on ? "ON" : "OFF");
    UART_DMA_Send((uint8_t *)buf, len);

    /* 保护状态打*/
    len = snprintf(buf, sizeof(buf), "Prot: OV=%d UV=%d OC=%d OT=%d LT=%d\r\n",
                   g_prot.ov_flag, g_prot.uv_flag,
                   g_prot.oc_flag, g_prot.ot_flag, g_prot.lt_flag);
    UART_DMA_Send((uint8_t *)buf, len);

    len = snprintf(buf, sizeof(buf), "HW:   OV=%d UV=%d SCD=%d OCD=%d\r\n",
                   g_prot.hw_ov, g_prot.hw_uv,
                   g_prot.hw_scd, g_prot.hw_ocd);
    UART_DMA_Send((uint8_t *)buf, len);

    len = snprintf(buf, sizeof(buf), "GAIN:   %d uV, Offset: %d mV\r\n", bms_data.adc_gain, bms_data.adc_offset);
    UART_DMA_Send((uint8_t *)buf, len);

    len = snprintf(buf, sizeof(buf),
                   "Filter: win=%d cell_db=%dmV temp_db=%.1fC\r\n",
                   BQ_FILTER_WINDOW_SIZE,
                   BQ_CELL_DEBOUNCE_MV,
                   BQ_TEMP_DEBOUNCE_C);
    UART_DMA_Send((uint8_t *)buf, len);

    len = snprintf(buf, sizeof(buf), "========================\r\n");
    UART_DMA_Send((uint8_t *)buf, len);
}
