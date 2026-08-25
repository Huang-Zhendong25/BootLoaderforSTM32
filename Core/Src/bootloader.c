#include "bootloader.h"
#include <string.h>
#include "usart.h"
#include "iwdg.h"

static uint32_t cur_write_addr = APP_FLASH_STARTADDR;
static uint32_t cur_total_len = 0;
static uint8_t rx_buffer[RX_BUFFER_SIZE];

static void Flash_EraseApp(void)
{
    uint32_t page_start = (APP_FLASH_STARTADDR / 1024) * 1024;
    uint32_t page_end = ((APP_FLASH_STARTADDR + APP_MAX_SIZE - 1) / 1024) * 1024;

    FLASH_EraseInitTypeDef erase_init = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .PageAddress = page_start,
        .NbPages = (page_start - page_end) / 1024 + 1
    };
    uint32_t erase_error = 0;
    HAL_FLASH_Unlock();
    HAL_FLASHEx_Erase(&erase_init, &erase_error);
    HAL_FLASH_Lock();
}

static bool Flash_WriteBuffer(uint32_t addr, uint8_t *data, uint32_t len)
{
    uint32_t word_count = len / 4, word_count_remain = len % 4;

    HAL_FLASH_Unlock();
    for (uint32_t i = 0; i < word_count; i++)
    {
        uint32_t word = *(uint32_t *)(data + i * 4);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + i * 4, word) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return false;
        }
    }
    if (word_count_remain > 0)
    {
        uint32_t last_word = 0xffffffff;
        memcpy(&last_word, data + word_count * 4, word_count_remain);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + word_count * 4, last_word) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return false;
        }
    }
    HAL_FLASH_Lock();
    return true;
}

static void Bootloader_SendResponse(char *respText)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)respText, strlen(respText), HAL_MAX_DELAY);
}

static bool Bootloader_ReceiveData(uint8_t *data, uint16_t len, uint32_t timeout)
{
    uint32_t tick_start = HAL_GetTick();
    uint16_t received_len = 0;

    while (received_len < len)
    {
        HAL_IWDG_Refresh(&hiwdg);
        if (HAL_GetTick() - tick_start > timeout)
        {
            return false;
        }
        if (HAL_UART_Receive(&huart2, &data[received_len], 1, 10) == HAL_OK)
        {
            received_len += 1;
            tick_start = HAL_GetTick();
        }
    }
    return true;
}

static uint32_t Bootloader_CRC32(uint8_t *data, uint32_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

static bool isValid(void)
{
    uint32_t app_stack_top = *(__IO uint32_t *)APP_FLASH_STARTADDR;
    uint32_t app_resetHandler = *(__IO uint32_t *)(APP_FLASH_STARTADDR + 4);

    if ((app_stack_top & 0x2ffe0000u) != 0x20000000u)
    {
        return false;
    }
//    if ((app_resetHandler < 0x80000000u) || (app_resetHandler > 0x080fffffu))
//    {
//        return false;
//    }
    return true;
}

void Bootloader_JumpToApp(void)
{
    if (!isValid())
        return;
    
    uint32_t app_resetHandler = *(__IO uint32_t *)(APP_FLASH_STARTADDR + 4);
    uint32_t app_stack_top = *(__IO uint32_t *)APP_FLASH_STARTADDR;

    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    for (uint8_t i = 0; i < 8; i++)
    {
        NVIC->ICER[i] = 0xffffffff;
        NVIC->ICPR[i] = 0xffffffff;
    }
    __enable_irq();
    HAL_UART_DeInit(&huart2);
    SCB->VTOR = APP_FLASH_STARTADDR;
    __set_MSP(app_stack_top);
    __set_CONTROL(0);

    typedef void (*pFunction)(void);
    pFunction jump_func = (pFunction)app_resetHandler;
    jump_func();

    while (1)
    {
        NVIC_SystemReset();
    }
}

static bool Bootloader_Upgrade(void)
{
    uint8_t cmd;
    uint8_t data_len;    //maxlen = 256bytes
    uint32_t host_crc, host_size;
    uint32_t calc_crc;

    bool data_received = false;
    uint32_t start_tick;

    LED_ON;
    cur_write_addr = APP_FLASH_STARTADDR;
    cur_total_len = 0;

    Flash_EraseApp();
    start_tick = HAL_GetTick();

    while (1)
    {
        HAL_IWDG_Refresh(&hiwdg);

        if (!data_received && (HAL_GetTick() - start_tick > UPGRADE_WAIT_TIMEOUT_MS))
        {
            LED_OFF;
            return false;
        }

        if (HAL_UART_Receive(&huart2, &cmd, 1, 100) != HAL_OK)
        {
            continue;
        }
        LED_ON;

        HAL_Delay(2000);

        data_received = true;

        if (cmd == CMD_DATA)
        {
            if (!Bootloader_ReceiveData(&data_len, 1, 100))
            {
                Bootloader_SendResponse(RESP_LEN_FAIL);
                continue;
            }
            if (data_len == 0 || data_len > RX_BUFFER_SIZE || (data_len % 4))
            {
                Bootloader_SendResponse(RESP_LEN_ERR);
                continue;
            }
            if (!Bootloader_ReceiveData(rx_buffer, data_len, 500))
            {
                Bootloader_SendResponse(RESP_DATA_FAIL);
                continue;
            }
            if (!Flash_WriteBuffer(cur_write_addr, rx_buffer, data_len))
            {
                Bootloader_SendResponse(RESP_WRITE_FAIL);
                continue;
            }
            cur_write_addr += data_len;
            cur_total_len += data_len;
            Bootloader_SendResponse(RESP_OK);
        }
        else if (cmd == CMD_END)
        {
            if (!Bootloader_ReceiveData((uint8_t *)&host_crc, 4, 100))
            {
                Bootloader_SendResponse(RESP_CRC_FAIL);
                return false;
            }
            if (!Bootloader_ReceiveData((uint8_t *)&host_size, 4, 100))
            {
                Bootloader_SendResponse(RESP_SIZE_FAIL);
                return false;
            }
            if (cur_total_len != host_size)
            {
                Bootloader_SendResponse(RESP_SIZE_MISMATCH);
                return false;
            }
            calc_crc = Bootloader_CRC32((uint8_t *)APP_FLASH_STARTADDR, host_size);
            if (calc_crc == host_crc)
            {
                Bootloader_SendResponse(RESP_DONE);
                LED_OFF;

                uint32_t clear = 0xffffffff;
                Flash_WriteBuffer(UPGRADE_FLAG_ADDR, (uint8_t *)&clear, 4);
                
                HAL_Delay(100);
                Bootloader_JumpToApp();

                return true;
            }
            else
            {
                Bootloader_SendResponse(RESP_FAIL);
                return false;
            }
        }
        else if (cmd == CMD_ABORT)
        {
            Bootloader_SendResponse(RESP_OK);
            LED_OFF;
            return false;
        }
    }
}

void Bootloader_MainLoop(void)
{
    LED_OFF;

    if (*(__IO uint32_t *)UPGRADE_FLAG_ADDR == UPGRADE_FLAGE_MAGIC)
    {
        uint32_t clear = 0xffffffff;
        Flash_WriteBuffer(UPGRADE_FLAG_ADDR, (uint8_t *)clear, 4);

        LED_ON;
        HAL_Delay(2000);
        
        if (Bootloader_Upgrade())
        {
            if (isValid())
            {
                HAL_Delay(100);
                Bootloader_JumpToApp();
            }
        }
    }
    
    if (isValid())
    {
        HAL_Delay(100);
        Bootloader_JumpToApp();
    }

    LED_ON;
    while (1)
    {
        HAL_IWDG_Refresh(&hiwdg);
        
        LED_ON;
        HAL_Delay(2000);

        uint8_t cmd;
        if (HAL_UART_Receive(&huart2, &cmd, 1, 100) == HAL_OK)
        {
            if (cmd == CMD_DATA)
            {
                if (Bootloader_Upgrade())
                {
                    if (isValid())
                    {
                        HAL_Delay(100);
                        Bootloader_JumpToApp();
                    }
                }
                LED_ON;
            }
        }
        HAL_Delay(10);
    }
}
