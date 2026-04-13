#ifndef __INT_W24C02_H__
#define __INT_W24C02_H__


#include "i2c.h"

#define W24C02_ADDR 0xA0
#define W24C02_ADDR_READ 0xA1
#define W24C02_ADDR_WRITE 0xA0
#define W24C02_ADDR_SIZE 8
#define W24C02_PAGE_SIZE 8




//读一个字节
uint8_t W24C02_ReadByte(uint16_t addr);

//写一个字节
void W24C02_WriteByte(uint16_t addr, uint8_t data);

//读多个字节
void W24C02_ReadBytes(uint16_t addr, uint8_t* data, uint16_t size);

//写多个字节
void W24C02_WriteBytes(uint16_t addr, uint8_t* data, uint16_t size);




#endif /* __INT_W24C02_H__ */

