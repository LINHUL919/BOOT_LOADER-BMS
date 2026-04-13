#include "app_update.h"

//标记上位机接收的状态
AppUpdateState_t app_update_state = APP_UPDATE_WAIT_FOR_REQUEST;
//用于接收开发板发来的更新请求
CAN_MessageTypeDef can_msgs[3] = {0};
//用于记录接收到的消息数量
uint8_t msg_count = 0;

//记录拿过来的更新程序数据的长度
extern uint16_t total_length;
//记录已经发送的更新程序数据的长度
uint16_t sent_length = 0;
//数据缓冲区，can一次只能发送8个字节
uint8_t data_buffer[8] = {0};



/*
 * @brief: 初始化上位机更新程序
 */
void App_update_init(void)
{
    printf("APP_update_init\r\n");
    sent_length = 0;
    app_update_state = APP_UPDATE_WAIT_FOR_REQUEST;
    //初始化CAN接口
    CAN_Init();

    printf("APP_update_wait_for_request\r\n");
    //如果已经烧录过更新好的程序了，需要手动填写一下程序的总长度，才能发送程序
    if(total_length == 0)
    {
        printf("Please set the total length of the application before sending.\r\n");
        //演示案例
        total_length = 1024; //假设程序总长度为1024字节
    }

}


/*
 * @brief: 等待接收开发板的更新请求
 * 
 */
void App_update_wait_for_request(void)
{

   CAN_ReceiveData(can_msgs, &msg_count);
    for (uint8_t i = 0; i < msg_count; i++)
    {
        //只接收id为0的消息
        if (can_msgs[i].RxHeader.StdId == 0)
        {
            //判断是否为更新消息  "sss"
            if (strcmp((char *)can_msgs[i].data, app_update_request) == 0)
            {
                printf("Received app update request\r\n");
                app_update_state = APP_UPDATE_SENDING_APP;
                //可以延时一会儿，再发送程序
                HAL_Delay(100);
            }
        }
    }
}

static uint32_t App_crc_cal(uint32_t flash_addr, uint16_t length)
{ 
    //将FLASH地址转换为32位数据指针
    uint32_t *data_32 = (uint32_t *)flash_addr;
     uint16_t length_32 = (length + 3) / 4; //计算32位数据的长度，注意这里假设数据的长度是4的倍数，如果不是需要进行处理
    //复位CRC
    __HAL_CRC_DR_RESET(&hcrc);  
     //调用HAL库的CRC计算函数，计算数据的CRC值
    uint32_t crc_value = HAL_CRC_Calculate(&hcrc, data_32, length_32);
    return crc_value;
}





/*
 * @brief: 上位机发送更新程序给开发板
 * 
 */
void App_update_send_app(void)
{
    printf("App_update_send_app\r\n");
    //发送数据
    if (sent_length < total_length)
    {
        //计算本次发送长度，最多8字节
        uint16_t remaining = total_length - sent_length;
        uint8_t chunk = (remaining >= 8) ? 8 : (uint8_t)remaining;

        //从Flash读取数据到缓冲区
        for (uint8_t i = 0; i < chunk; i++)
        {
            data_buffer[i] = *(volatile uint8_t *)(APP_START_ADDRESS + sent_length + i);
        }

        //通过CAN发送
        sent_length += chunk;
        
        CAN_SendData(app_update_request_ID, data_buffer, chunk);
        //为了实现波特率匹配
        //HAL_Delay(1);

        //每发送256字节延时100ms，给接收端处理时间
        if (sent_length % 256 == 0)
        {
            HAL_Delay(100);
        }

        //全部发送完毕，回到等待状态
        if (sent_length >= total_length)
        {
            //数据发送完了，重置发送长度，可以实现重新发送
            sent_length = 0;
            app_update_state = APP_UPDATE_WAIT_FOR_REQUEST;
            
            //发送CRC校验值，给开发板验证数据完整性
            HAL_Delay(2100); //延时一下，确保之前的数据都发送完了
            uint32_t crc_value = App_crc_cal(APP_START_ADDRESS, total_length);
            memset(data_buffer, 0, sizeof(data_buffer));
            //把CRC值分成4个字节放到数据缓冲区里
            data_buffer[0] = (crc_value >> 24) & 0xFF;
            data_buffer[1] = (crc_value >> 16) & 0xFF;
            data_buffer[2] = (crc_value >> 8) & 0xFF;
            data_buffer[3] = crc_value & 0xFF;
            CAN_SendData(app_update_request_ID, data_buffer, 4);

        }

    }
    
}


/*
 * @brief: 循环程序
 *
 */
void App_update_work(void)
{
    switch (app_update_state)
    {
        case APP_UPDATE_WAIT_FOR_REQUEST:
            App_update_wait_for_request();
            break;

        case APP_UPDATE_SENDING_APP:
            App_update_send_app();
            break;

        default:
            break;
    }
}
