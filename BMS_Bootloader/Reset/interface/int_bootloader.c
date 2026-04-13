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

        //写入前先检查是否越过扇区3边界
        if ((APP_START_ADDRESS + flash_write_offset + receive_length) > APP_SECTOR3_END_ADDRESS)
        {
            printf("Flash overflow\r\n");
            HAL_UARTEx_ReceiveToIdle_IT(&huart1, receive_buffer, BOOTLOADER_UART_RECEIVE_BUFFER_SIZE);
            return;
        }

        //将接收到的数据按字节连续写入Flash，地址从APP_START_ADDRESS开始递增
        HAL_FLASH_Unlock();

        for (uint32_t i = 0; i < receive_length; i++)
        {
            uint32_t write_address = APP_START_ADDRESS + flash_write_offset + i;
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE,
                                  write_address,
                                  receive_buffer[i]) != HAL_OK)
            {
                printf("Flash write failed\r\n");
                HAL_FLASH_Lock();
                HAL_UARTEx_ReceiveToIdle_IT(&huart1, receive_buffer, BOOTLOADER_UART_RECEIVE_BUFFER_SIZE);
                return;
            }
        }

        flash_write_offset += receive_length;

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
 * @brief: 跳转到应用程序
 * @return {*}0表示跳转成功，1表示跳转失败
 */
uint8_t Int_bootloader_jump_to_app(void)
{
   typedef void (*pFunction)(void);

    //1.校验应用程序的合法性，主要是检查初始栈指针和复位处理函数地址是否合理
   uint32_t app_stack_pointer = *(__IO uint32_t *)APP_START_ADDRESS;//获取应用程序的初始栈指针
   uint32_t app_reset_handler = *(__IO uint32_t *)(APP_START_ADDRESS + 4);//获取应用程序的复位处理函数地址
   //1.1校验栈顶地址，必须在SRAM范围内，F407的SRAM范围是0x20000000 ~ 0x2001FFFF
    if ((app_stack_pointer < SRAM_START_ADDRESS) || (app_stack_pointer > SRAM_END_ADDRESS))
    {
         printf("Invalid application stack pointer\r\n");
         return 1;
    }
    //1.2校验复位处理函数地址，必须在Flash范围内
    if ((app_reset_handler < APP_START_ADDRESS) || (app_reset_handler > APP_SECTOR3_END_ADDRESS))

    {
         printf("Invalid app_reset_handler\r\n");
         return 1;
    }
    //2.注销boot loader程序

    //手动注销内核相关的设置，
    NVIC_DisableIRQ(EXTI15_10_IRQn);//禁用外部中断(KEY1使用PC13,对应EXTI15_10)
    NVIC_DisableIRQ(USART1_IRQn);//禁用USART1中断
    //关闭Sys Tick
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    //注销HAL库设置，只是去注销外设，不回去注销内核相关的设置，所以NVIC中断控制器的设置还在
    HAL_DeInit();
    //2.1关闭中断
    __disable_irq();
    //2.2设置主堆栈指针MSP为应用程序的初始栈指针
    __set_MSP(app_stack_pointer);
    //2.3重定向中断向量表
    SCB->VTOR = APP_START_ADDRESS;
    //2.4跳转到应用程序的复位处理函数
    pFunction app_entry = (pFunction)app_reset_handler;
    app_entry();

    return 0;
}

/*
 * @brief: 擦除应用程序区域的Flash
 * @return {*}
 */
void Int_bootloader_erase(void)
{
    //擦除扇区3（0x0800C000~0x0800FFFF），即APP运行区，后续回调中只负责写入
    HAL_FLASH_Unlock();
    FLASH_EraseInitTypeDef erase_init;
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Sector = FLASH_SECTOR_3;
    erase_init.NbSectors = 1;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    uint32_t sector_error = 0;
    if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK)
    {
       printf("Flash erase failed\r\n");
    }
     HAL_FLASH_Lock();
}




