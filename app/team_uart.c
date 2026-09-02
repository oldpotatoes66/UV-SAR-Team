/* Minimal SAR-TEAM configuration protocol.
 *
 * Supports only the stock HELLO command and exact eight-byte reads/writes at
 * 0x1FF8. This deliberately excludes full EEPROM access, reset commands and
 * radio-register debugging from field builds.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app/uart.h"
#include "bsp/dp32g030/dma.h"
#include "driver/crc.h"
#include "driver/eeprom.h"
#include "driver/uart.h"
#include "misc.h"
#include "version.h"

#define TEAM_CONFIG_ADDRESS 0x1FF8u
#define TEAM_CONFIG_SIZE 8u
#define DMA_INDEX(x, y) (((x) + (y)) % sizeof(UART_DMA_Buffer))

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t size;
} Header_t;

static const uint8_t Obfuscation[16] = {
    0x16, 0x6C, 0x14, 0xE6, 0x2E, 0x91, 0x0D, 0x40,
    0x21, 0x35, 0xD5, 0x40, 0x13, 0x03, 0xE9, 0x80
};

static uint8_t Command[32];
static uint16_t writeIndex;
static uint32_t timestamp;

static void SendReply(uint8_t *reply, uint16_t size)
{
    Header_t outer = {0xCDAB, size};
    const uint8_t padding[2] = {
        Obfuscation[(size + 0) % 16] ^ 0xFF,
        Obfuscation[(size + 1) % 16] ^ 0xFF
    };
    const uint16_t footer = 0xBADC;

    for (uint16_t i = 0; i < size; i++)
        reply[i] ^= Obfuscation[i % 16];
    UART_Send(&outer, sizeof(outer));
    UART_Send(reply, size);
    UART_Send(padding, sizeof(padding));
    UART_Send(&footer, sizeof(footer));
}

static void SendHello(void)
{
    uint8_t reply[40] = {0};
    Header_t *header = (Header_t *)reply;

    header->id = 0x0515;
    header->size = 36;
    strncpy((char *)&reply[4], Version, 15);
    SendReply(reply, sizeof(reply));
}

static void SendReadReply(void)
{
    uint8_t reply[16] = {0};
    Header_t *header = (Header_t *)reply;

    header->id = 0x051C;
    header->size = 12;
    reply[4] = TEAM_CONFIG_ADDRESS & 0xFF;
    reply[5] = TEAM_CONFIG_ADDRESS >> 8;
    reply[6] = TEAM_CONFIG_SIZE;
    EEPROM_ReadBuffer(TEAM_CONFIG_ADDRESS, &reply[8], TEAM_CONFIG_SIZE);
    SendReply(reply, sizeof(reply));
}

static void SendWriteReply(void)
{
    uint8_t reply[6] = {0};
    Header_t *header = (Header_t *)reply;

    header->id = 0x051E;
    header->size = 2;
    reply[4] = TEAM_CONFIG_ADDRESS & 0xFF;
    reply[5] = TEAM_CONFIG_ADDRESS >> 8;
    SendReply(reply, sizeof(reply));
}

bool UART_IsCommandAvailable(void)
{
    uint16_t dmaLength = (uint16_t)(DMA_CH0->ST & 0xFFFu);
    uint16_t available;
    uint16_t index;
    uint16_t size;
    uint16_t tail;

    while (writeIndex != dmaLength && UART_DMA_Buffer[writeIndex] != 0xAB)
        writeIndex = DMA_INDEX(writeIndex, 1);
    if (writeIndex == dmaLength)
        return false;

    available = writeIndex < dmaLength ? (uint16_t)(dmaLength - writeIndex)
        : (uint16_t)(dmaLength + sizeof(UART_DMA_Buffer) - writeIndex);
    if (available < 8)
        return false;
    if (UART_DMA_Buffer[DMA_INDEX(writeIndex, 1)] != 0xCD) {
        writeIndex = DMA_INDEX(writeIndex, 1);
        return false;
    }

    index = DMA_INDEX(writeIndex, 2);
    size = UART_DMA_Buffer[index] |
           ((uint16_t)UART_DMA_Buffer[DMA_INDEX(index, 1)] << 8);
    if ((uint32_t)size + 8u > sizeof(Command) ||
        available < (uint16_t)(size + 8u))
        return false;

    index = DMA_INDEX(index, 2);
    tail = DMA_INDEX(index, size + 2);
    if (UART_DMA_Buffer[tail] != 0xDC ||
        UART_DMA_Buffer[DMA_INDEX(tail, 1)] != 0xBA) {
        writeIndex = dmaLength;
        return false;
    }

    for (uint16_t i = 0; i < size + 2; i++)
        Command[i] = UART_DMA_Buffer[DMA_INDEX(index, i)] ^ Obfuscation[i % 16];
    writeIndex = DMA_INDEX(tail, 2);

    return CRC_Calculate(Command, size) ==
           (uint16_t)(Command[size] | ((uint16_t)Command[size + 1] << 8));
}

void UART_HandleCommand(void)
{
    const Header_t *header = (const Header_t *)Command;

    if (header->id == 0x0514 && header->size == 4) {
        memcpy(&timestamp, &Command[4], sizeof(timestamp));
        gSerialConfigCountDown_500ms = 12;
        SendHello();
        return;
    }

    if (header->id == 0x051B && header->size == 8 &&
        Command[4] == (TEAM_CONFIG_ADDRESS & 0xFF) &&
        Command[5] == (TEAM_CONFIG_ADDRESS >> 8) &&
        Command[6] == TEAM_CONFIG_SIZE &&
        memcmp(&Command[8], &timestamp, sizeof(timestamp)) == 0) {
        gSerialConfigCountDown_500ms = 12;
        SendReadReply();
        return;
    }

    if (header->id == 0x051D && header->size == 16 &&
        Command[4] == (TEAM_CONFIG_ADDRESS & 0xFF) &&
        Command[5] == (TEAM_CONFIG_ADDRESS >> 8) &&
        Command[6] == TEAM_CONFIG_SIZE &&
        memcmp(&Command[8], &timestamp, sizeof(timestamp)) == 0) {
        gSerialConfigCountDown_500ms = 12;
        EEPROM_WriteBuffer(TEAM_CONFIG_ADDRESS, &Command[12]);
        SendWriteReply();
    }
}
