#include "App_bootloader.h"

uint8_t App_bootlaoder_status = BOOTLOADER_FLAG_NO_UPDATE; //默认不需要更新



/*
 * @brief: 判断当前是否需要更新
 * @return {*}
 */
void App_bootloader_NeedUpdate(void)
{
    printf("BL Start\r\n");
   //读取EEPROM中bootloader标志和校验密钥 3个字节的数据，一个标志位和两个字节的校验密钥
   uint8_t data[3];
   W24C02_ReadBytes(BOOTLOADER_FLAG_ADDR, data, 3);
   //先校验验密钥是否正确，再判断标志位
   uint16_t key = (data[1] << 8) | data[2]; //将两个字节的校验密钥合并成一个16位整数,高字节在前，低字节在后
   if (key != BOOTLOADER_KEY_VALUE) //校验密钥错误
   {
    //重置密钥和标志位，防止误判
    uint8_t reset_data[3] = {BOOTLOADER_FLAG_NO_UPDATE, (BOOTLOADER_KEY_VALUE >> 8) & 0xFF, BOOTLOADER_KEY_VALUE & 0xFF}; //重置标志位和校验密钥
    W24C02_WriteBytes(BOOTLOADER_FLAG_ADDR, reset_data, 3);
    HAL_Delay(5); //等待EEPROM完成写入操作
   }
    //校验密钥正确，判断标志位
    if (data[0] == BOOTLOADER_FLAG_VALUE)
    {
        App_bootlaoder_status = BOOTLOADER_FLAG_VALUE; //需要更新
    }
    else
    {
      //no update
    }    
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == KEY1_Pin)
  {
    App_bootlaoder_status = BOOTLOADER_FLAG_FACTORY_RESET; //按键按下，设置为需要恢复出厂设置
  }
  else if (GPIO_Pin == KEY2_Pin)
  {
    App_bootlaoder_status = BOOTLOADER_FLAG_CAN_UPDATE; //KEY2按下，进入CAN更新工具模式
  }
}

//检查是否需要恢复出厂设置
void App_bootloader_check_factory_reset(void)
{
    HAL_Delay(5000); 

}


uint8_t metadata_buffer[16]; //假设元数据信息不超过16字节
uint8_t flash_rw_buffer[256]; //从外置Flash读取数据的缓冲区，对齐W25Q128页大小(256B)

//校验的封装，返回0表示校验通过，1表示校验失败
static uint8_t App_bootloader_check_update_condition(void)
{
    //读取元数据信息，描述后续的程序

    W25Q128_ReadData(METADATA_ADDRESS_BLOCK_ID,
                      METADATA_ADDRESS_SECTOR_ID,
                      METADATA_ADDRESS_PAGE_ID,
                      METADATA_ADDRESS_ADDR,
                      metadata_buffer,
                      8);//读取前8字节的元数据信息，包含块ID、扇区ID、页ID和页内地址等信息

    //校验元数据信息的合法性
    //前四个字节是程序的起始地址，后四个字节是程序的长度
    uint32_t program_start_address = (metadata_buffer[0] << 24) | (metadata_buffer[1] << 16) |
                                     (metadata_buffer[2] << 8) |
                                     metadata_buffer[3];
    uint32_t program_length = (metadata_buffer[4] << 24) | (metadata_buffer[5] << 16) |
                              (metadata_buffer[6] << 8) |
                              metadata_buffer[7];
    //假设程序的起始地址不能在第一扇中0x001000
    if (program_start_address < PROGRAM_START_ADDRESS_MIN || program_start_address > PROGRAM_START_ADDRESS_MAX)
    {
        printf("Invalid addr\r\n");
        return 1;
    }
    //假设程序的长度不能小于500字节，也不能大于app区的最大长度16KB
    if (program_length < PROGRAM_LENGTH_MIN || program_length > PROGRAM_LENGTH_MAX)
    {
        printf("Invalid len\r\n");
        return 1;
    }
    return 0; //校验通过
}

//擦除内置的APP区，方便我后续将从外置的FLASH中读取的信息写入到内置的APP区中
//芯片型号是STM32F407ZGT6，内置的APP区是从0x0800C000到0x0800FFFF，全16KB,也就是扇区3
static void App_bootloader_erase_internal_flash_app_area(void)
{
    HAL_FLASH_Unlock(); //解锁Flash

    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0; //用于存储擦除错误的扇区号

    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS; //按扇区擦除
    erase_init.Sector = FLASH_SECTOR_3;             //擦除扇区3
    erase_init.NbSectors = 1;                       //擦除1个扇区
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;//电压范围2.7V~3.6V

    if (HAL_FLASHEx_Erase(&erase_init, &sector_error) != HAL_OK)
    {
        printf("Erase err\r\n");
    }

    HAL_FLASH_Lock(); //重新锁定Flash
}




//静态获取外置FLASH中的升级程序，并将其写入到STM32的Flash中
static void App_bootloader_update_from_external_flash(void)
{
    //1校验
    if (App_bootloader_check_update_condition() != 0)
    {
        printf("Check fail\r\n");
        return; //校验失败，不擦除不写入，直接返回
    }

    //从元数据中取出程序起始地址和长度
    uint32_t program_start_address = (metadata_buffer[0] << 24) | (metadata_buffer[1] << 16) |
                                     (metadata_buffer[2] << 8)  | metadata_buffer[3];
    uint32_t program_length = (metadata_buffer[4] << 24) | (metadata_buffer[5] << 16) |
                              (metadata_buffer[6] << 8)  | metadata_buffer[7];

    //2读取程序数据，并写入到STM32的Flash中
    //2.1擦除内置的APP区
    App_bootloader_erase_internal_flash_app_area();

    //2.2从外置Flash逐页读取程序数据，通过缓冲区写入内置Flash APP区
    HAL_FLASH_Unlock();

    uint32_t bytes_written = 0;      //已写入的字节数
    uint32_t flash_dest = APP_UPDATE_START_ADDRESS; //内置Flash写入目标地址（Sector 3: 0x0800C000）

    while (bytes_written < program_length)
    {
        //计算本次需要读取的字节数（最后一次可能不足256字节）
        uint16_t read_size = 256;
        if (program_length - bytes_written < 256)
        {
            read_size = (uint16_t)(program_length - bytes_written);
        }

        //将外置Flash的24位地址拆解为 block/sector/page/addr
        uint32_t ext_addr = program_start_address + bytes_written;
        uint8_t block_id  = (uint8_t)((ext_addr >> 16) & 0xFF); //高8位 = 块ID
        uint8_t sector_id = (uint8_t)((ext_addr >> 12) & 0x0F); //次4位 = 扇区ID
        uint8_t page_id   = (uint8_t)((ext_addr >> 8)  & 0x0F); //再4位 = 页ID
        uint8_t addr      = (uint8_t)(ext_addr & 0xFF);         //低8位 = 页内偏移

        //读取前先将缓冲区全部置为0xFF，这样最后一次不足256字节时尾部自动为0xFF，与Flash擦除态一致
        memset(flash_rw_buffer, 0xFF, sizeof(flash_rw_buffer));

        //从外置Flash读取一页数据到缓冲区
        W25Q128_ReadData(block_id, sector_id, page_id, addr, flash_rw_buffer, read_size);

        //将read_size向上对齐到4字节，确保完整写入
        uint16_t write_size = (read_size + 3) & ~3;

        //将缓冲区数据按4字节(word)写入内置Flash
        for (uint16_t i = 0; i < write_size; i += 4)
        {
            //小端序组装：buffer[0]在低位，buffer[3]在高位
            uint32_t word = (uint32_t)flash_rw_buffer[i]
                          | ((uint32_t)flash_rw_buffer[i + 1] << 8)
                          | ((uint32_t)flash_rw_buffer[i + 2] << 16)
                          | ((uint32_t)flash_rw_buffer[i + 3] << 24);

            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_dest, word) != HAL_OK)
            {
                printf("Write err\r\n");
                HAL_FLASH_Lock();
                return;
            }
            flash_dest += 4;
        }

        bytes_written += read_size;
    }

    HAL_FLASH_Lock();
    printf("Update OK\r\n");
}






//执行更新流程
void App_bootloader_Update(void)
{
    if(App_bootlaoder_status == BOOTLOADER_FLAG_VALUE)
    {
            //擦除更新标志位，防止重复更新  
            W24C02_WriteByte(BOOTLOADER_FLAG_ADDR,BOOTLOADER_FLAG_NO_UPDATE);
            HAL_Delay(5); //等待EEPROM完成写入操作  
        printf("Updating...\r\n");
        //执行更新流程-->将W25Q128的程序写入到STM32的Flash中
        App_bootloader_update_from_external_flash();
        //更新完成后，重置EEPROM中的标志位和校验密钥，防止重复更新
    }
    else if (App_bootlaoder_status == BOOTLOADER_FLAG_FACTORY_RESET)
    {
        printf("Factory reset\r\n");
        //擦除Sector 3用户程序区域，恢复出厂后将跳转Sector 1默认程序
        App_bootloader_erase_internal_flash_app_area();
        //重置EEPROM中的标志位和校验密钥，防止重复恢复出厂设置
        uint8_t reset_data[3] = {BOOTLOADER_FLAG_NO_UPDATE, (BOOTLOADER_KEY_VALUE >> 8) & 0xFF, BOOTLOADER_KEY_VALUE & 0xFF};
        W24C02_WriteBytes(BOOTLOADER_FLAG_ADDR, reset_data, 3);
        HAL_Delay(5); //等待EEPROM完成写入操作
        printf("Factory reset OK\r\n");
    }
    else if (App_bootlaoder_status == BOOTLOADER_FLAG_CAN_UPDATE)
    {
        printf("CAN update mode\r\n");
        //直接跳转到CAN更新工具程序（Sector 2）
    }
    else
    {
        //不需要更新，直接跳转到应用程序
        printf("Jump APP\r\n");
        App_bootloader_JumpToApp();
    }
}


//执行跳转到应用程序的流程
void App_bootloader_JumpToApp(void)
{
    if(App_bootlaoder_status == BOOTLOADER_FLAG_FACTORY_RESET)
    {
        //跳转到出厂设置的默认程序 Sector 1 (0x08004000)
        Int_bootloader_jump_to_default_app();
    }
    else if(App_bootlaoder_status == BOOTLOADER_FLAG_CAN_UPDATE)
    {
        //跳转到CAN更新工具程序 Sector 2 (0x08008000)
        Int_bootloader_jump_to_can_tool();
    }
    else
    {
        //跳转到正常的应用程序 Sector 3 (0x0800C000)
        Int_bootloader_jump_to_app();
    }
}






