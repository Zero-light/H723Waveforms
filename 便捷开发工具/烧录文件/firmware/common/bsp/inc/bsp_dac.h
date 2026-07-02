/*
 * bsp_dac.h
 * DAC hardware abstraction.
 */
#ifndef BSP_DAC_H
#define BSP_DAC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef void* BspDacHandle_t;

bool BSP_DAC_Init(BspDacHandle_t *handle);
bool BSP_DAC_SetValue(BspDacHandle_t handle, uint16_t value);
void BSP_DAC_DeInit(BspDacHandle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DAC_H */
