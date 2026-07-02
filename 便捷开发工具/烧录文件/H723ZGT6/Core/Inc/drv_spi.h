/*
 * drv_spi.h
 * SPI driver, board-agnostic.
 */
#ifndef DRV_SPI_H
#define DRV_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t cpol;
    uint8_t cpha;
    uint8_t prescaler;
    uint8_t data_size;  /* 8 or 16 */
} DrvSpiConfig_t;

void DRV_SPI_Init(void);
bool DRV_SPI_Configure(const DrvSpiConfig_t *cfg);
bool DRV_SPI_WriteReg(uint8_t addr, uint16_t data, uint8_t data_width);
bool DRV_SPI_WriteRegs(const uint8_t *payload, uint8_t num_regs, uint8_t data_width);

#ifdef __cplusplus
}
#endif

#endif /* DRV_SPI_H */
