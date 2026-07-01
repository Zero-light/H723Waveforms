#ifndef APP_SPI_H
#define APP_SPI_H

#include <stdint.h>
#include <stdbool.h>

/* SPI2 on PB12-PB15:
 * PB13 = SCK, PB15 = MOSI, PB14 = MISO, PB12 = CS (software)
 */

typedef struct {
    uint8_t cpol;       /* 0 or 1 */
    uint8_t cpha;       /* 0 or 1 */
    uint8_t prescaler;  /* 0=/2, 1=/4, 2=/8, 3=/16, 4=/32, 5=/64, 6=/128, 7=/256 */
    uint8_t data_size;  /* 8 or 16 (SPI frame size) */
} SpiConfig_t;

void APP_SPI_Init(void);
bool APP_SPI_Configure(const SpiConfig_t *cfg);

/* Single register write (8 or 16-bit data) */
bool APP_SPI_WriteReg(uint8_t addr, uint16_t data, uint8_t data_width);

/* Batch register write from raw payload.
 * payload format: [addr0, data0...] [addr1, data1...] ...
 * data_width: 8 or 16 bits per data value
 */
bool APP_SPI_WriteRegs(const uint8_t *payload, uint8_t num_regs, uint8_t data_width);

#endif /* APP_SPI_H */
