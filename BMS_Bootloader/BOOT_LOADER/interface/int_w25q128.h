#ifndef __INT_W25Q128_H__
#define __INT_W25Q128_H__

#include "main.h"
#include "spi.h"

//SPI设备在使用的时候才需要拉低片选引脚，使用完后需要拉高片选引脚
//拉低片选引脚
void W25Q128_Start(void);

//拉高片选引脚
void W25Q128_Stop(void);

//写一个字节
void W25Q128_WriteByte(uint8_t data);

//读一个字节
uint8_t W25Q128_ReadByte(void);


//测试方法：读取ID，W25Q128的ID为0xEF4018
void W25Q128_ReadID(uint8_t* mf_id, uint16_t *dev_id);


/*
 * @brief: 读取数据
 * @param {uint8_t} block_ID: 块ID:W25Q128的块大小为64KB，共16块，block_ID取值范围0~15
 * @param {uint8_t} sector_ID: 扇区ID:W25Q128的扇区大小为4KB，每块16个扇区，sector_ID取值范围0~15
 * @param {uint8_t} page_ID: 页ID:W25Q128的页大小为256B，每扇区16页，page_ID取值范围0~15
 * @param {uint8_t} addr: 页内地址: 页内地址范围为0~255
 * @param {uint8_t*} data: 数据缓冲区
 * @param {uint16_t} size: 数据长度
 */
void W25Q128_ReadData(uint8_t block_ID,uint8_t sector_ID,uint8_t page_ID,uint8_t addr,uint8_t* data,uint16_t size);


/*
 * @brief: 写入数据
 * @param {uint8_t} block_ID: 块ID:W25Q128的块大小为64KB，共16块，block_ID取值范围0~15
 * @param {uint8_t} sector_ID: 扇区ID:W25Q128的扇区大小为4KB，每块16个扇区，sector_ID取值范围0~15
 * @param {uint8_t} page_ID: 页ID:W25Q128的页大小为256B，每扇区16页，page_ID取值范围0~15
 * @param {uint8_t} addr: 页内地址: 页内地址范围为0~255
 * @param {uint8_t*} data: 数据缓冲区
 * @param {uint16_t} size: 数据长度
 */
void W25Q128_WriteData(uint8_t block_ID,uint8_t sector_ID,uint8_t page_ID,uint8_t addr,uint8_t* data,uint16_t size);

/*
 * @brief: 擦除扇区
 * @param {uint8_t} block_ID: 块ID:W25Q128的块大小为64KB，共16块，block_ID取值范围0~15
 * @param {uint8_t} sector_ID: 扇区ID:W25Q128的扇区大小为4KB，每块16个扇区，sector_ID取值范围0~15
 * @return {*}
 */
void W25Q128_EraseSector(uint8_t block_ID,uint8_t sector_ID);

#endif /* __INT_W25Q128_H__ */

