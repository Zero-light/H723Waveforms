/*
 * bsp_iwdg.h
 * Independent Watchdog driver header for H723ZG_V1.
 */
#ifndef BSP_IWDG_H
#define BSP_IWDG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Initialize the independent watchdog.
 * LSI ~32 kHz, prescaler 256 => 8 ms tick. Reload=125 => ~1 s timeout.
 * Must be called once during board initialization.
 */
void BSP_IWDG_Init(void);

/* Refresh the watchdog. Call periodically from the main loop. */
void BSP_IWDG_Refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_IWDG_H */
