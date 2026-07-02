/*
 * bsp_error.c
 * H723ZG_V1 board error handler.
 */
#include "bsp_error.h"
#include "bsp_gpio.h"
#include "stm32h7xx_hal.h"

void Error_Handler(void)
{
    __disable_irq();
    while (1) { }
}

void BSP_Error_Handler(void)
{
    __disable_irq();
    while (1) {
        BSP_GPIO_LedToggle();
        for (volatile uint32_t i = 0; i < 500000u; i++) { }
    }
}

void BSP_Assert_Failed(const char *file, int line)
{
    (void)file;
    (void)line;
    BSP_Error_Handler();
}
