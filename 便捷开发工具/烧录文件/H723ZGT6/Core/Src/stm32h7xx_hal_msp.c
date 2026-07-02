/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file         stm32h7xx_hal_msp.c
  * @brief        Minimal MSP Initialization.
  *               Per-peripheral MSP (TIM, DMA, etc.) is handled by the BSP
  *               layer under firmware/boards/ and firmware/common/.
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"

void HAL_MspInit(void)
{
  __HAL_RCC_SYSCFG_CLK_ENABLE();
}
