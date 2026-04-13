#ifndef __APP_BOOTLOADER_H__
#define __APP_BOOTLOADER_H__

#include "int_w24c02.h"
#include "int_bootloader.h"
#include "int_w25q128.h"

//添加校验密钥
#define BOOTLOADER_KEY_ADDR 0x11 //EEPROM中存储校验密钥的地址
#define BOOTLOADER_KEY_VALUE 0x3C4D //校验密钥的值

#define BOOTLOADER_FLAG_ADDR 0x10 //EEPROM中存储bootloader标志的地址
//需要更新
#define BOOTLOADER_FLAG_VALUE 0xA5 //需要更新时写入的标志值
//不需要更新
#define BOOTLOADER_FLAG_NO_UPDATE 0x5A //不需要更新时写入的标志值

//恢复出厂设置的标志
#define BOOTLOADER_FLAG_FACTORY_RESET 0xFF //恢复出厂设置时写

//CAN更新模式的标志
#define BOOTLOADER_FLAG_CAN_UPDATE 0xCC //进入CAN更新工具模式

//元数据信息地址0x00000000 和长度 FLASH
#define METADATA_ADDRESS_BLOCK_ID 0x00 //元数据中块ID的偏移地址
#define METADATA_ADDRESS_SECTOR_ID 0x00 //元数据中扇区ID的偏移地址
#define METADATA_ADDRESS_PAGE_ID 0x00 //元数据中页ID的偏移地址
#define METADATA_ADDRESS_ADDR 0x00 //元数据中页内地址的偏移地址

//更新程序写入的内置Flash目标地址（Sector 3）
#define APP_UPDATE_START_ADDRESS  0x0800C000U  //Sector 3 起始地址
#define APP_UPDATE_END_ADDRESS    0x08010000U  //Sector 3 结束地址（不含）

//程序存储的判断条件
//1.地址条件,不能在外置FLASH的第一个扇区，就只是人为规定而已
#define PROGRAM_START_ADDRESS_MIN 0x001000
#define PROGRAM_START_ADDRESS_MAX 0xFFFFFF
//2.长度条件，不能小于500字节，也不能大于app区的最大长度16KB
#define PROGRAM_LENGTH_MIN 500
#define PROGRAM_LENGTH_MAX (16 * 1024)


//判断当前是否需要更新
void App_bootloader_NeedUpdate(void);

//检查是否需要恢复出厂设置
void App_bootloader_check_factory_reset(void);

//执行更新流程
void App_bootloader_Update(void);

//执行跳转到应用程序的流程
void App_bootloader_JumpToApp(void);

#endif /* __APP_BOOTLOADER_H__ */

