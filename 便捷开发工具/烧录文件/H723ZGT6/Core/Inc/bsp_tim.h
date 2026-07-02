/*
 * bsp_tim.h
 * Timer hardware abstraction (used by waveform generator).
 */
#ifndef BSP_TIM_H
#define BSP_TIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef void* BspTimHandle_t;

bool BSP_TIM_Init(BspTimHandle_t *handle);
bool BSP_TIM_SetFrequency(BspTimHandle_t handle, uint32_t sample_rate_hz);
bool BSP_TIM_BaseStart(BspTimHandle_t handle);
bool BSP_TIM_BaseStop(BspTimHandle_t handle);
bool BSP_TIM_EnableDmaUpdate(BspTimHandle_t handle);
bool BSP_TIM_DisableDmaUpdate(BspTimHandle_t handle);
bool BSP_TIM_GenerateUpdate(BspTimHandle_t handle);
void BSP_TIM_LinkDma(BspTimHandle_t tim, void *dma_handle);
void BSP_TIM_DeInit(BspTimHandle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* BSP_TIM_H */
