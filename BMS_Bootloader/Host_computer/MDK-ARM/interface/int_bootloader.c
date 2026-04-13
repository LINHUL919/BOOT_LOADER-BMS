#include "int_bootloader.h"

//接收缓冲区
uint8_t receive_buffer[BOOTLOADER_UART_RECEIVE_BUFFER_SIZE];
uint16_t receive_length = 0;
uint16_t total_length = 0;

//当前写入程序的偏移量
uint32_t flash_write_offset = 0;

//记录当前接收数据的时间
uint32_t last_receive_time = 0;





void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    //判断是否是我们使用的串口
    if (huart->Instance == USART1)
    {
        //记录当前接收数据的时间  单位是毫秒
        last_receive_time = HAL_GetTick();

        //更新接收长度
        receive_length = Size;
        total_length += receive_length;

        //写入前先检查是否越过应用区末尾
        if ((APP_START_ADDRESS + flash_write_offset + receive_length) > APP_END_ADDRESS)
        {
            printf("Flash overflow\r\n");
            HAL_UARTEx_ReceiveToIdle_IT(&huart1, receive_buffer, BOOTLOADER_UART_RECEIVE_BUFFER_SIZE);
            return;
        }

        //F103只支持半字（16位）写入，将接收数据按2字节一组写入Flash
        HAL_FLASH_Unlock();

        for (uint32_t i = 0; i < receive_length; i += 2)
        {
            uint32_t write_address = APP_START_ADDRESS + flash_write_offset + i;
            uint16_t data;
            if (i + 1 < receive_length)
            {
                //正常情况：两字节组成一个半字（小端序）
                data = (uint16_t)receive_buffer[i] | ((uint16_t)receive_buffer[i + 1] << 8);
            }
            else
            {
                //奇数字节剩余最后一字节，高位补0xFF
                data = (uint16_t)receive_buffer[i] | 0xFF00;
            }
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                  write_address,
                                  (uint64_t)data) != HAL_OK)
            {
                printf("Flash write failed\r\n");
                HAL_FLASH_Lock();
                HAL_UARTEx_ReceiveToIdle_IT(&huart1, receive_buffer, BOOTLOADER_UART_RECEIVE_BUFFER_SIZE);
                return;
            }
        }

        //半字写入实际占用的字节数必须是偶数，否则下次写入地址会不对齐
        flash_write_offset += (receive_length + 1) & ~1U;

        //重新加锁
        HAL_FLASH_Lock();

        //使用完数据之后，清空接收缓冲区，准备下一次的接收
        memset(receive_buffer, 0, BOOTLOADER_UART_RECEIVE_BUFFER_SIZE);

        //准备下一次接收，继续使用中断方式
        HAL_UARTEx_ReceiveToIdle_IT(&huart1, receive_buffer, BOOTLOADER_UART_RECEIVE_BUFFER_SIZE);
    }
}
/*
 * @brief: 初始化引导程序，准备接收A程序
 * @return {*}
 */
void Int_bootloader_rec_app(void)
{
    //初始化写入状态
    flash_write_offset = 0;
    total_length = 0;   

    //清空初始化之前串口使用的所有问题
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);

    //带有中断的串口接收函数，接收完成后会调用回调函数
    //句柄huart1，接收缓冲区receive_buffer，缓冲区大小BOOTLOADER_UART_RECEIVE_BUFFER_SIZE
    //接收到空闲帧时触发中断，接收完成后会调用回调函数
    HAL_UARTEx_ReceiveToIdle_IT(&huart1, receive_buffer, BOOTLOADER_UART_RECEIVE_BUFFER_SIZE);
}





/*
 * @brief: 擦除应用程序区域的Flash
 * @return {*}
 */
void Int_bootloader_erase(void)
{
    //初始化阶段先擦除应用区所有页，后续回调中只负责写入
    //F103C8T6：页擦除，每页1KB，共擦除32页（0x08008000~0x0800FFFF）
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef erase_init;
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.PageAddress = APP_START_ADDRESS;
    erase_init.NbPages = APP_PAGE_COUNT;

    uint32_t page_error = 0;
    if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK)
    {
       printf("Flash erase failed\r\n");
    }
    HAL_FLASH_Lock();
}




