/*
 * bsp_spi.h
 * SPI hardware abstraction.
 */
#ifndef BSP_SPI_H
#define BSP_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef void* BspSpiHandle_t;

typedef struct {
    bool     cpol;
    bool     cpha;
    uint8_t  prescaler;
    uint8_t  data_size;     /* 8 or 16 */
} BspSpiConfig_t;

bool BSP_SPI_Init(BspSpiHandle_t *handle, const BspSpiConfig_t *cfg);
bool BSP_SPI_Transmit(BspSpiHandle_t handle, const uint8_t *data, uint16_t len, uint32_t timeout_ms);
void BSP_SPI_CsLow(void);
void BSP_SPI_CsHigh(void);
void BSP_SPI_DeInit(BspSpiHandle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* BSP_SPI_H */
