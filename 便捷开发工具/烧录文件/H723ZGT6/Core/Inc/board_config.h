/*
 * board_config.h
 * Board-specific configuration for H723ZG_V1.
 * All hardware resources (pins, clocks, channels, handles) are defined here.
 * Upper layers include only this file and common/bsp headers.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

/* Chip family and board identity */
#define BOARD_ID                    "h723zg_v1"
#define BOARD_NAME                  "STM32H723ZG Nucleo/Custom V1"
#define BOARD_CHIP                  "STM32H723ZGTx"
#define BOARD_SYSCLK_HZ             192000000u
#define BOARD_AHB_HZ                192000000u
#define BOARD_APB1_HZ               96000000u
#define BOARD_APB2_HZ               96000000u
#define BOARD_ADC_VREF_MV           3300u

/* On-board LED */
#define BSP_LED_PORT                GPIOG
#define BSP_LED_PIN                 GPIO_PIN_7

/* ADC channels (ADC1): only PA6 (IN3) and PA7 (IN7) */
#define BSP_ADC_NUM_CHANNELS        2
#define BSP_ADC_CH0_PIN_PORT        GPIOA
#define BSP_ADC_CH0_PIN_NUM         GPIO_PIN_6      /* PA6 -> ADC1_IN3 */
#define BSP_ADC_CH0_CHANNEL         ADC_CHANNEL_3
#define BSP_ADC_CH1_PIN_PORT        GPIOA
#define BSP_ADC_CH1_PIN_NUM         GPIO_PIN_7      /* PA7 -> ADC1_IN7 */
#define BSP_ADC_CH1_CHANNEL         ADC_CHANNEL_7

/* DAC */
#define BSP_DAC_CHANNEL             DAC_CHANNEL_1
#define BSP_DAC_PIN_PORT            GPIOA
#define BSP_DAC_PIN_NUM             GPIO_PIN_4      /* PA4 -> DAC1_OUT1 */

/* SPI (SPI2) */
#define BSP_SPI_INSTANCE            SPI2
#define BSP_SPI_SCK_PORT            GPIOB
#define BSP_SPI_SCK_PIN             GPIO_PIN_13
#define BSP_SPI_SCK_AF              GPIO_AF5_SPI2
#define BSP_SPI_MISO_PORT           GPIOB
#define BSP_SPI_MISO_PIN            GPIO_PIN_14
#define BSP_SPI_MISO_AF             GPIO_AF5_SPI2
#define BSP_SPI_MOSI_PORT           GPIOB
#define BSP_SPI_MOSI_PIN            GPIO_PIN_15
#define BSP_SPI_MOSI_AF             GPIO_AF5_SPI2
#define BSP_SPI_CS_PORT             GPIOB
#define BSP_SPI_CS_PIN              GPIO_PIN_12

/* Waveform generator (TIM2 + DMA1 Stream0) */
#define BSP_WAVE_TIM_INSTANCE       TIM2
#define BSP_WAVE_DMA_STREAM         DMA1_Stream0
#define BSP_WAVE_DMA_REQUEST        DMA_REQUEST_TIM2_UP
#define BSP_WAVE_DMA_IRQ            DMA1_Stream0_IRQn
#define BSP_WAVE_DMA_HANDLER        DMA1_Stream0_IRQHandler

/* Waveform GPIO channels mapped to PA0..PA3, PA5 */
#define BSP_WAVE_CH0_BIT            0u   /* PA0 */
#define BSP_WAVE_CH1_BIT            1u   /* PA1 */
#define BSP_WAVE_CH2_BIT            2u   /* PA2 */
#define BSP_WAVE_CH3_BIT            3u   /* PA3 */
#define BSP_WAVE_CH4_BIT            5u   /* PA5 */

/* USB CDC */
#define BSP_USB_VBUS_DETECT         0    /* 0 = no VBUS sensing */

#ifdef __cplusplus
}
#endif

#endif /* BOARD_CONFIG_H */
