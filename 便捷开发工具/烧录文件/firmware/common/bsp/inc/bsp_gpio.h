/*
 * bsp_gpio.h
 * GPIO abstraction.
 */
#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BSP_GPIO_MODE_INPUT = 0,
    BSP_GPIO_MODE_OUTPUT_PP,
    BSP_GPIO_MODE_OUTPUT_OD,
    BSP_GPIO_MODE_AF_PP,
    BSP_GPIO_MODE_AF_OD,
    BSP_GPIO_MODE_ANALOG,
} BspGpioMode_t;

typedef enum {
    BSP_GPIO_SPEED_LOW = 0,
    BSP_GPIO_SPEED_MEDIUM,
    BSP_GPIO_SPEED_HIGH,
    BSP_GPIO_SPEED_VERY_HIGH,
} BspGpioSpeed_t;

typedef enum {
    BSP_GPIO_PULL_NONE = 0,
    BSP_GPIO_PULL_UP,
    BSP_GPIO_PULL_DOWN,
} BspGpioPull_t;

typedef struct {
    void       *port;           /* platform port handle, e.g. GPIO_TypeDef* */
    uint16_t    pin;
    BspGpioMode_t mode;
    BspGpioPull_t pull;
    BspGpioSpeed_t speed;
    uint8_t     alternate;      /* AF number when mode is AF */
} BspGpioPin_t;

void BSP_GPIO_InitPin(const BspGpioPin_t *pin);
void BSP_GPIO_WritePin(void *port, uint16_t pin, bool high);
void BSP_GPIO_TogglePin(void *port, uint16_t pin);
bool BSP_GPIO_ReadPin(void *port, uint16_t pin);
void BSP_GPIO_LedToggle(void);

/* Debug helpers: read back GPIO port registers */
uint32_t BSP_GPIO_ReadModer(void *port);
uint32_t BSP_GPIO_ReadAfr(void *port, uint8_t index);

#ifdef __cplusplus
}
#endif

#endif /* BSP_GPIO_H */
