#include "app_update.h"
#include <stdio.h>
#include "int_w25q128.h"

/* ======================== 全局变量 ======================== */
static uint8_t  app_data_buffer[APP_DATA_MAX_LEN]; // 固件+CRC 缓冲区
static uint32_t app_data_length = 0;               // 已接收总字节数（含CRC）

static CAN_MessageTypeDef can_rx_buf[3];            // CAN 接收缓冲（FIFO最多3帧）
static uint8_t  can_rx_count = 0;

static Update_StateTypeDef update_state = UPDATE_SEND_CMD; // 上电直接发送
static uint32_t last_receive_time = 0;              // 上次收到数据的 tick


/* ======================== CRC 计算 ======================== */
// 使用硬件 CRC 外设计算 CRC32
static uint32_t App_crc_cal(uint8_t *data, uint32_t length)
{
    uint32_t *data_32 = (uint32_t *)data;
    uint32_t length_32 = (length + 3) / 4;
    __HAL_CRC_DR_RESET(&hcrc);
    return HAL_CRC_Calculate(&hcrc, data_32, length_32);
}


/* ====================== 初始化 ============================ */
// 初始化 CAN2 并自动进入发送请求状态（上电即调用）
void App_Update(void)
{
    CAN2_Init();
    printf("CAN Tool Start\r\n");
    update_state = UPDATE_SEND_CMD;
}


/* ==================== 状态机各阶段 ======================== */

// 发送 CAN "sss"，请求上位机开始发固件
static void do_send_cmd(void)
{
    memset(app_data_buffer, 0, sizeof(app_data_buffer));
    app_data_length = 0;
    last_receive_time = 0;

    CAN2_SendData(APP_UPDATE_CMD_ID,
                  (uint8_t *)APP_UPDATE_REQUEST,
                  strlen(APP_UPDATE_REQUEST));

    printf("Request sent, waiting for firmware...\r\n");
    update_state = UPDATE_RECEIVE_DATA;
}


// 逐帧接收所有 CAN 数据（固件+CRC），超时后进入校验
static void do_receive_data(void)
{
    CAN2_ReceiveData(can_rx_buf, &can_rx_count);

    for (uint8_t i = 0; i < can_rx_count; i++)
    {
        uint8_t dlc = can_rx_buf[i].RxHeader.DLC;

        // 溢出保护
        if (app_data_length + dlc > APP_DATA_MAX_LEN)
        {
            printf("ERROR: data overflow!\r\n");
            update_state = UPDATE_END;
            can_rx_count = 0;
            return;
        }

        memcpy(app_data_buffer + app_data_length, can_rx_buf[i].data, dlc);
        app_data_length += dlc;
        last_receive_time = HAL_GetTick();
    }
    can_rx_count = 0;

    // 超时判定：3000ms 无新帧 → 数据(含CRC)全部收完
    if (last_receive_time != 0 &&
        (HAL_GetTick() - last_receive_time) > RECV_TIMEOUT_MS)
    {
        printf("Receive done, total: %d bytes\r\n", (int)app_data_length);
        update_state = UPDATE_CRC_CHECK;
    }
}


// 提取末尾 4 字节为 CRC32，校验前面的固件数据
static void do_crc_check(void)
{
    // 至少需要固件数据 + 4字节CRC
    if (app_data_length <= 4)
    {
        printf("ERROR: too short (%d bytes)\r\n", (int)app_data_length);
        update_state = UPDATE_END;
        return;
    }

    uint32_t firmware_len = app_data_length - 4;

    // 最后 4 字节是 CRC（低位在前）
    uint32_t received_crc =
        ((uint32_t)app_data_buffer[firmware_len])            |
        ((uint32_t)app_data_buffer[firmware_len + 1] << 8)   |
        ((uint32_t)app_data_buffer[firmware_len + 2] << 16)  |
        ((uint32_t)app_data_buffer[firmware_len + 3] << 24);

    uint32_t calc_crc = App_crc_cal(app_data_buffer, firmware_len);

    if (calc_crc == received_crc)
    {
        printf("CRC OK (0x%08X), firmware=%d bytes\r\n",
               (unsigned)calc_crc, (int)firmware_len);
        app_data_length = firmware_len;  // 更新为纯固件长度
        update_state = UPDATE_WRITE_STORAGE;
    }
    else
    {
        printf("CRC error! calc=0x%08X recv=0x%08X\r\n",
               (unsigned)calc_crc, (unsigned)received_crc);
        update_state = UPDATE_END;
    }
}


// 写 W25Q128（元数据+固件）+ 写 EEPROM 更新标志，完成后复位
static void do_write_storage(void)
{
    /* ---------- 1. 擦除 W25Q128 ---------- */
    printf("Erasing W25Q128...\r\n");
    // 擦除元数据扇区 (Block0, Sector0, 地址0x000000)
    W25Q128_EraseSector(0, 0);
    // 擦除数据扇区 (从0x001000起，按固件大小计算需要几个4KB扇区)
    uint16_t data_sectors = (app_data_length + 4095) / 4096;
    for (uint8_t i = 0; i < data_sectors; i++)
    {
        uint32_t sector_addr = APP_DATA_ADDR + ((uint32_t)i * 4096);
        uint8_t block  = (sector_addr >> 16) & 0x0F;
        uint8_t sector = (sector_addr >> 12) & 0x0F;
        W25Q128_EraseSector(block, sector);
    }

    /* ---------- 2. 写入元数据（大端序，8字节）---------- */
    uint8_t metadata[8];
    // 字节[0:3] = 程序在W25Q128中的起始地址 0x00001000（大端）
    metadata[0] = (APP_DATA_ADDR >> 24) & 0xFF;   // 0x00
    metadata[1] = (APP_DATA_ADDR >> 16) & 0xFF;   // 0x00
    metadata[2] = (APP_DATA_ADDR >> 8)  & 0xFF;   // 0x10
    metadata[3] = (APP_DATA_ADDR >> 0)  & 0xFF;   // 0x00
    // 字节[4:7] = 程序长度（大端）
    metadata[4] = (app_data_length >> 24) & 0xFF;
    metadata[5] = (app_data_length >> 16) & 0xFF;
    metadata[6] = (app_data_length >> 8)  & 0xFF;
    metadata[7] = (app_data_length >> 0)  & 0xFF;
    W25Q128_WriteData(0, 0, 0, 0, metadata, 8);   // Block0,Sector0,Page0

    /* ---------- 3. 写入固件数据（0x001000起，每页256字节）---------- */
    printf("Writing firmware to W25Q128...\r\n");
    uint32_t written = 0;
    while (written < app_data_length)
    {
        uint16_t chunk = (app_data_length - written > 256)
                         ? 256 : (app_data_length - written);
        // 将线性偏移转换为 block/sector/page/offset 参数
        uint32_t abs_addr = APP_DATA_ADDR + written;
        uint8_t block  = (abs_addr >> 16) & 0x0F;
        uint8_t sector = (abs_addr >> 12) & 0x0F;
        uint8_t page   = (abs_addr >> 8)  & 0x0F;
        uint8_t offset = abs_addr & 0xFF;
        W25Q128_WriteData(block, sector, page, offset,
                          app_data_buffer + written, chunk);
        written += chunk;
    }

    /* ---------- 4. 写 EEPROM 更新标志 ---------- */
    uint8_t eeprom_buf[3];
    eeprom_buf[0] = BOOTLOADER_FLAG_VALUE;   // 0xA5 → addr 0x10
    eeprom_buf[1] = BOOTLOADER_KEY_BYTE_11;  // 0x3C → addr 0x11（密钥高字节）
    eeprom_buf[2] = BOOTLOADER_KEY_BYTE_12;  // 0x4D → addr 0x12（密钥低字节）
    W24C02_WriteBytes(BOOTLOADER_FLAG_ADDR, eeprom_buf, 3);

    printf("Update ready, rebooting...\r\n");
    update_state = UPDATE_END;
}


/* ======================== 主循环状态机 ======================== */
void App_Update_Work(void)
{
    switch (update_state)
    {
    case UPDATE_SEND_CMD:
        do_send_cmd();
        break;
    case UPDATE_RECEIVE_DATA:
        do_receive_data();
        break;
    case UPDATE_CRC_CHECK:
        do_crc_check();
        break;
    case UPDATE_WRITE_STORAGE:
        do_write_storage();
        break;
    case UPDATE_END:
        HAL_Delay(1000);
        NVIC_SystemReset();
        break;
    default:
        break;
    }
}

