/* Minimal SAR-TEAM configuration/channel protocol.
 *
 * Supports HELLO, the exact eight-byte TEAM block, and 16-byte-or-smaller
 * access to the three MR channel regions. Settings, VFO state, calibration,
 * reset commands and radio-register debugging remain inaccessible.
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
#define CHANNEL_RECORD_START 0x0000u
#define CHANNEL_RECORD_END   0x0C80u
#define CHANNEL_ATTR_START   0x0D60u
#define CHANNEL_ATTR_END     0x0E28u
#define CHANNEL_NAME_START   0x0F50u
#define CHANNEL_NAME_END     0x1BD0u
#define MAX_TRANSFER_SIZE    16u
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

static bool IsAllowedRange(uint16_t address, uint8_t size)
{
    const uint32_t end = (uint32_t)address + size;

    if (size == 0 || size > MAX_TRANSFER_SIZE)
        return false;
    if (address == TEAM_CONFIG_ADDRESS && size == TEAM_CONFIG_SIZE)
        return true;
    return end <= CHANNEL_RECORD_END ||
           (address >= CHANNEL_ATTR_START && end <= CHANNEL_ATTR_END) ||
           (address >= CHANNEL_NAME_START && end <= CHANNEL_NAME_END);
}

static void SendReadReply(uint16_t address, uint8_t size)
{
    uint8_t reply[8 + MAX_TRANSFER_SIZE] = {0};
    Header_t *header = (Header_t *)reply;

    header->id = 0x051C;
    header->size = 4 + size;
    reply[4] = address & 0xFF;
    reply[5] = address >> 8;
    reply[6] = size;
    EEPROM_ReadBuffer(address, &reply[8], size);
    SendReply(reply, 8 + size);
}

static void SendWriteReply(uint16_t address)
{
    uint8_t reply[6] = {0};
    Header_t *header = (Header_t *)reply;

    header->id = 0x051E;
    header->size = 2;
    reply[4] = address & 0xFF;
    reply[5] = address >> 8;
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
        memcmp(&Command[8], &timestamp, sizeof(timestamp)) == 0) {
        const uint16_t address = Command[4] | ((uint16_t)Command[5] << 8);
        const uint8_t size = Command[6];
        if (IsAllowedRange(address, size)) {
            gSerialConfigCountDown_500ms = 12;
            SendReadReply(address, size);
            return;
        }
    }

    if (header->id == 0x051D && header->size >= 9 &&
        header->size <= 8 + MAX_TRANSFER_SIZE &&
        memcmp(&Command[8], &timestamp, sizeof(timestamp)) == 0) {
        const uint16_t address = Command[4] | ((uint16_t)Command[5] << 8);
        const uint8_t size = Command[6];
        if (header->size == 8 + size && IsAllowedRange(address, size)) {
            gSerialConfigCountDown_500ms = 12;
            EEPROM_WriteBuffer(address, &Command[12]);
            SendWriteReply(address);
        }
    }
}
