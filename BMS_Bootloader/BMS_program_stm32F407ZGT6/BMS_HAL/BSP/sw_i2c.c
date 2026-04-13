/**
 * @file    sw_i2c.c
 * @brief   软件模拟I2C驱动实现 (HAL库版本)
 * @note    完全移植自原工程 BSP/i2c1.c
 *          原代码用标准库GPIO操作 (GPIO_SetBits/ResetBits/GPIO_ReadInputDataBit)
 *          这里全部替换为HAL库 (HAL_GPIO_WritePin/HAL_GPIO_ReadPin)
 *          I2C时序逻辑与原代码完全一致，未做任何修改
 *
 *          原工程引脚: PB8(SCL), PB9(SDA), 开漏输出, 50MHz
 *          I2C速率: 由 I2C_Delay() 中的循环次数决定, 约几十KHz
 */

/* Includes ------------------------------------------------------------------*/
#include "sw_i2c.h"

/* Private functions ---------------------------------------------------------*/

/**
 * @brief  I2C时序延时函数
 * @note   对应原代码 static void I2C_delay(void)
 *         原代码: uint8_t i=50; while(i) { i--; }
 *         这个延时决定了I2C时钟速率
 *         循环50次大约对应72MHz主频下的几μs延时
 *         如果通信不稳定, 可以适当增大这个值
 */
static void I2C_Delay(void)
{
    uint8_t i = 50;  /* 原代码就是50, 可根据实际调整 */
    while (i) {
        i--;
    }
}

/* Exported functions --------------------------------------------------------*/

/**
 * @brief  I2C GPIO初始化
 * @note   对应原代码 void I2C1_Configuration(void)
 *
 *         原代码:
 *           RCC_APB2PeriphClockCmd(I2C1_SCL_GPIO_RCC, ENABLE);
 *           GPIO_InitStructure.GPIO_Pin  = I2C1_SCL_PIN;       // PB8
 *           GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
 *           GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;   // 开漏输出
 *           GPIO_Init(I2C1_SCL_GPIO_PORT, &GPIO_InitStructure);
 *           // SDA同理, PB9, 开漏
 *
 *         HAL版本用 GPIO_MODE_OUTPUT_OD 替代 GPIO_Mode_Out_OD
 *         用 __HAL_RCC_GPIOB_CLK_ENABLE() 替代 RCC_APB2PeriphClockCmd
 *
 *         如果你已经在CubeMX中把PB8/PB9配置为开漏输出, 
 *         MX_GPIO_Init() 会自动完成这些配置, 就不需要调用本函数了
 */
void SW_I2C_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 使能GPIOC时钟 (F407移植: PB->PC) */
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /*
     * 配置 PB8 (SCL) 为开漏输出
     * 开漏模式: 输出0时拉低, 输出1时释放(由外部上拉电阻拉高)
     * I2C协议要求SCL/SDA必须是开漏模式 + 外部上拉电阻
     */
    GPIO_InitStruct.Pin   = SW_I2C_SCL_PIN;       /* PB8 */
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;   /* 开漏输出, 对应原代码 GPIO_Mode_Out_OD */
    GPIO_InitStruct.Pull  = GPIO_NOPULL;            /* 不使用内部上拉, 需要外部4.7K上拉电阻 */
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;   /* 50MHz, 对应原代码 GPIO_Speed_50MHz */
    HAL_GPIO_Init(SW_I2C_SCL_PORT, &GPIO_InitStruct);

    /*
     * 配置 PB9 (SDA) 为开漏输出
     * 与SCL完全相同的配置
     */
    GPIO_InitStruct.Pin = SW_I2C_SDA_PIN;          /* PB9 */
    HAL_GPIO_Init(SW_I2C_SDA_PORT, &GPIO_InitStruct);

    /*
     * 释放I2C总线, 空闲状态下SCL和SDA都应该为高电平
     * 上电后先拉高, 确保总线处于空闲状态
     */
    SCL_H();
    SDA_H();
}

/**
 * @brief  发送I2C起始信号
 * @note   对应原代码 static FunctionalState I2C1_Start(void)
 *
 *         I2C起始条件: SCL为高时, SDA从高变低
 *
 *         原代码时序:
 *           SDA_H; SCL_H;        // 先释放两条线
 *           delay;
 *           if(!SDA_read) return DISABLE;  // 检查SDA是否为高(总线空闲)
 *           SDA_L;               // SDA拉低 = 起始信号
 *           delay;
 *           if(SDA_read) return DISABLE;   // 确认SDA已拉低
 *           SDA_L; delay;        // 保持
 *
 * @retval 1 = 起始信号发送成功
 *         0 = 总线忙(SDA被其他设备拉低)
 */
uint8_t SW_I2C_Start(void)
{
    SDA_H();           /* 释放SDA */
    SCL_H();           /* 释放SCL */
    I2C_Delay();

    /* 检测SDA是否为高电平, 如果为低说明总线被占用 */
    if (!SDA_READ()) {
        return 0;      /* 总线忙, 对应原代码 return DISABLE */
    }

    SDA_L();           /* SCL为高时, SDA从高变低 = 起始信号 */
    I2C_Delay();

    /* 确认SDA已经被成功拉低 */
    if (SDA_READ()) {
        return 0;      /* 异常: SDA没有变低, 对应原代码 return DISABLE */
    }

    SDA_L();           /* 保持SDA为低 */
    I2C_Delay();

    return 1;          /* 成功, 对应原代码 return ENABLE */
}

/**
 * @brief  发送I2C停止信号
 * @note   对应原代码 static void I2C1_Stop(void)
 *
 *         I2C停止条件: SCL为高时, SDA从低变高
 *
 *         原代码时序:
 *           SCL_L; delay;    // SCL先拉低
 *           SDA_L; delay;    // SDA确保为低
 *           SCL_H; delay;    // SCL拉高
 *           SDA_H; delay;    // SCL高时SDA从低变高 = 停止信号
 */
void SW_I2C_Stop(void)
{
    SCL_L();           /* SCL先拉低 */
    I2C_Delay();
    SDA_L();           /* SDA拉低, 为后面的上升沿做准备 */
    I2C_Delay();
    SCL_H();           /* SCL拉高 */
    I2C_Delay();
    SDA_H();           /* SCL高时SDA从低变高 = 停止信号 */
    I2C_Delay();
}

/**
 * @brief  主机发送ACK应答 (告诉从机: 数据已收到, 继续发)
 * @note   对应原代码 static void I2C1_Ack(void)
 *
 *         ACK: 在第9个时钟, SDA为低电平
 *
 *         原代码时序:
 *           SCL_L; delay;    // SCL拉低, 准备设置SDA
 *           SDA_L; delay;    // SDA拉低 = ACK
 *           SCL_H; delay;    // SCL拉高, 从机采样到ACK
 *           SCL_L; delay;    // SCL拉低, 一个时钟完成
 */
void SW_I2C_Ack(void)
{
    SCL_L();
    I2C_Delay();
    SDA_L();           /* SDA为低 = ACK */
    I2C_Delay();
    SCL_H();           /* 时钟上升沿, 从机采样 */
    I2C_Delay();
    SCL_L();           /* 时钟结束 */
    I2C_Delay();
}

/**
 * @brief  主机发送NACK非应答 (告诉从机: 不再接收了)
 * @note   对应原代码 static void I2C1_NoAck(void)
 *
 *         NACK: 在第9个时钟, SDA为高电平
 *         通常在读取最后一个字节后发送NACK, 然后紧跟STOP
 *
 *         原代码时序:
 *           SCL_L; delay;
 *           SDA_H; delay;    // SDA拉高 = NACK
 *           SCL_H; delay;
 *           SCL_L; delay;
 */
void SW_I2C_NAck(void)
{
    SCL_L();
    I2C_Delay();
    SDA_H();           /* SDA为高 = NACK */
    I2C_Delay();
    SCL_H();           /* 时钟上升沿, 从机采样 */
    I2C_Delay();
    SCL_L();           /* 时钟结束 */
    I2C_Delay();
}

/**
 * @brief  等待从机发送ACK应答
 * @note   对应原代码 static FunctionalState I2C1_WaitAck(void)
 *
 *         发送完8bit数据后, 第9个时钟由从机控制SDA:
 *           SDA=低 -> 从机应答ACK (数据收到)
 *           SDA=高 -> 从机非应答NACK (有问题)
 *
 *         原代码时序:
 *           SCL_L; delay;
 *           SDA_H; delay;    // 释放SDA, 让从机控制
 *           SCL_H; delay;    // 时钟高, 读取从机应答
 *           if(SDA_read) { SCL_L; return DISABLE; }  // NACK
 *           SCL_L; return ENABLE;                     // ACK
 *
 * @retval 1 = 收到ACK
 *         0 = 收到NACK或无应答
 */
uint8_t SW_I2C_WaitAck(void)
{
    SCL_L();
    I2C_Delay();
    SDA_H();           /* 释放SDA线, 让从机来控制SDA */
    I2C_Delay();
    SCL_H();           /* 时钟拉高, 在这个时刻读取从机的应答 */
    I2C_Delay();

    if (SDA_READ()) {
        /* SDA仍然为高 = 从机没有拉低 = NACK */
        SCL_L();
        return 0;      /* 对应原代码 return DISABLE */
    }

    /* SDA被从机拉低 = ACK */
    SCL_L();
    return 1;          /* 对应原代码 return ENABLE */
}

/**
 * @brief  发送1个字节, 高位先发 (MSB first)
 * @note   对应原代码 static void I2C1_SendByte(uint8_t SendByte)
 *
 *         每个bit的发送过程:
 *           SCL拉低 -> 设置SDA(数据位) -> SCL拉高(从机在上升沿采样)
 *         循环8次发完一个字节
 *
 *         原代码:
 *           uint8_t i=8;
 *           while(i--) {
 *               SCL1_L; delay;
 *               if(SendByte&0x80) SDA1_H; else SDA1_L;
 *               SendByte<<=1;
 *               delay;
 *               SCL1_H; delay;
 *           }
 *           SCL1_L;
 *
 * @param  byte: 要发送的1字节数据
 */
void SW_I2C_SendByte(uint8_t byte)
{
    uint8_t i = 8;

    while (i--) {
        SCL_L();               /* SCL拉低, 这时可以改变SDA */
        I2C_Delay();

        if (byte & 0x80) {     /* 取最高位 */
            SDA_H();           /* bit=1, SDA拉高 */
        } else {
            SDA_L();           /* bit=0, SDA拉低 */
        }

        byte <<= 1;           /* 左移, 准备下一位 */
        I2C_Delay();

        SCL_H();               /* SCL拉高, 从机在此上升沿采样SDA */
        I2C_Delay();
    }

    SCL_L();                   /* 8位发完, SCL拉低, 准备接收ACK */
}

/**
 * @brief  接收1个字节, 高位先收 (MSB first)
 * @note   对应原代码 static uint8_t I2C1_ReceiveByte(void)
 *
 *         每个bit的接收过程:
 *           SCL拉低 -> 等待从机设置SDA -> SCL拉高 -> 读取SDA
 *         循环8次收完一个字节
 *
 *         原代码:
 *           uint8_t i=8;
 *           uint8_t ReceiveByte=0;
 *           SDA1_H;           // 释放SDA, 从机输出数据
 *           while(i--) {
 *               ReceiveByte<<=1;
 *               SCL1_L; delay;
 *               SCL1_H; delay;
 *               if(SDA1_read) ReceiveByte|=0x01;
 *           }
 *           SCL1_L;
 *
 *         注意: 调用此函数后, 需要自己发ACK或NACK
 *               读多个字节: 前面的用 SW_I2C_Ack(), 最后一个用 SW_I2C_NAck()
 *
 * @retval 接收到的1字节数据
 */
uint8_t SW_I2C_ReceiveByte(void)
{
    uint8_t i = 8;
    uint8_t byte = 0;

    SDA_H();                   /* 释放SDA线, 这样从机才能控制SDA输出数据 */

    while (i--) {
        byte <<= 1;           /* 左移, 为新bit腾出最低位 */

        SCL_L();               /* SCL拉低, 从机会在这段时间设置SDA */
        I2C_Delay();

        SCL_H();               /* SCL拉高, 此时SDA上的数据有效, 可以读取 */
        I2C_Delay();

        if (SDA_READ()) {      /* 读取SDA */
            byte |= 0x01;     /* SDA为高, 该bit = 1 */
        }
        /* SDA为低则该bit保持为0 (左移后默认就是0) */
    }

    SCL_L();                   /* 8位收完, SCL拉低 */
    return byte;
}
