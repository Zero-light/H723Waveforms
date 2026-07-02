/*
 * drv_dac.c
 * DAC driver implementation using BSP_DAC.
 */
#include "drv_dac.h"
#include "bsp_dac.h"
#include <stddef.h>

static BspDacHandle_t s_dacHandle = NULL;

void DRV_DAC_Init(void)
{
    BSP_DAC_Init(&s_dacHandle);
}

bool DRV_DAC_SetValue(uint16_t value)
{
    return BSP_DAC_SetValue(s_dacHandle, value);
}
