/*
 * bsp_clock.c
 * H723ZG_V1 clock configuration.
 *
 * ═══ IMPORTANT: IOC vs firmware discrepancy ═══
 * The CubeMX .ioc file is configured for HSE (25 MHz external crystal),
 * but this firmware uses HSI (64 MHz internal RC oscillator) for the PLL
 * source.  This is intentional: many custom boards omit the external HSE
 * crystal and run entirely from HSI.
 *
 * If you regenerate code from CubeMX, DO NOT replace this file with the
 * generated SystemClock_Config().  The CubeMX-generated clock init will
 * hang waiting for HSE ready on boards without a crystal.
 *
 * PLL: HSI 64 MHz → ÷M=4 → 16 MHz → ×N=24 → 384 MHz VCO → ÷P=2 → 192 MHz SYSCLK
 */
#include "bsp_clock.h"
#include "bsp_error.h"
#include "board_config.h"
#include "stm32h7xx_hal.h"

void BSP_Clock_Init(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Enable HSI48 for USB clock source */
    __HAL_RCC_HSI48_ENABLE();
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_HSI48RDY) == RESET) { }

    /* Scale voltage regulator for 192 MHz */
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0) != HAL_OK) {
        BSP_Error_Handler();
    }

    /* Configure main PLL: HSI 64 MHz -> PLL1 -> 192 MHz
     * M=4, N=24, P=2  => 64/4*24/2 = 192 MHz
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 24;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        BSP_Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2
                                | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        BSP_Error_Handler();
    }
    /* Re-sync SysTick to the new HCLK (192 MHz) after clock switch.
     * HAL_Init() configured SysTick for HSI 64 MHz; without this line
     * SysTick fires 3x too fast (3 kHz instead of 1 kHz), which breaks
     * all HAL_Delay(), HAL_GetTick(), and any ms-granularity logic.
    * ponytail: HAL_SYSTICK_Config wraps SysTick_Config() with correct
    * reload value.  No GPIO/peripheral registers are touched. */
    {
        uint32_t const ticks = SystemCoreClock / 1000U;
        HAL_SYSTICK_Config(ticks);
    }
}

uint32_t BSP_GetTick(void)
{
    return HAL_GetTick();
}

void BSP_DelayMs(uint32_t ms)
{
    HAL_Delay(ms);
}

uint32_t BSP_GetSysClkHz(void)
{
    return BOARD_SYSCLK_HZ;
}

uint32_t BSP_GetPclk1Hz(void)
{
    return BOARD_APB1_HZ;
}

uint32_t BSP_GetPclk2Hz(void)
{
    return BOARD_APB2_HZ;
}
