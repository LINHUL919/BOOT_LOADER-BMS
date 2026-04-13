#ifndef __BQ76940_H
#define __BQ76940_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/**
 * @file    bq76940.h
 * @brief   BQ76940电池管理芯片驱动 (HAL库版本)
 * @note    移植自原工程 BSP/BQ76930.h + BSP/BQ76930.c
 *          原工程文件名叫BQ76930, 但实际驱动的是BQ76940 (支持15串)
 *
 *          数据流:  BQ76940 --(软件I2C)--> STM32读取 --(USART)--> 串口打印
 *
 *          I2C通信协议:
 *            从机地址: 0x08 (7bit), 写地址=0x10, 读地址=0x11
 *            写操作: START -> 0x10 -> reg -> data -> CRC8 -> STOP (必须带CRC)
 *            读操作: START -> 0x10 -> reg -> RESTART -> 0x11 -> 读数据 -> STOP (不带CRC)
 *            CRC8多项式: x^8 + x^2 + x + 1 (key=7)
 */

/* ================================================================
 *  BQ76940 I2C 地址定义
 *  原代码中:
 *    写: I2C1_SendByte(0x08 << 1)  即 0x10
 *    读: I2C1_SendByte(0X11)       即 0x11
 * ================================================================ */
#define BQ76940_I2C_ADDR_7BIT   0x08                                    /* 7位从机地址 */
#define BQ76940_WRITE_ADDR      (BQ76940_I2C_ADDR_7BIT << 1)           /* 写地址 = 0x10 */
#define BQ76940_READ_ADDR       ((BQ76940_I2C_ADDR_7BIT << 1) | 1)     /* 读地址 = 0x11 */

/* ================================================================
 *  寄存器地址定义 (与原代码 BQ76930.h 完全一致)
 * ================================================================ */

/* ----- 系统与控制寄存器 ----- */
#define REG_SYS_STAT    0x00    /* 系统状态寄存器
                                 * bit[3]=UV  欠压标志
                                 * bit[2]=OV  过压标志
                                 * bit[1]=SCD 短路标志
                                 * bit[0]=OCD 过流标志
                                 * 写0xFF可清除所有标志 */
#define REG_CELLBAL1    0x01    /* 均衡控制1, 控制 Cell 1~5 的均衡开关 */
#define REG_CELLBAL2    0x02    /* 均衡控制2, 控制 Cell 6~10 的均衡开关 */
#define REG_CELLBAL3    0x03    /* 均衡控制3, 控制 Cell 11~15 的均衡开关 (BQ76940特有) */
#define REG_SYS_CTRL1   0x04   /* 系统控制1
                                 * bit[4]=ADC_EN   ADC使能
                                 * bit[3]=TEMP_SEL 温度通道选择
                                 * 原代码初始化值=0x18 (ADC_EN=1, TEMP_SEL=1) */
#define REG_SYS_CTRL2   0x05   /* 系统控制2
                                 * bit[6]=CC_EN  库仑计使能
                                 * bit[1]=DSG_ON 放电MOS使能
                                 * bit[0]=CHG_ON 充电MOS使能
                                 * 原代码初始化值=0x43 (CC_EN=1, DSG_ON=1, CHG_ON=1) */
                                
/* 均衡控制寄存器 (对应原BSP CELLBAL1/2/3) */
#define REG_CELLBAL1    0x01    /* bit[4:0]: 单体1~5 */
#define REG_CELLBAL2    0x02    /* bit[4:0]: 单体6~10 */
#define REG_CELLBAL3    0x03    /* bit[4:0]: 单体11~15 */

/* ----- 保护寄存器 ----- */
#define REG_PROTECT1     0x06   /* 保护1: SCD(短路)阈值和延时
                                 * 原代码设为0xFF */
#define REG_PROTECT2     0x07   /* 保护2: OCD(过流)阈值和延时
                                 * 原代码设为0xFF */
#define REG_PROTECT3     0x08   /* 保护3: UV/OV延时配置 */
#define REG_OV_TRIP      0x09   /* 过压阈值寄存器
                                 * 计算公式: OVTrip = ((OVPThreshold - offset) / 0.377 + 0.5) >> 4
                                 * 原代码 OVPThreshold = 4400mV */
#define REG_UV_TRIP      0x0A   /* 欠压阈值寄存器
                                 * 计算公式: UVTrip = ((UVPThreshold - offset) / 0.377 + 0.5) >> 4
                                 * 原代码 UVPThreshold = 2400mV */
#define REG_CC_CFG       0x0B   /* 库仑计配置, 原代码初始化值=0x19 */

/* ----- 单节电池电压寄存器 -----
 * 每节电池占2个字节: HI(高字节), LO(低字节)
 * 14位ADC值, 计算公式: voltage_mV = (raw * GAIN) / 1000 + ADC_offset
 *
 * 寄存器地址映射 (从原代码 Get_Battery1()~Get_Battery15() 提取):
 *   VC1:  0x0C/0x0D     VC2:  0x0E/0x0F     VC3:  0x10/0x11
 *   VC4:  0x12/0x13     VC5:  0x14/0x15     VC6:  0x16/0x17
 *   VC7:  0x18/0x19     VC8:  0x1A/0x1B     VC9:  0x1C/0x1D
 *   VC10: 0x1E/0x1F     VC11: 0x20/0x21     VC12: 0x22/0x23
 *   VC13: 0x24/0x25     VC14: 0x26/0x27     VC15: 0x28/0x29
 *
 * 规律: VC[n] 的 HI地址 = 0x0C + (n-1)*2
 */
#define REG_VC1_HI      0x0C   /* Cell 1 电压高字节 (所有cell的起始地址) */
#define REG_VC1_LO      0x0D   /* Cell 1 电压低字节 */

/* ----- 总电压寄存器 ----- */
#define REG_BAT_HI      0x2A   /* 总电压高字节 */
#define REG_BAT_LO      0x2B   /* 总电压低字节 */

/* ----- 温度寄存器 -----
 * BQ76940有2个温度通道 TS1/TS2, 连接NTC热敏电阻
 * 原代码使用TS1通道, NTC参数: 10KΩ, B=3380
 */
#define REG_TS1_HI      0x2C   /* 温度通道1 高字节 */
#define REG_TS1_LO      0x2D   /* 温度通道1 低字节 */
#define REG_TS2_HI      0x2E   /* 温度通道2 高字节 */
#define REG_TS2_LO      0x2F   /* 温度通道2 低字节 */

/* ----- 电流寄存器 (库仑计) -----
 * 16位有符号值, 通过采样电阻上的压降测量电流
 * 原代码计算: current_mA = raw * 2.11
 * 其中 2.11 = 8.44μV/LSB ÷ 4mΩ采样电阻 (根据实际电阻调整)
 */
#define REG_CC_HI       0x32   /* 库仑计高字节 */
#define REG_CC_LO       0x33   /* 库仑计低字节 */

/* ----- ADC校准寄存器 -----
 * GAIN和OFFSET是出厂校准值, 每颗芯片不同, 必须读取后用于电压计算
 * 原代码:
 *   ADC_GAIN = ((GAIN1 & 0x0C) << 1) + ((GAIN2 & 0xE0) >> 5)  --> 5bit值
 *   GAIN = 365 + ADC_GAIN   --> 单位μV, 典型值约377
 *   ADC_offset = OFFSET寄存器 --> 有符号, 单位mV
 */
#define REG_ADCGAIN1    0x50   /* ADC增益寄存器1, bit[3:2]有效 */
#define REG_ADCOFFSET   0x51   /* ADC偏移寄存器, 有符号8位 */
#define REG_ADCGAIN2    0x59   /* ADC增益寄存器2, bit[7:5]有效 */

/* ================================================================
 *  唤醒引脚定义
 *  原代码: IO_CTRL.h 中
 *    #define MCU_WAKE_BQ  GPIO_Pin_8     (PA8)
 *    #define MCU_WAKE_BQ_ONOFF(x)  GPIO_WriteBit(GPIOA, MCU_WAKE_BQ, x)
 *
 *  唤醒流程: PA8拉高 -> 延时100ms -> PA8拉低
 *  BQ76940上电后处于SHIP低功耗模式, 必须通过此脉冲唤醒才能I2C通信
 *  如果你的唤醒引脚不是PA8, 修改这里即可
 * ================================================================ */
#define BQ_WAKE_PORT    GPIOA
#define BQ_WAKE_PIN     GPIO_PIN_8

/* ================================================================
 *  电池配置
 * ================================================================ */
#define CELL_COUNT      15      /* 15串电池 (BQ76940最大支持15串) */

/* ================================================================
 *  BMS数据结构 - 存储BQ76940采集到的所有电池信息
 *
 *  对应原代码中的全局变量:
 *    int Batteryval[50]    -> cell_mv[] + pack_mv
 *    float Tempval_2       -> temp_degC
 *    float Currentval      -> current_mA
 *    int GAIN              -> adc_gain
 *    int ADC_offset        -> adc_offset
 * ================================================================ */
typedef struct {
    int      cell_mv[CELL_COUNT];   /* 15节电池电压, 单位mV
                                     * 对应原代码 Batteryval[0]~Batteryval[14] */

    int      cell_raw_mv[CELL_COUNT];  /* 本轮原始计算值(过滤前), 单位mV
                                        * 仅用于调试观察, 不直接参与保护逻辑 */

    uint8_t  cell_valid[CELL_COUNT];   /* 本轮电芯数据是否有效: 1=有效 0=无效
                                        * 无效通常表示越界(通信毛刺/寄存器异常) */

    uint16_t cell_invalid_cnt[CELL_COUNT];
                                       /* 每节电芯累计无效次数(上电以来)
                                        * 用于观察某一路是否持续不稳定 */

    float    temp_raw_degC;         /* 本轮原始计算温度(过滤前), 单位°C
                                     * 仅用于调试观察 */
    uint8_t  temp_valid;            /* 本轮温度数据是否有效: 1=有效 0=无效 */
    uint16_t temp_invalid_cnt;      /* 温度累计无效次数(上电以来) */
                                     
    int      pack_mv;               /* 总电压 (15节之和), 单位mV
                                     * 对应原代码 Batteryval[15] */
    uint8_t  soc_percent;           /* SOC百分比(0~100)
                                     * 对应原BSP Batteryval[16] */

    float    temp_degC;             /* 温度, 单位°C
                                     * 对应原代码 Tempval_2 (NTC 10K B3380计算) */
    int      current_mA;            /* 电流, 单位mA
                                     * 对应原代码 Batteryval[17]
                                     * 正值=放电, 负值=充电 */
    int      adc_gain;              /* ADC增益, 单位μV
                                     * 对应原代码 GAIN, 典型值约377 */
    int8_t   adc_offset;            /* ADC偏移, 单位mV, 有符号
                                     * 对应原代码 ADC_offset */
    
    uint8_t  chg_on;                /* 充电MOS状态: 1=开启, 0=关闭
                                     * 对应原代码 CHG_STA */
    uint8_t  dsg_on;                /* 放电MOS状态: 1=开启, 0=关闭
                                     * 对应原代码 DSG_STA */
    
    uint16_t balance_mask;          /* 当前均衡位图
                                     * bit0=单体1 ... bit14=单体15
                                     * 0=不均衡, 1=正在均衡
                                     * 对应原BSP未完成的均衡状态 */

    int      avg_cell_mv;            /* 已连接电芯平均电压 mV
                                     * 由UpdateAll()动态计算(>1000mV视为有效)
                                     * 替代 pack_mv/SOC_CONNECTED_CELLS 硬编码 */
    uint8_t  connected_cells;        /* 已连接电芯数(>1000mV)
                                     * 由UpdateAll()动态统计 */

} BQ76940_Data_t;

/* ================================================================
 *  函数声明
 * ================================================================ */

/**
 * @brief  初始化BQ76940
 * @note   对应原代码 void BQ76930_config(void) 的完整流程:
 *           第1步: WAKE_ALL_DEVICE()    唤醒芯片(PA8脉冲)
 *           第2步: BQ_1_config()        12个寄存器初始化写入
 *           第3步: Get_offset()         读ADC校准参数(GAIN和OFFSET)
 *           第4步: OV_UV_1_PROTECT()    计算并设置过压/欠压阈值
 *           第5步: OCD_SCD_PROTECT()    设置过流/短路保护
 *           第6步: 写SYS_STAT=0xFF      清除所有状态标志
 */
void BQ76940_Init(void);

/**
 * @brief  读取15节电池电压并计算总电压
 * @note   对应原代码 Get_Battery1()~Get_Battery15() + Get_Update_ALL_Data()
 *         原代码用15个独立函数分别读取每节电压, 这里用一个循环替代
 *         电压计算公式: V(mV) = (raw_adc * adc_gain) / 1000 + adc_offset
 */
void BQ76940_ReadAllCells(void);

/**
 * @brief  读取温度 (TS1通道, NTC 10K/B3380)
 * @note   对应原代码 void Get_BQ1_2_Temp(void)
 *         使用B值法计算NTC温度:
 *           Rt = 10000 * Vtsx / (3300 - Vtsx)
 *           T = 1 / (1/T2 + ln(Rt/Rp)/B) - 273.15
 */
void BQ76940_ReadTemp(void);

/**
 * @brief  读取电流 (库仑计CC寄存器)
 * @note   对应原代码 void Get_BQ_Current(void)
 *         raw <= 0x7D00 时为正向电流: I = raw * 2.11 mA
 *         raw > 0x7D00 时为反向电流: I = -(0xFFFF - raw) * 2.11 mA
 *         2.11的来源: 8.44μV/LSB ÷ 4mΩ采样电阻
 */
void BQ76940_ReadCurrent(void);

/**
 * @brief  一次性采集全部数据 (电压+温度+电流)
 * @note   对应原代码 void Get_Update_Data(void) 的简化版
 *         原代码还包含SOC估算、BMS状态读取、上传数据打包等,
 *         这里只保留最基础的数据采集, 后续可逐步添加
 */
void BQ76940_UpdateAll(void);

/**
 * @brief  根据总压估算SOC(电压表法)
 * @note   对应原BSP Get_SOC() 的迁移版本
 *         输入使用 bms_data.pack_mv (过滤后的总压)
 *         输出写入 bms_data.soc_percent
 */
void BQ76940_UpdateSOC(void);

/**
 * @brief  按位图设置均衡通道
 * @param  cell_mask  bit0=单体1 ... bit14=单体15
 *                    0x0000=全关, 0x7FFF=全部15个通道均衡
 * @note   对应原BSP Battery1_Balance()等函数的完整版本
 *         原BSP函数每次只设一个通道且相互覆盖, 本函数支持多路同时设置
 * @note   安全警告: 请勿同时使能相邻两个单体, 建议奇偶交替
 */
void BQ76940_SetBalance(uint16_t cell_mask);

/**
 * @brief  关闭所有均衡通道
 * @note   等效于 BQ76940_SetBalance(0x0000)
 *         初始化和保护触发时应调用此函数
 */
void BQ76940_ClearBalance(void);

/**
 * @brief  读取当前均衡状态位图 (从寄存器读回)
 * @retval bit0=单体1 ... bit14=单体15
 */
uint16_t BQ76940_GetBalanceMask(void);

/**
 * @brief  通过串口打印所有电池数据 (可读文本格式)
 * @param  huart: HAL库UART句柄指针, 例如 &huart1
 * @note   使用 HAL_UART_Transmit() 发送格式化字符串
 *         输出内容: 15节电压、总电压、温度、电流、ADC校准参数
 */
void BQ76940_PrintData(UART_HandleTypeDef *huart);

/* 连续ACK失败达到此阈值后, 自动重新初始化BQ76940 */
#define BQ_COMM_FAIL_THRESHOLD   5

/* 两次自动重初始化之间的最小间隔(ms), 防止反复重启 */
#define BQ_REINIT_MIN_INTERVAL   3000

/* ==================== 保护阈值配置 ====================
 * 对应原代码 main.c 中的硬编码阈值
 * 后续可改为从Flash读取, 目前先用宏定义
 * ====================================================== */
#define BQ_OV_THRESHOLD         4200    /* 过压阈值 mV (原代码4200)
                                         * 任一电芯超过此值 -> 关充电 */
#define BQ_OV_RECOVER           4100    /* 过压恢复阈值 mV (原代码4100)
                                         * 全部电芯低于此值 -> 恢复充电 */
#define BQ_UV_THRESHOLD         2800    /* 欠压阈值 mV (原代码2800)
                                         * 任一电芯低于此值 -> 关放电 */
#define BQ_UV_RECOVER           3000    /* 欠压恢复阈值 mV (加200mV回差防抖)
                                         * 全部电芯高于此值 -> 恢复放电 */
#define BQ_OC_THRESHOLD         5000    /* 过流阈值 mA (原代码5000)
                                         * 电流超过此值 -> 关全部MOS */
#define BQ_OC_RECOVER           4500    /* 过流恢复阈值 mA (加500mA回差防抖) */
#define BQ_OT_THRESHOLD         60      /* 过温阈值 °C (原代码从Flash读, 默认60)
                                         * 温度超过此值 -> 关全部MOS */
#define BQ_OT_RECOVER           55      /* 过温恢复阈值 °C (加5°C回差防抖) */
#define BQ_LT_THRESHOLD         0       /* 低温充电阈值 °C
                                         * 温度低于此值 -> 禁止充电(关CHG MOS)
                                         * 锂电池0°C以下充电会析锂, 损害电池 */
#define BQ_LT_RECOVER           5       /* 低温充电恢复阈值 °C (加5°C回差防抖) */

/* ==================== 数据有效性过滤(电芯电压) ====================
 * 作用:
 *   过滤明显不可能的电芯值, 例如 42mV/59mV 这种通信毛刺或拼帧错误
 *
 * 建议范围:
 *   锂电单体正常工作一般在 2500~4300mV 之间
 *   这里放宽为 2000~4500mV, 避免误杀边界工况
 *
 * 说明:
 *   该过滤仅用于"数据有效性判断"
 *   不改变BQ76940内部保护阈值寄存器设置
 * ================================================================ */
#define BQ_CELL_VALID_MIN_MV    2000
#define BQ_CELL_VALID_MAX_MV    4500

/* ==================== 数据有效性过滤(温度) ====================
 * 有效温度范围: -40°C ~ 120°C
 * 超出此范围认为是传感器异常或通信毛刺, 不更新温度值
 * 范围可根据实际应用场景调整(例如电池不允许高于80°C应用)
 * ============================================================= */
#define BQ_TEMP_VALID_MIN_C    (-40)
#define BQ_TEMP_VALID_MAX_C    120

/* ==================== 数据平滑参数 ====================
 * WINDOW:
 *   滑动窗口长度, 推荐 3~8
 *   值越大越平滑, 但响应越慢
 *
 * DEBOUNCE:
 *   去抖死区, 当变化量小于该值时保持不变
 *   防止末位抖动导致串口数字频繁跳变
 * ===================================================== */
#define BQ_FILTER_WINDOW_SIZE      4      /* 电压/温度统一窗口长度 */
#define BQ_CELL_DEBOUNCE_MV        10     /* 电压去抖死区: 10mV */
#define BQ_TEMP_DEBOUNCE_C         0.3f   /* 温度去抖死区: 0.3°C */


/* ==================== 保护状态结构体 ==================== */
typedef struct {
    uint8_t  ov_flag;       /* 过压保护已触发: 1=是 0=否
                             * 对应原代码 OV_FLAG */
    uint8_t  uv_flag;       /* 欠压保护已触发
                             * 对应原代码 UV_FLAG */
    uint8_t  oc_flag;       /* 过流保护已触发
                             * 对应原代码 OC_FLAG */
    uint8_t  ot_flag;       /* 过温保护已触发
                             * 对应原代码 Temp_up_flag */
    uint8_t  lt_flag;       /* 低温充电保护已触发: 1=是 0=否
                             * 0°C以下禁止充电, 5°C以上恢复 */

    /* 硬件报警标志 (来自SYS_STAT寄存器) */
    uint8_t  hw_ov;         /* 芯片硬件过压报警
                             * 对应原代码 OV_Alarm_flag */
    uint8_t  hw_uv;         /* 芯片硬件欠压报警
                             * 对应原代码 UV_Alarm_flag */
    uint8_t  hw_scd;        /* 芯片硬件短路报警
                             * 对应原代码 SCD_Alarm_flag */
    uint8_t  hw_ocd;        /* 芯片硬件过流报警
                             * 对应原代码 OCD_Alarm_flag */
} BQ76940_Protection_t;

/* ==================== 保护函数声明 ==================== */

/**
 * @brief  执行全部保护检查 (OV/UV/OC/OT) + 自动MOS控制 + 自动恢复
 * @note   对应原代码 main.c 主循环中的4组 if/else 保护判断
 *         在 BQ76940_UpdateAll() 之后调用
 */
void BQ76940_ProtectionCheck(void);

/**
 * @brief  读取BQ76940硬件报警标志 (SYS_STAT寄存器)
 * @note   对应原代码 ALERT_1_Recognition()
 *         读SYS_STAT的UV/OV/SCD/OCD位
 */
void BQ76940_ReadAlerts(void);

/**
 * @brief  获取当前保护状态 (供串口打印或上位机使用)
 * @param  prot: 输出保护状态结构体指针
 */
void BQ76940_ProtectionGet(BQ76940_Protection_t *prot);

/**
 * @brief  清除所有保护标志 (手动复位)
 */
void BQ76940_ProtectionReset(void);

/* 全局BMS数据, 其他文件可通过 extern 访问 */
typedef struct {
    uint32_t ack_fail_total;         /* ACK累计失败次数 (上电以来) */
    uint16_t ack_fail_continuous;    /* ACK连续失败次数 (成功一次就清零) */
    uint8_t  last_transfer_ok;       /* 最近一次通信结果: 1=成功 0=失败 */
    uint16_t reinit_count;           /* 新增: 累计重初始化次数 */
} BQ76940_CommHealth_t;

void BQ76940_CommHealthReset(void);
void BQ76940_CommHealthGet(BQ76940_CommHealth_t *info);

/**
 * @brief  通信健康检查 + 自动恢复
 *         在主循环每次采集前调用, 连续失败>=阈值时自动重初始化
 * @retval 1=通信正常可继续采集, 0=刚执行了重初始化(本轮跳过采集)
 */
uint8_t BQ76940_CommCheck(void);

/* ===================== MOS管控制 =====================
 * 对应原代码 BQ76930.c 中的6个MOS控制函数
 * SYS_CTRL2寄存器: bit[6]=CC_EN bit[1]=DSG bit[0]=CHG
 * ===================================================== */

/**
 * @brief  打开充电MOS, 不影响放电MOS状态
 * @note   对应原代码 Only_Open_CHG()
 */
void BQ76940_OpenCHG(void);

/**
 * @brief  关闭充电MOS, 不影响放电MOS状态
 * @note   对应原代码 Only_Close_CHG()
 */
void BQ76940_CloseCHG(void);

/**
 * @brief  打开放电MOS, 不影响充电MOS状态
 * @note   对应原代码 Only_Open_DSG()
 */
void BQ76940_OpenDSG(void);

/**
 * @brief  关闭放电MOS, 不影响充电MOS状态
 * @note   对应原代码 Only_Close_DSG()
 */
void BQ76940_CloseDSG(void);

/**
 * @brief  同时打开充电+放电MOS
 * @note   对应原代码 Open_DSG_CHG()
 */
void BQ76940_OpenAll(void);

/**
 * @brief  同时关闭充电+放电MOS
 * @note   对应原代码 Close_DSG_CHG()
 */
void BQ76940_CloseAll(void);

extern BQ76940_Data_t bms_data;

/* ==================== ALERT 中断 ====================
 * BQ76940 ALERT 引脚接 PB2 (EXTI2, 下降沿触发)
 * 中断中仅置标志, 主循环中处理
 * ===================================================== */
extern volatile uint8_t g_bq_alert_flag;  /* 1=收到ALERT中断 */

/**
 * @brief  ALERT 中断回调 (由 HAL_GPIO_EXTI_Callback 调用)
 * @note   仅置标志+通知BMS_Task, 不做I2C操作(中断中不能用软件I2C)
 */
void BQ76940_AlertCallback(void);

/**
 * @brief  注册接收ALERT通知的FreeRTOS任务
 * @param  handle  BMS_Task任务句柄 (void*避免头文件耦合)
 * @note   在main.c中xTaskCreate之后、vTaskStartScheduler之前调用
 */
void BQ76940_SetAlertTask(void *handle);

#ifdef __cplusplus
}
#endif

#endif /* __BQ76940_H */
