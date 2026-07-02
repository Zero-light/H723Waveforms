/*
 * drv_dac.h
 * DAC driver, board-agnostic.
 */
#ifndef DRV_DAC_H
#define DRV_DAC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

void DRV_DAC_Init(void);
bool DRV_DAC_SetValue(uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* DRV_DAC_H */
