#ifndef    __APP_BOOTLOADER_H
#define    __APP_BOOTLOADER_H  


#include "int_bootloader.h"


typedef enum
{
    //初始化状态，等待用户输入确认开始传输
    APP_BOOTLOADER_STATE_INIT,
    //等待数据
    APP_BOOTLOADER_STATE_RUN,
    //接收数据
    APP_BOOTLOADER_STATE_REC_DATA,
    //校验数据
    APP_BOOTLOADER_STATE_CHECK_DATA,
    //跳转到应用程序
    APP_BOOTLOADER_STATE_COMPLITE,

} AppBootloaderState;


#define APP_BOOTLOADER_UART_RECEIVE_BUFFER_SIZE 64

/*
 * @brief: 初始化boot loader程序打印日志启动的作用
 * @return {*}
 */
void App_bootloader_init(void);

/*
 * @brief: 等待用户传输确认
 * @return {*}
 */
void App_bootloader_run(void);


/*
 * @brief: 接收数据
 * @return {*}
 */
void App_bootloader_rec_data(void);


/*
 * @brief:已经接收完成，校验数据 
 * @return {*}0表示校验成功，1表示校验失败
 */
uint8_t App_bootloader_check_data(void);


/*
 * @brief:在main方法的while循环中调用的主函数，负责整个boot loader的流程控制 
 * @return {*}
 */
void App_bootloader_work(void);

#endif /* __APP_BOOTLOADER_H */



