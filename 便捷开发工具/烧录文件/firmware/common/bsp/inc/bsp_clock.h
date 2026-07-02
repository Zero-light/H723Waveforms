/*
 * bsp_clock.h
 * Clock and system tick abstraction.
 */
#ifndef BSP_CLOCK_H
#define BSP_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void BSP_Clock_Init(void);
uint32_t BSP_GetTick(void);
void BSP_DelayMs(uint32_t ms);
uint32_t BSP_GetSysClkHz(void);
uint32_t BSP_GetPclk1Hz(void);
uint32_t BSP_GetPclk2Hz(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_CLOCK_H */
