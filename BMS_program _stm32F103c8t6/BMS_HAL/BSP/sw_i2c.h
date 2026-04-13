#ifndef __SW_I2C_H
#define __SW_I2C_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/**
 * @file    sw_i2c.h
 * @brief   软件模拟I2C驱动 (HAL库版本)
 * @note    移植自原工程 BSP/i2c1.h + BSP/i2c1.c
 *          原工程使用标准库 GPIO_SetBits/ResetBits，这里改为 HAL_GPIO_WritePin
 *          引脚配置: PB8(SCL), PB9(SDA)，开漏输出
 */

/* ======================== I2C引脚定义 ========================
 * 对应原代码 i2c1.h 中的:
 *   #define I2C1_SCL_PIN  GPIO_Pin_8   // PB8
 *   #define I2C1_SDA_PIN  GPIO_Pin_9   // PB9
 * 如果你的硬件引脚不同，只需修改这里即可
 * ============================================================ */
#define SW_I2C_SCL_PORT     GPIOB
#define SW_I2C_SCL_PIN      GPIO_PIN_8
#define SW_I2C_SDA_PORT     GPIOB
#define SW_I2C_SDA_PIN      GPIO_PIN_9

/* ======================== 引脚操作宏 ========================
 * 对应原代码中的 SCL1_H, SCL1_L, SDA1_H, SDA1_L, SCL1_read, SDA1_read
 * ============================================================ */
#define SCL_H()     HAL_GPIO_WritePin(SW_I2C_SCL_PORT, SW_I2C_SCL_PIN, GPIO_PIN_SET)
#define SCL_L()     HAL_GPIO_WritePin(SW_I2C_SCL_PORT, SW_I2C_SCL_PIN, GPIO_PIN_RESET)
#define SDA_H()     HAL_GPIO_WritePin(SW_I2C_SDA_PORT, SW_I2C_SDA_PIN, GPIO_PIN_SET)
#define SDA_L()     HAL_GPIO_WritePin(SW_I2C_SDA_PORT, SW_I2C_SDA_PIN, GPIO_PIN_RESET)
#define SCL_READ()  HAL_GPIO_ReadPin(SW_I2C_SCL_PORT, SW_I2C_SCL_PIN)
#define SDA_READ()  HAL_GPIO_ReadPin(SW_I2C_SDA_PORT, SW_I2C_SDA_PIN)

/* ======================== 函数声明 ========================== */

/**
 * @brief  I2C GPIO初始化, 配置SCL/SDA为开漏输出
 * @note   对应原代码 I2C1_Configuration()
 *         如果已在CubeMX中配置PB8/PB9为开漏输出, 可不调用
 */
void SW_I2C_Init(void);

/**
 * @brief  发送I2C起始信号
 * @note   对应原代码 I2C1_Start()
 * @retval 1=成功, 0=总线忙
 */
uint8_t SW_I2C_Start(void);

/**
 * @brief  发送I2C停止信号
 * @note   对应原代码 I2C1_Stop()
 */
void SW_I2C_Stop(void);

/**
 * @brief  发送ACK应答
 * @note   对应原代码 I2C1_Ack()
 */
void SW_I2C_Ack(void);

/**
 * @brief  发送NACK非应答
 * @note   对应原代码 I2C1_NoAck()
 */
void SW_I2C_NAck(void);

/**
 * @brief  等待从机ACK
 * @note   对应原代码 I2C1_WaitAck()
 * @retval 1=收到ACK, 0=未收到(NACK)
 */
uint8_t SW_I2C_WaitAck(void);

/**
 * @brief  发送1个字节 (MSB先发)
 * @note   对应原代码 I2C1_SendByte()
 * @param  byte: 要发送的字节
 */
void SW_I2C_SendByte(uint8_t byte);

/**
 * @brief  接收1个字节 (MSB先收)
 * @note   对应原代码 I2C1_ReceiveByte()
 * @retval 收到的字节
 */
uint8_t SW_I2C_ReceiveByte(void);

#ifdef __cplusplus
}
#endif

#endif /* __SW_I2C_H */
