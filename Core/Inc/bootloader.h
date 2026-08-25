#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include "main.h"
#include <stdint.h>

#define APP_FLASH_STARTADDR     0x08006000
#define APP_MAX_SIZE            0x0000a000
#define UPGRADE_FLAG_ADDR       0x0800f000
#define UPGRADE_FLAGE_MAGIC     0x5a5a5a5a

#define CMD_DATA                0x01
#define CMD_END                 0x02
#define CMD_ABORT               0x03

#define UPGRADE_WAIT_TIMEOUT_MS 2000

#define RESP_OK                 "OK"
#define RESP_LEN_FAIL           "LEN_FAIL"
#define RESP_LEN_ERR            "LEN_ERR"
#define RESP_DATA_FAIL          "DATA_FAIL"
#define RESP_WRITE_FAIL         "WRITE_FAIL"
#define RESP_CRC_FAIL           "CRC_FAIL"
#define RESP_SIZE_FAIL          "SIZE_FAIL"
#define RESP_SIZE_MISMATCH      "SIZE_MISMATCH"
#define RESP_DONE               "DONE"
#define RESP_FAIL               "FAIL"

#define RX_BUFFER_SIZE      128

#define LED_ON      HAL_GPIO_WritePin(LED_STATE_GPIO_Port, LED_STATE_Pin, GPIO_PIN_SET)
#define LED_OFF     HAL_GPIO_WritePin(LED_STATE_GPIO_Port, LED_STATE_Pin, GPIO_PIN_RESET)

void Bootloader_MainLoop(void);

#endif // !__BOOTLOADER_H
