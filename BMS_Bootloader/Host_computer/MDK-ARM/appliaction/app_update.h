#ifndef _APP_UPDATE_H_
#define _APP_UPDATE_H_

#include "Int_can.h"
#include "usart.h"
#include "int_bootloader.h"
#include "crc.h"


#define  app_update_request  "sss"
#define  app_update_request_ID 1

typedef enum
{
    //等待指令
     APP_UPDATE_WAIT_FOR_REQUEST,

    //发送app
    APP_UPDATE_SENDING_APP

} AppUpdateState_t;


/*
 * @brief: 初始化上位机更新程序
 */
void App_update_init(void);



/*
 * @brief: 等待接收开发板的更新请求
 * 
 */
void App_update_wait_for_request(void);



/*
 * @brief: 上位机发送更新程序给开发板
 * 
 */
void App_update_send_app(void);


/*
 * @brief: 循环程序
 *
 */
void App_update_work(void);


#endif /* _APP_UPDATE_H_ */
