/*
 * bsp_tim.c
 * H723ZG_V1 TIM2 implementation for waveform generator.
 */
#include "bsp_tim.h"
#include "bsp_error.h"
#include "board_config.h"
#include "stm32h7xx_hal.h"
#include "bsp_dma.h"

static TIM_HandleTypeDef s_htim2;

bool BSP_TIM_Init(BspTimHandle_t *handle)
{
    if (handle == NULL) return false;

    __HAL_RCC_TIM2_CLK_ENABLE();

    s_htim2.Instance = BSP_WAVE_TIM_INSTANCE;
    s_htim2.Init.Prescaler = 0;
    s_htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_htim2.Init.Period = 0xFFFFFFFFu;
    s_htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&s_htim2) != HAL_OK) {
        return false;
    }

    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&s_htim2, &sClockSourceConfig) != HAL_OK) {
        return false;
    }

    TIM_MasterConfigTypeDef sMasterConfig = {0};
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&s_htim2, &sMasterConfig) != HAL_OK) {
        return false;
    }

    *handle = &s_htim2;
    return true;
}

bool BSP_TIM_SetFrequency(BspTimHandle_t handle, uint32_t sample_rate_hz)
{
    (void)handle;
    if (sample_rate_hz == 0) return false;

    uint32_t tim_clk = HAL_RCC_GetPCLK1Freq();
    uint32_t apb1_div = (RCC->D2CFGR & RCC_D2CFGR_D2PPRE1_Msk) >> RCC_D2CFGR_D2PPRE1_Pos;
    if (apb1_div != RCC_D2CFGR_D2PPRE1_DIV1) {
        tim_clk *= 2;
    }

    uint32_t ticks = tim_clk / sample_rate_hz;
    if (ticks == 0) ticks = 1;

    uint32_t prescaler = 0;
    uint32_t period = 0;
    if (ticks <= 0xFFFFu) {
        prescaler = 0;
        period = (uint16_t)(ticks - 1);
    } else {
        prescaler = ticks / 0xFFFFu;
        if (prescaler > 0xFFFFu) prescaler = 0xFFFFu;
        period = (uint16_t)((ticks / (prescaler + 1)) - 1);
    }

    __HAL_TIM_SET_PRESCALER(&s_htim2, prescaler);
    __HAL_TIM_SET_AUTORELOAD(&s_htim2, period);
    BSP_TIM_GenerateUpdate(handle);
    return true;
}

bool BSP_TIM_BaseStart(BspTimHandle_t handle)
{
    (void)handle;
    return (HAL_TIM_Base_Start(&s_htim2) == HAL_OK);
}

bool BSP_TIM_BaseStop(BspTimHandle_t handle)
{
    (void)handle;
    return (HAL_TIM_Base_Stop(&s_htim2) == HAL_OK);
}

bool BSP_TIM_EnableDmaUpdate(BspTimHandle_t handle)
{
    (void)handle;
    __HAL_TIM_ENABLE_DMA(&s_htim2, TIM_DMA_UPDATE);
    return true;
}

bool BSP_TIM_DisableDmaUpdate(BspTimHandle_t handle)
{
    (void)handle;
    __HAL_TIM_DISABLE_DMA(&s_htim2, TIM_DMA_UPDATE);
    return true;
}

bool BSP_TIM_GenerateUpdate(BspTimHandle_t handle)
{
    (void)handle;
    return (HAL_TIM_GenerateEvent(&s_htim2, TIM_EVENTSOURCE_UPDATE) == HAL_OK);
}

void BSP_TIM_LinkDma(BspTimHandle_t tim, void *dma_handle)
{
    (void)tim;
    __HAL_LINKDMA(&s_htim2, hdma[TIM_DMA_ID_UPDATE], *((DMA_HandleTypeDef *)dma_handle));
}

void BSP_TIM_DeInit(BspTimHandle_t handle)
{
    (void)handle;
    HAL_TIM_Base_DeInit(&s_htim2);
}
