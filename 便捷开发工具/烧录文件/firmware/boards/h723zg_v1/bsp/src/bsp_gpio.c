/*
 * bsp_gpio.c
 * H723ZG_V1 GPIO implementation.
 */
#include "bsp_gpio.h"
#include "board_config.h"
#include "stm32h7xx_hal.h"

static uint32_t map_mode(BspGpioMode_t mode)
{
    switch (mode) {
        case BSP_GPIO_MODE_INPUT:     return GPIO_MODE_INPUT;
        case BSP_GPIO_MODE_OUTPUT_PP: return GPIO_MODE_OUTPUT_PP;
        case BSP_GPIO_MODE_OUTPUT_OD: return GPIO_MODE_OUTPUT_OD;
        case BSP_GPIO_MODE_AF_PP:     return GPIO_MODE_AF_PP;
        case BSP_GPIO_MODE_AF_OD:     return GPIO_MODE_AF_OD;
        case BSP_GPIO_MODE_ANALOG:    return GPIO_MODE_ANALOG;
        default:                      return GPIO_MODE_INPUT;
    }
}

static uint32_t map_pull(BspGpioPull_t pull)
{
    switch (pull) {
        case BSP_GPIO_PULL_UP:   return GPIO_PULLUP;
        case BSP_GPIO_PULL_DOWN: return GPIO_PULLDOWN;
        default:                 return GPIO_NOPULL;
    }
}

static uint32_t map_speed(BspGpioSpeed_t speed)
{
    switch (speed) {
        case BSP_GPIO_SPEED_LOW:       return GPIO_SPEED_FREQ_LOW;
        case BSP_GPIO_SPEED_MEDIUM:    return GPIO_SPEED_FREQ_MEDIUM;
        case BSP_GPIO_SPEED_HIGH:      return GPIO_SPEED_FREQ_HIGH;
        case BSP_GPIO_SPEED_VERY_HIGH: return GPIO_SPEED_FREQ_VERY_HIGH;
        default:                       return GPIO_SPEED_FREQ_LOW;
    }
}

void BSP_GPIO_InitPin(const BspGpioPin_t *pin)
{
    if (pin == NULL) return;

    GPIO_InitTypeDef init = {0};
    init.Pin = pin->pin;
    init.Mode = map_mode(pin->mode);
    init.Pull = map_pull(pin->pull);
    init.Speed = map_speed(pin->speed);
    if (pin->mode == BSP_GPIO_MODE_AF_PP || pin->mode == BSP_GPIO_MODE_AF_OD) {
        init.Alternate = pin->alternate;
    }
    HAL_GPIO_Init((GPIO_TypeDef *)pin->port, &init);
}

void BSP_GPIO_WritePin(void *port, uint16_t pin, bool high)
{
    HAL_GPIO_WritePin((GPIO_TypeDef *)port, pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void BSP_GPIO_TogglePin(void *port, uint16_t pin)
{
    HAL_GPIO_TogglePin((GPIO_TypeDef *)port, pin);
}

bool BSP_GPIO_ReadPin(void *port, uint16_t pin)
{
    return HAL_GPIO_ReadPin((GPIO_TypeDef *)port, pin) == GPIO_PIN_SET;
}

void BSP_GPIO_LedToggle(void)
{
    BSP_GPIO_TogglePin(BSP_LED_PORT, BSP_LED_PIN);
}

uint32_t BSP_GPIO_ReadModer(void *port)
{
    return ((GPIO_TypeDef *)port)->MODER;
}

uint32_t BSP_GPIO_ReadAfr(void *port, uint8_t index)
{
    return ((GPIO_TypeDef *)port)->AFR[index & 1u];
}
