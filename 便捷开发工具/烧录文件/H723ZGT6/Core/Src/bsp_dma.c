/*
 * bsp_dma.c
 * H723ZG_V1 DMA implementation for TIM2 update.
 */
#include "bsp_dma.h"
#include "bsp_error.h"
#include "board_config.h"
#include "stm32h7xx_hal.h"

DMA_HandleTypeDef hdma_tim2_up;

bool BSP_DMA_Init(BspDmaHandle_t *handle, const BspDmaConfig_t *cfg)
{
    if (handle == NULL || cfg == NULL) return false;

    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_tim2_up.Instance = BSP_WAVE_DMA_STREAM;
    hdma_tim2_up.Init.Request = BSP_WAVE_DMA_REQUEST;
    hdma_tim2_up.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_tim2_up.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_tim2_up.Init.MemInc = DMA_MINC_ENABLE;
    hdma_tim2_up.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_tim2_up.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_tim2_up.Init.Mode = (cfg->mode == BSP_DMA_MODE_CIRCULAR) ? DMA_CIRCULAR : DMA_NORMAL;
    hdma_tim2_up.Init.Priority = DMA_PRIORITY_LOW;
    hdma_tim2_up.Init.FIFOMode = DMA_FIFOMODE_DISABLE;

    if (HAL_DMA_Init(&hdma_tim2_up) != HAL_OK) {
        return false;
    }

    HAL_NVIC_SetPriority(BSP_WAVE_DMA_IRQ, 0, 0);
    HAL_NVIC_EnableIRQ(BSP_WAVE_DMA_IRQ);

    *handle = &hdma_tim2_up;
    return true;
}

bool BSP_DMA_Start(BspDmaHandle_t handle, const BspDmaConfig_t *cfg)
{
    (void)handle;
    return (HAL_DMA_Start(&hdma_tim2_up,
                          (uint32_t)cfg->mem_addr,
                          (uint32_t)cfg->periph_addr,
                          cfg->length) == HAL_OK);
}

bool BSP_DMA_Stop(BspDmaHandle_t handle)
{
    (void)handle;
    return (HAL_DMA_Abort(&hdma_tim2_up) == HAL_OK);
}

void BSP_DMA_DeInit(BspDmaHandle_t handle)
{
    (void)handle;
    HAL_DMA_DeInit(&hdma_tim2_up);
}
