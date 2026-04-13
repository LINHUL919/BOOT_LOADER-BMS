#include "App_bootloader.h"
#include "string.h"
#include "stdlib.h"

// 接收用户输入的确认开始传输的缓冲区
uint8_t app_rec_start_buffer[APP_BOOTLOADER_UART_RECEIVE_BUFFER_SIZE];

uint16_t app_rec_start_length = 0; // 接收用户输入的确认开始传输的长度

// 接收数据的总长度
uint16_t app_total_length = 0;

// 记录上次接收数据的时间
extern uint32_t last_receive_time;

// 记录累计接收数据的长度
extern uint16_t total_length;

// boot loader的状态
AppBootloaderState app_bootloader_state = APP_BOOTLOADER_STATE_INIT;




/*
 * @brief: 初始化boot loader程序打印日志启动的作用
 * @return {*}
 */
void App_bootloader_init(void)
{
    printf("Bootloader started\r\n");
    printf("Waiting for data...\r\n");
    printf("Please send 'start:len' to start\r\n");
    app_bootloader_state = APP_BOOTLOADER_STATE_INIT;
}

/*
 * @brief: 等待用户传输确认
 * 如果发送的指令不对是不需要重启程序的，重新发送start指令就可以了
 */
void App_bootloader_run(void)
{
    // 使用非中断的方式接收数据，等待用户输入确认开始传输
    // 挂起等待接收，直到接收到buffer满或者接收到空闲帧，才会返回
    HAL_UARTEx_ReceiveToIdle(&huart1,
                             app_rec_start_buffer,
                             APP_BOOTLOADER_UART_RECEIVE_BUFFER_SIZE,
                             &app_rec_start_length,
                             HAL_MAX_DELAY);

    if (app_rec_start_length > 0)
    {
        // 简单校验用户输入的格式，必须以"start: "开头，后面跟一个数字表示接收数据的总长度
        char *start_str = strstr((char *)app_rec_start_buffer, "start:");
        if (start_str != NULL)
        {
            // 解析出数据长度
            uint16_t expected_length = atoi(start_str + strlen("start:"));
            if (expected_length > 0)
            {
                app_total_length = expected_length;
                printf("Start command received, expected data length: %u bytes\r\n", expected_length);
                // 修改狀態
                app_bootloader_state = APP_BOOTLOADER_STATE_RUN;
            }
            else
            {
                printf("Invalid data length\r\n");
            }
        }
        else
        {
            printf("Invalid format, please send 'start:len'\r\n");
        }
    }
    else
    {
        printf("Invalid start command\r\n");
    }
}

/*
 * @brief: 接收数据
 * @return {*}
 */
void App_bootloader_rec_data(void)
{
    // 接收完成后，修改状态，准备校验数据
    //(1)软件方式，从第一次接收到数据开始算，从空闲帧开始算等待2s，如果超过2s没有接收到数据了，认为接收完成了
    if (total_length != 0 && (HAL_GetTick() - last_receive_time) > 2000)
    {
        // 已经2秒没有接收到数据了，认为数据接收完成了
        // 修改状态，准备校验数据
        app_bootloader_state = APP_BOOTLOADER_STATE_CHECK_DATA;
    }
   
}

/*
 * @brief:已经接收完成，校验数据
 * @return {*}0表示校验成功，1表示校验失败
 */
uint8_t App_bootloader_check_data(void)
{
    // 简单校验一下接收的数据长度是否和用户输入的长度一致
    if (app_total_length == total_length)
    {
        // 说明长度一致没有问题
        printf("Data received successfully, total length: %u bytes\r\n", app_total_length);
        // 修改状态，准备跳转到应用程序
        app_bootloader_state = APP_BOOTLOADER_STATE_COMPLITE;
        return 0;
    }
    else
    {
        printf("Length mismatch, expected: %u, actual: %u\r\n", app_total_length, total_length);
        // 校验失败，回到初始状态重新等待
        app_bootloader_state = APP_BOOTLOADER_STATE_INIT;
        return 1;
    }
}



/*
 * @brief:在main方法的while循环中调用的主函数，负责整个boot loader的流程控制
 * @return {*}
 */

void App_bootloader_work(void)
{
    switch (app_bootloader_state)
    {
    case APP_BOOTLOADER_STATE_INIT:
        // 初始化状态，等待用户输入确认开始传输
        App_bootloader_run();
        break;
    case APP_BOOTLOADER_STATE_RUN:
        // 接收数据的准备工作
        // 确认要写入的flash地址，提前擦除flash,擦除一扇
        Int_bootloader_erase();
        printf("Flash erased\r\n");
        printf("Start receiving data...\r\n");
        // 修改状态，准备接收数据
        app_bootloader_state = APP_BOOTLOADER_STATE_REC_DATA;
        // 初始化超时起点为当前时刻，避免一进来就判定超时
        last_receive_time = HAL_GetTick();
        //准备接收之前，清空按键中断的标志，避免之前的按键操作影响到接收完成的判断

        Int_bootloader_rec_app();
        break;
    case APP_BOOTLOADER_STATE_REC_DATA:
        // 接收数据的过程中，主要是等待接收完成的回调函数中处理数据，所以这里不需要做什么
        App_bootloader_rec_data();
        break;
    case APP_BOOTLOADER_STATE_CHECK_DATA:
        // 接收完成后，校验数据
        if (App_bootloader_check_data())
        {
            printf("Data check failed, restarting bootloader\r\n");
            NVIC_SystemReset(); // 校验失败，重启系统回到初始状态
        }
        break;
    default:
        break;
    }
}
