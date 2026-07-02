/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Global handles and entry point declarations.
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

/* Exported handles -----------------------------------------------*/
extern DMA_HandleTypeDef  hdma_tim2_up;
extern PCD_HandleTypeDef  hpcd_USB_OTG_HS;

/* Exported functions ---------------------------------------------*/
void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
