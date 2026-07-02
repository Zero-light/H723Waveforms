/*
 * bsp_init.c
 * H723ZG_V1 board-level initialization.
 */
#include "bsp.h"
#include "board_config.h"
#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* Debug option: set to 1 to skip PA0~PA3/PA5 input init,
 * isolating crosstalk from high-speed digital edges on PA6/PA7. */
#define BSP_GPIO_SKIP_DIGITAL_OUT_TEST  0

static void gpio_init(void);

void BSP_Init(void)
{
    HAL_Init();
    BSP_Clock_Init();
    gpio_init();
}

static void gpio_init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* ADC pins as analog: PA4 (DAC), PA6, PA7 */
    const BspGpioPin_t adc_pins[] = {
        { BSP_DAC_PIN_PORT,  BSP_DAC_PIN_NUM,  BSP_GPIO_MODE_ANALOG, BSP_GPIO_PULL_NONE, BSP_GPIO_SPEED_LOW, 0 },
        { BSP_ADC_CH0_PIN_PORT, BSP_ADC_CH0_PIN_NUM, BSP_GPIO_MODE_ANALOG, BSP_GPIO_PULL_NONE, BSP_GPIO_SPEED_LOW, 0 },
        { BSP_ADC_CH1_PIN_PORT, BSP_ADC_CH1_PIN_NUM, BSP_GPIO_MODE_ANALOG, BSP_GPIO_PULL_NONE, BSP_GPIO_SPEED_LOW, 0 },
    };
    for (uint8_t i = 0; i < sizeof(adc_pins) / sizeof(adc_pins[0]); i++) {
        BSP_GPIO_InitPin(&adc_pins[i]);
    }

    /* Explicitly clear AF for PA6/PA7 to prevent TIM/SPI leakage.
     * Use read-modify-write to avoid side effects on other pins. */
    uint32_t afr0 = GPIOA->AFR[0];
    afr0 &= ~((0xFUL << (6U * 4U)) | (0xFUL << (7U * 4U)));
    GPIOA->AFR[0] = afr0;

#if BSP_GPIO_SKIP_DIGITAL_OUT_TEST
    /* ponytail: diagnostic mode - skip high-speed digital outputs on PA0~PA3/PA5
     * to rule out crosstalk to analog inputs PA6/PA7. */
    (void)0;
#else
    /* Waveform GPIO pins PA0..PA3, PA5 default to high-impedance input.
     * They are dynamically switched to output by DRV_WaveGen_Start() only
     * for the channels actually enabled in ch_mask. This prevents the MCU
     * from driving unused pins low at power-up, which can look like a
     * short-to-ground when external circuitry is connected. */
    const BspGpioPin_t wave_pins[] = {
        { GPIOA, GPIO_PIN_0, BSP_GPIO_MODE_INPUT, BSP_GPIO_PULL_NONE, BSP_GPIO_SPEED_LOW, 0 },
        { GPIOA, GPIO_PIN_1, BSP_GPIO_MODE_INPUT, BSP_GPIO_PULL_NONE, BSP_GPIO_SPEED_LOW, 0 },
        { GPIOA, GPIO_PIN_2, BSP_GPIO_MODE_INPUT, BSP_GPIO_PULL_NONE, BSP_GPIO_SPEED_LOW, 0 },
        { GPIOA, GPIO_PIN_3, BSP_GPIO_MODE_INPUT, BSP_GPIO_PULL_NONE, BSP_GPIO_SPEED_LOW, 0 },
        { GPIOA, GPIO_PIN_5, BSP_GPIO_MODE_INPUT, BSP_GPIO_PULL_NONE, BSP_GPIO_SPEED_LOW, 0 },
    };
    for (uint8_t i = 0; i < sizeof(wave_pins) / sizeof(wave_pins[0]); i++) {
        BSP_GPIO_InitPin(&wave_pins[i]);
    }
#endif

    /* On-board LED PG7 */
    __HAL_RCC_GPIOG_CLK_ENABLE();
    const BspGpioPin_t led_pin = {
        BSP_LED_PORT, BSP_LED_PIN,
        BSP_GPIO_MODE_OUTPUT_PP, BSP_GPIO_PULL_NONE, BSP_GPIO_SPEED_LOW, 0
    };
    BSP_GPIO_InitPin(&led_pin);
    BSP_GPIO_WritePin(BSP_LED_PORT, BSP_LED_PIN, true);
}
