#ifndef __INT_CAN_H
#define __INT_CAN_H

#include "can.h"
#include "string.h"

typedef struct
{
    CAN_RxHeaderTypeDef RxHeader; //CAN报文头
    uint8_t data[8]; //CAN报文数据

} CAN_MessageTypeDef;


/*
 * @brief: 初始化CAN2模块
 * 配置白名单过滤器  手动开启CAN2接收功能
 */
void CAN_Init(void);

/*
 * @brief: 发送CAN2数据
 * @param {uint8_t} *data: 数据指针
 * @param {uint8_t} length: 数据长度
 * @param {uint16_t} id: CAN报文ID
 * 注意：数据长度不能超过8字节
 * 这里我主要使用数据帧就行了不用远程帧
 */
void CAN_SendData(uint16_t id, uint8_t *data, uint8_t length);




/*
 * @brief: 接收CAN2数据
 * @param {CAN_MessageTypeDef} *rec_msg: 数组指针 最多一次可以获取到3条CAN报文
 * @param {uint8_t} *msg_count: 接收到的CAN报文数量
 * @return {*}
 */
void CAN_ReceiveData(CAN_MessageTypeDef *rec_msg,uint8_t *msg_count);






#endif /* End of file */





