#include "Int_can.h"
#include <stdio.h>

/*
 * @brief: 初始化CAN2模块
 * 配置白名单过滤器 手动开启CAn2接收功能
 */
void CAN_Init(void)
{

    //配置白名单过滤器
    CAN_FilterTypeDef sFilterConfig;
    sFilterConfig.FilterBank = 0; //使用第0个过滤器
    sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK; //掩码模式
    sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT; //32位过滤器
    sFilterConfig.FilterIdHigh = 0x0000; //过滤器ID高16位
    sFilterConfig.FilterIdLow = 0x0000; //过滤器ID低16位
    sFilterConfig.FilterMaskIdHigh = 0x0000; //过滤器掩码高16位
    sFilterConfig.FilterMaskIdLow = 0x0000; //过滤器掩码低16位
    sFilterConfig.FilterFIFOAssignment = CAN_FILTER_FIFO0; //分配到FIFO0
    sFilterConfig.FilterActivation = ENABLE; //激活过滤器
    sFilterConfig.SlaveStartFilterBank = 14; //从第14个过滤器开始分配给从CAN实例
    if (HAL_CAN_ConfigFilter(&hcan, &sFilterConfig) != HAL_OK)
    {
        Error_Handler();
    }

    //启动CAN2外设（必须在过滤器配置之后、收发之前调用）
    if (HAL_CAN_Start(&hcan) != HAL_OK)
    {
        Error_Handler();
    }
}

/*
 * @brief: 发送CAN数据
 * @param {uint8_t} *data: 数据指针
 * @param {uint8_t} length: 数据长度
 * @param {uint16_t} id: CAN报文ID
 * 注意：数据长度不能超过8字节
 * 这里我主要使用数据帧就行了不用远程帧
 */
void CAN_SendData(uint16_t id, uint8_t *data, uint8_t length)
{ 
    //等待发送邮箱空闲,为了保证发送数据的顺序，等待所有邮箱都空闲
    while (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) < 3);


    CAN_TxHeaderTypeDef TxHeader;
    TxHeader.StdId = id; //标准ID
    TxHeader.RTR = CAN_RTR_DATA; //数据帧
    TxHeader.IDE = CAN_ID_STD; //使用标准ID
    TxHeader.DLC = length; //数据长度
    uint32_t TxMailbox;

    HAL_CAN_AddTxMessage(&hcan, &TxHeader, data, &TxMailbox);

}


/*
 * @brief: 接收CAN数据
 * @param {CAN_MessageTypeDef} *rec_msg: 数组指针 最多一次可以获取到3条CAN报文
 * @param {uint8_t} *msg_count: 接收到的CAN报文数量
 * @return {*}
 */
void CAN_ReceiveData(CAN_MessageTypeDef *rec_msg,uint8_t *msg_count)
{
    *msg_count = HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0);
     for (uint8_t i = 0;i < *msg_count;i++ )
     {
        //指针指向当前缓存
        CAN_MessageTypeDef *current_msg = &rec_msg[i];
        memset(current_msg, 0, sizeof(CAN_MessageTypeDef));
        //获取当前缓存的CAN报文
        HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &(current_msg->RxHeader), current_msg->data);
     }
}



