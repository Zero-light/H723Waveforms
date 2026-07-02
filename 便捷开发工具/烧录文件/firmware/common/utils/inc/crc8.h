/*
 * crc8.h
 * CRC-8/SMBus utility.
 */
#ifndef CRC8_H
#define CRC8_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

uint8_t CRC8_Update(uint8_t crc, uint8_t byte);
uint8_t CRC8_Calc(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* CRC8_H */
