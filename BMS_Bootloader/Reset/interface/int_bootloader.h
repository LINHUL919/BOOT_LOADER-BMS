#ifndef __INT_BOOTLOADER_H
#define __INT_BOOTLOADER_H

#include "usart.h"
#include "stdio.h"
#include "string.h"

#define BOOTLOADER_UART_RECEIVE_BUFFER_SIZE 512

//程序写入的起始位置A区
#define APP_START_ADDRESS 0x0800C000
//F407扇区3地址范围: 0x0800C000 ~ 0x0800FFFF（16KB）
#define APP_SECTOR3_END_ADDRESS 0x0800FFFFU

//栈顶地址必须在SRAM范围内，F407的SRAM范围是0x20000000 ~ 0x2001FFFF
#define SRAM_START_ADDRESS 0x20000000U
#define SRAM_END_ADDRESS 0x2001FFFFU





/*
 * @brief: UART receive event callback
 * @param {UART_HandleTypeDef} *huart: UART handle
 * @param {uint16_t} Size: Number of bytes received
 * @return {*}
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size);





/*
 * @brief: 准备接收A程序
 * @return {*}
 */
void Int_bootloader_rec_app(void);


/*
 * @brief: 跳转到应用程序
 * @return {*}0表示跳转成功，1表示跳转失败
 */
uint8_t Int_bootloader_jump_to_app(void);

/*
 * @brief: 擦除应用程序区域的Flash
 * @return {*}
 */
void Int_bootloader_erase(void);

#endif // __INT_BOOTLOADER_H

