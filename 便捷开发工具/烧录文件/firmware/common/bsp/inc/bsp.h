/*
 * bsp.h
 * Board Support Package common interface.
 * Include this header to access all BSP services.
 */
#ifndef BSP_H
#define BSP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp_gpio.h"
#include "bsp_error.h"
#include "bsp_clock.h"
#include "bsp_adc.h"
#include "bsp_dac.h"
#include "bsp_spi.h"
#include "bsp_tim.h"
#include "bsp_dma.h"
#include "bsp_usb.h"

/* Initialize the entire board.
 * Must be called before any other BSP/Driver/APP function.
 */
void BSP_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_H */
