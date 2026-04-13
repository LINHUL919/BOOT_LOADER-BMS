#ifndef APP_UPDATE_H
#define APP_UPDATE_H

#include "usart.h"
#include "can.h"
#include "Int_can2.h"
#include "string.h"
#include "crc.h"
#include "int_w24c02.h"

/* ======================== CAN 更新请求 ======================== */
#define APP_UPDATE_REQUEST      "sss"           // 请求上位机发送固件
#define APP_UPDATE_CMD_ID       0               // CAN 帧 StdId

/* ======================== 固件缓冲区 ========================== */
#define APP_DATA_MAX_LEN        (16 * 1024 + 4) // 最大固件16KB + 4B CRC

/* ========== W25Q128 存储地址（必须和 Bootloader 匹配）========= */
#define APP_METADATA_ADDR       0x000000        // 元数据地址（8字节，大端序）
#define APP_DATA_ADDR           0x001000        // 固件数据起始地址

/* ========== W24C02 标志（必须和 Bootloader 匹配）============= */
#define BOOTLOADER_FLAG_ADDR    0x10            // 更新标志地址
#define BOOTLOADER_FLAG_VALUE   0xA5            // 需要更新
#define BOOTLOADER_KEY_BYTE_11  0x3C            // 地址0x11：密钥高字节
#define BOOTLOADER_KEY_BYTE_12  0x4D            // 地址0x12：密钥低字节

/* ======================== 接收超时 ============================ */
// 上位机每256B延时100ms，数据发完延时2100ms再发CRC
// 3000ms超时确保：跨越2100ms间隔 + CRC帧到达后充分等待
#define RECV_TIMEOUT_MS         3000

/* ======================== 状态机 ============================== */
typedef enum
{
    UPDATE_SEND_CMD = 0,        // 发送 "sss" 请求
    UPDATE_RECEIVE_DATA,        // 逐帧接收固件+CRC
    UPDATE_CRC_CHECK,           // 提取末尾4字节做CRC校验
    UPDATE_WRITE_STORAGE,       // 写 W25Q128 + EEPROM
    UPDATE_END,                 // 延时后复位
} Update_StateTypeDef;

/* ======================== 对外接口 ============================ */
void App_Update(void);          // 初始化并自动开始
void App_Update_Work(void);     // 主循环轮询

#endif // APP_UPDATE_H


