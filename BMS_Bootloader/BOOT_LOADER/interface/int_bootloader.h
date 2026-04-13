#ifndef __INT_BOOTLOADER_H
#define __INT_BOOTLOADER_H

#include "usart.h"
#include "stdio.h"
#include "string.h"

//默认程序起始位置（Sector 1）
#define DEFAULT_APP_START_ADDRESS 0x08004000
//F407扇区1地址范围: 0x08004000 ~ 0x08007FFF
#define DEFAULT_APP_SECTOR1_END_ADDRESS 0x08008000U

//CAN更新工具区（Sector 2）
#define CAN_TOOL_START_ADDRESS  0x08008000U
//F407扇区2地址范围: 0x08008000 ~ 0x0800BFFF
#define CAN_TOOL_SECTOR2_END_ADDRESS 0x0800C000U

//程序写入的起始位置A区（Sector 3）
#define APP_START_ADDRESS 0x0800C000U
//F407扇区3地址范围: 0x0800C000 ~ 0x0800FFFF
#define APP_SECTOR3_END_ADDRESS 0x08010000U

//栈顶地址必须在SRAM范围内，F407的SRAM范围是0x20000000 ~ 0x2001FFFF
#define SRAM_START_ADDRESS 0x20000000U
#define SRAM_END_ADDRESS 0x2001FFFFU






/*
 * @brief: 跳转到应用程序
 * @return {*}0表示跳转成功，1表示跳转失败
 */
uint8_t Int_bootloader_jump_to_app(void);

/*
 * @brief: 跳转到默认程序（Sector 1）
 * @return {*}0表示跳转成功，1表示跳转失败
 */
uint8_t Int_bootloader_jump_to_default_app(void);

/*
 * @brief: 跳转到CAN更新工具（Sector 2）
 * @return {*}0表示跳转成功，1表示跳转失败
 */
uint8_t Int_bootloader_jump_to_can_tool(void);

#endif // __INT_BOOTLOADER_H

