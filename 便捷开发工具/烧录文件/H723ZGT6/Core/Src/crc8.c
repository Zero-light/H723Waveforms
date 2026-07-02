/*
 * crc8.c
 * CRC-8/SMBus polynomial 0x07.
 */
#include "crc8.h"

uint8_t CRC8_Update(uint8_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint8_t b = 0; b < 8; b++) {
        crc = (crc & 0x80u) ? ((crc << 1) ^ 0x07u) : (crc << 1);
    }
    return crc;
}

uint8_t CRC8_Calc(const uint8_t *data, uint16_t len)
{
    uint8_t crc = 0x00;
    for (uint16_t i = 0; i < len; i++) {
        crc = CRC8_Update(crc, data[i]);
    }
    return crc;
}
