#include "int_w25q128.h"


//SPI设备在使用的时候才需要拉低片选引脚，使用完后需要拉高片选引脚
//拉低片选引脚
void W25Q128_Start(void)
{
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_6, GPIO_PIN_RESET); //假设片选引脚连接在PG6
}

//拉高片选引脚
void W25Q128_Stop(void)
{
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_6, GPIO_PIN_SET); //假设片选引脚连接在PG6
}



//写一个字节
void W25Q128_WriteByte(uint8_t data)
{

    //SPI发送数据的流程：先拉低片选引脚，发送数据，最后拉高片选引脚
    HAL_SPI_Transmit(&hspi1, &data, 1, 100); //假设使用hspi1，发送一个字节，超时时间100ms


}

//读一个字节
uint8_t W25Q128_ReadByte(void)
{
    uint8_t data;
    
    //SPI接收数据的流程：先拉低片选引脚，接收数据，最后拉高片选引脚
    HAL_SPI_Receive(&hspi1, &data, 1, 100); //假设使用hspi1，接收一个字节，超时时间100ms
    return data;
}

//测试方法：读取ID，W25Q128的ID为0xEF4018
//注意：整个读ID过程必须在同一个片选周期内完成
void W25Q128_ReadID(uint8_t* mf_id, uint16_t *dev_id)
{
    W25Q128_Start(); //拉低片选
    W25Q128_WriteByte(0x9F); //发送JEDEC ID命令
    *mf_id = W25Q128_ReadByte();              //制造商ID (应为0xEF)
    uint8_t dev_id_high = W25Q128_ReadByte(); //设备ID高字节
    uint8_t dev_id_low = W25Q128_ReadByte();  //设备ID低字节
    *dev_id = (dev_id_high << 8) | dev_id_low; //合成设备ID (应为0x4018)
    W25Q128_Stop();  //拉高片选
}

//静态方法，等待芯片忙碌完成，W25Q128在执行写入或擦除操作时会进入忙碌状态，此时无法接受新的命令
static void W25Q128_WaitBusy(void)
{
    W25Q128_Start(); //拉低片选
    W25Q128_WriteByte(0x05); //发送读取状态寄存器命令
    uint8_t status;
    do {
        status = W25Q128_ReadByte(); //读取状态寄存器值
    } while (status & 0x01); //检查忙碌位（最低位），如果为1则继续等待
    W25Q128_Stop(); //拉高片选
}

/*
 * @brief: 读取数据
 * @param {uint8_t} block_ID: 块ID:W25Q128的块大小为64KB，共16块，block_ID取值范围0~15
 * @param {uint8_t} sector_ID: 扇区ID:W25Q128的扇区大小为4KB，每块16个扇区，sector_ID取值范围0~15
 * @param {uint8_t} page_ID: 页ID:W25Q128的页大小为256B，每扇区16页，page_ID取值范围0~15
 * @param {uint8_t} addr: 写入的地址
 * @param {uint8_t*} data: 数据缓冲区
 * @param {uint16_t} size: 数据长度
 */
void W25Q128_ReadData(uint8_t block_ID,
                      uint8_t sector_ID,
                      uint8_t page_ID,
                      uint8_t addr,
                      uint8_t* data,
                      uint16_t size)
                      {
                        //等待芯片空闲
                        W25Q128_WaitBusy();
                        //拉低片选
                        W25Q128_Start();
                        //读取流程：发送读取命令，发送地址，接收数据，拉高片选
                        W25Q128_WriteByte(0x03); //发送读取数据命令
                        //计算24位地址：块ID占8位，扇区ID占4位，页ID占4位，页内地址占8位
                        uint32_t address = ((block_ID & 0xFF) << 16) | ((sector_ID & 0x0F) << 12) | ((page_ID & 0x0F) << 8) | (addr & 0xFF);
                        //发送24位地址，分三次发送，每次8位
                        W25Q128_WriteByte((address >> 16) & 0xFF); //地址高8位
                        W25Q128_WriteByte((address >> 8) & 0xFF);  //地址中8位
                        W25Q128_WriteByte(address & 0xFF);         //地址低8位
                        //接收数据
                        for (uint16_t i = 0; i < size; i++) {
                            data[i] = W25Q128_ReadByte();
                        }
                        //拉高片选
                        W25Q128_Stop();

                      }

//使用32位地址的读取数据接口
void W25Q128_ReadData32(uint32_t address, uint8_t* data, uint16_t size)
{
    //等待芯片空闲
    W25Q128_WaitBusy();
    //拉低片选
    W25Q128_Start();
    //读取流程：发送读取命令，发送地址，接收数据，拉高片选
    W25Q128_WriteByte(0x03); //发送读取数据命令
    //发送32位地址，分四次发送，每次8位
    W25Q128_WriteByte((address >> 24) & 0xFF); //地址最高8位
    W25Q128_WriteByte((address >> 16) & 0xFF); //地址高8位
    W25Q128_WriteByte((address >> 8) & 0xFF);  //地址中8位
    W25Q128_WriteByte(address & 0xFF);         //地址低8位
    //接收数据
    for (uint16_t i = 0; i < size; i++) {
        data[i] = W25Q128_ReadByte();
    }
    //拉高片选
    W25Q128_Stop();
}


/*
 * @brief: 写入数据
 * @param {uint8_t} block_ID: 块ID:W25Q128的块大小为64KB，共16块，block_ID取值范围0~15
 * @param {uint8_t} sector_ID: 扇区ID:W25Q128的扇区大小为4KB，每块16个扇区，sector_ID取值范围0~15
 * @param {uint8_t} page_ID: 页ID:W25Q128的页大小为256B，每扇区16页，page_ID取值范围0~15
 * @param {uint8_t} addr: 页内地址: 页内地址范围为0~255
 * @param {uint8_t*} data: 数据缓冲区
 * @param {uint16_t} size: 数据长度
 */
void W25Q128_WriteData(
    uint8_t block_ID,
    uint8_t sector_ID,
    uint8_t page_ID,
    uint8_t addr,
    uint8_t* data,
    uint16_t size)
    {
        //写入流程：等待芯片空闲，拉低片选，发送写使能命令，拉高片选，等待芯片空闲，拉低片选，发送页编程命令，发送地址，发送数据，拉高片选
        W25Q128_WaitBusy(); //等待芯片空闲
        W25Q128_Start(); //拉低片选
        W25Q128_WriteByte(0x06); //发送写使能命令
        W25Q128_Stop(); //拉高片选

        W25Q128_WaitBusy(); //等待芯片空闲
        W25Q128_Start(); //拉低片选
        W25Q128_WriteByte(0x02); //发送页编程命令
        //计算24位地址：块ID占8位，扇区ID占4位，页ID占4位，页内地址占8位
        uint32_t address = ((block_ID & 0xFF) << 16) | ((sector_ID & 0x0F) << 12) | ((page_ID & 0x0F) << 8) | (addr & 0xFF);
        //发送24位地址，分三次发送，每次8位
        W25Q128_WriteByte((address >> 16) & 0xFF); //地址高8位
        W25Q128_WriteByte((address >> 8) & 0xFF);  //地址中8位
        W25Q128_WriteByte(address & 0xFF);         //地址低8位
        //发送数据
        for (uint16_t i = 0; i < size; i++) {
            W25Q128_WriteByte(data[i]);
        }
        W25Q128_Stop(); //拉高片选
    }

/*
 * @brief: 擦除扇区
 * @param {uint8_t} block_ID: 块ID:W25Q128的块大小为64KB，共16块，block_ID取值范围0~15
 * @param {uint8_t} sector_ID: 扇区ID:W25Q128的扇区大小为4KB，每块16个扇区，sector_ID取值范围0~15
 * @return {*}
 */
void W25Q128_EraseSector(uint8_t block_ID,uint8_t sector_ID)
{
    //擦除流程：等待芯片空闲，拉低片选，发送写使能命令，拉高片选，等待芯片空闲，拉低片选，发送扇区擦除命令，发送地址，拉高片选
    W25Q128_WaitBusy(); //等待芯片空闲
    W25Q128_Start(); //拉低片选
    W25Q128_WriteByte(0x06); //发送写使能命令
    W25Q128_Stop(); //拉高片选

    W25Q128_WaitBusy(); //等待芯片空闲
    W25Q128_Start(); //拉低片选
    W25Q128_WriteByte(0x20); //发送扇区擦除命令
    //计算24位地址：块ID占8位，扇区ID占4位，页ID和页内地址为0，因为擦除是以扇区为单位的
    uint32_t address = ((block_ID & 0xFF) << 16) | ((sector_ID & 0x0F) << 12);
    //发送24位地址，分三次发送，每次8位
    W25Q128_WriteByte((address >> 16) & 0xFF); //地址高8位
    W25Q128_WriteByte((address >> 8) & 0xFF);  //地址中8位
    W25Q128_WriteByte(address & 0xFF);         //地址低8位
    W25Q128_Stop(); //拉高片选
} 




