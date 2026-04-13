#include "int_w24c02.h"


//读一个字节
uint8_t W24C02_ReadByte(uint16_t addr)
{
    //流程介绍：
    //启动信号，发送器件地址+写，发送内存地址，重复启动信号，发送器件地址+读，读取数据，停止信号
    //(1)句柄 ，(2)设备地址 芯片固定值，(3)字节地址 数据写入位置，
    //(4)内存地址大小，(5)数据缓冲区，(6)数据大小，(7)超时时间
    uint8_t data;
    HAL_I2C_Mem_Read(&hi2c1, W24C02_ADDR, addr, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
    return data;
}


//写一个字节
void W24C02_WriteByte(uint16_t addr, uint8_t data)
{
    //流程介绍：
    //启动信号，发送器件地址+写，发送内存地址，发送数据，停止信号
    //(1)句柄 ，(2)设备地址 芯片固定值，(3)字节地址 数据写入位置，
    //(4)内存地址大小，(5)数据缓冲区，(6)数据大小，(7)超时时间
    HAL_I2C_Mem_Write(&hi2c1, W24C02_ADDR, addr, I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
}

//读多个字节
void W24C02_ReadBytes(uint16_t addr, uint8_t* data, uint16_t size)
{
    //流程介绍：
    //启动信号，发送器件地址+写，发送内存地址，重复启动信号，发送器件地址+读，读取数据，停止信号
    //(1)句柄 ，(2)设备地址 芯片固定值，(3)字节地址 数据写入位置，
    //(4)内存地址大小，(5)数据缓冲区，(6)数据大小，(7)超时时间
    HAL_I2C_Mem_Read(&hi2c1, W24C02_ADDR, addr, I2C_MEMADD_SIZE_8BIT, data, size, 100);
}

//写多个字节
//W24C02页大小为8字节，写入时必须按页对齐分段写，每页写完需等待5ms写入周期
void W24C02_WriteBytes(uint16_t addr, uint8_t* data, uint16_t size)
{
    uint16_t offset = 0;
    while (offset < size)
    {
        //计算当前页剩余可写字节数
        uint16_t page_remain = W24C02_PAGE_SIZE - ((addr + offset) % W24C02_PAGE_SIZE);
        //本次实际写入长度：取剩余数据和页剩余的较小值
        uint16_t write_len = (size - offset) < page_remain ? (size - offset) : page_remain;
        HAL_I2C_Mem_Write(&hi2c1, W24C02_ADDR, addr + offset, I2C_MEMADD_SIZE_8BIT, data + offset, write_len, 100);
        offset += write_len;
        HAL_Delay(5); //等待EEPROM完成本页写入
    }
}





