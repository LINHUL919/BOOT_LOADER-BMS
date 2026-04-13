#include "int_bootloader.h"


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
         //invalid SP
         return 1;
    }
    //1.2校验复位处理函数地址，必须在Sector 3 Flash范围内
    if ((app_reset_handler < APP_START_ADDRESS) || (app_reset_handler > APP_SECTOR3_END_ADDRESS))
    {
         //invalid reset
         return 1;
    }
    //2.注销boot loader程序

    //手动注销内核相关的设置，
    NVIC_DisableIRQ(EXTI0_IRQn);//禁用外部中断（KEY2在PA0，对应EXTI线0）
    NVIC_DisableIRQ(EXTI15_10_IRQn);//禁用外部中断（KEY1在PC13，对应EXTI线13）
    NVIC_DisableIRQ(USART1_IRQn);//禁用USART1中断
    //关闭Sys Tick
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    //注销HAL库设置，只是去注销外设，不回去注销内核相关的设置，所以NVIC中断控制器的设置还在
    //配置引脚外部中断会被清空，但NVIC中断控制器的设置不会被清空，所以需要手动去禁用相关的中断
    //外部中断默认0的话会指向引脚PA0，所以如果不禁用外部中断，跳转到应用程序后，如果PA0引脚状态发生变化，就会触发外部中断，导致程序异常
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
 * @brief: 跳转到默认程序（Sector 1, 0x08004000）
 * @return {*}0表示跳转成功，1表示跳转失败
 */
uint8_t Int_bootloader_jump_to_default_app(void)
{
   typedef void (*pFunction)(void);

    //1.校验默认程序的合法性
   uint32_t app_stack_pointer = *(__IO uint32_t *)DEFAULT_APP_START_ADDRESS;
   uint32_t app_reset_handler = *(__IO uint32_t *)(DEFAULT_APP_START_ADDRESS + 4);
   //1.1校验栈顶地址，必须在SRAM范围内
    if ((app_stack_pointer < SRAM_START_ADDRESS) || (app_stack_pointer > SRAM_END_ADDRESS))
    {
         //invalid SP
         return 1;
    }
    //1.2校验复位处理函数地址，必须在Sector 1 Flash范围内
    if ((app_reset_handler < DEFAULT_APP_START_ADDRESS) || (app_reset_handler > DEFAULT_APP_SECTOR1_END_ADDRESS))
    {
         //invalid reset
         return 1;
    }
    //2.注销boot loader程序
    NVIC_DisableIRQ(EXTI0_IRQn);//KEY2在PA0，对应EXTI线0
    NVIC_DisableIRQ(EXTI15_10_IRQn);//KEY1在PC13，对应EXTI线13
    NVIC_DisableIRQ(USART1_IRQn);
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    HAL_DeInit();
    __disable_irq();
    __set_MSP(app_stack_pointer);
    SCB->VTOR = DEFAULT_APP_START_ADDRESS;
    pFunction app_entry = (pFunction)app_reset_handler;
    app_entry();

    return 0;
}

/*
 * @brief: 跳转到CAN更新工具（Sector 2, 0x08008000）
 * @return {*}0表示跳转成功，1表示跳转失败
 */
uint8_t Int_bootloader_jump_to_can_tool(void)
{
   typedef void (*pFunction)(void);

    //1.校验CAN工具程序的合法性
   uint32_t app_stack_pointer = *(__IO uint32_t *)CAN_TOOL_START_ADDRESS;
   uint32_t app_reset_handler = *(__IO uint32_t *)(CAN_TOOL_START_ADDRESS + 4);
    //1.1校验栈顶地址，必须在SRAM范围内
    if ((app_stack_pointer < SRAM_START_ADDRESS) || (app_stack_pointer > SRAM_END_ADDRESS))
    {
         return 1;
    }
    //1.2校验复位处理函数地址，必须在Sector 2范围内
    if ((app_reset_handler < CAN_TOOL_START_ADDRESS) || (app_reset_handler > CAN_TOOL_SECTOR2_END_ADDRESS))
    {
         return 1;
    }
    //2.注销boot loader程序
    NVIC_DisableIRQ(EXTI0_IRQn);
    NVIC_DisableIRQ(EXTI15_10_IRQn);
    NVIC_DisableIRQ(USART1_IRQn);
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    HAL_DeInit();
    __disable_irq();
    __set_MSP(app_stack_pointer);
    SCB->VTOR = CAN_TOOL_START_ADDRESS;
    pFunction app_entry = (pFunction)app_reset_handler;
    app_entry();

    return 0;
}

