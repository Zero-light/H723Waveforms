/*
 * bsp_dma.h
 * DMA hardware abstraction.
 */
#ifndef BSP_DMA_H
#define BSP_DMA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef void* BspDmaHandle_t;

typedef enum {
    BSP_DMA_MODE_NORMAL = 0,
    BSP_DMA_MODE_CIRCULAR,
} BspDmaMode_t;

typedef struct {
    void       *periph_addr;
    void       *mem_addr;
    uint16_t    length;
    BspDmaMode_t mode;
} BspDmaConfig_t;

bool BSP_DMA_Init(BspDmaHandle_t *handle, const BspDmaConfig_t *cfg);
bool BSP_DMA_Start(BspDmaHandle_t handle, const BspDmaConfig_t *cfg);
bool BSP_DMA_Stop(BspDmaHandle_t handle);
void BSP_DMA_DeInit(BspDmaHandle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DMA_H */
