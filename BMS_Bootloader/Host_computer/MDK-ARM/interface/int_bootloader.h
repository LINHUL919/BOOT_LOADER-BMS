#ifndef __INT_BOOTLOADER_H
#define __INT_BOOTLOADER_H

#include "usart.h"
#include "stdio.h"
#include "string.h"

#define BOOTLOADER_UART_RECEIVE_BUFFER_SIZE 512

//程序写入的起始位置A区（Bootloader占前16KB）
#define APP_START_ADDRESS       0x08008000U
//F103C8T6 Flash总共64KB，结束地址0x08010000
#define APP_END_ADDRESS         0x08010000U
//F103C8T6每页1KB（0x400）
#define FLASH_PAGE_SIZE         0x400U
//应用区页数 = (0x08010000 - 0x08008000) / 0x400 = 32页
#define APP_PAGE_COUNT          ((APP_END_ADDRESS - APP_START_ADDRESS) / FLASH_PAGE_SIZE)







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

