/*
 * bsp_error.c
 * H723ZG_V1 board error handler.
 */
#include "bsp_error.h"
#include "bsp_gpio.h"
#include "stm32h7xx_hal.h"

void Error_Handler(void)
{

    /* Ensure GPIOG clock on so the LED can signal the fault before gpio_init().
     * RCC is always accessible after reset (HSI on), and for errors after
     * gpio_init() this is a harmless no-op. */
    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOGEN;
    (void)RCC->AHB4ENR; /* flush write */

    __disable_irq();
    BSP_Error_Handler(); /* LED-blink error indicator */
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
