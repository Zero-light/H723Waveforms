/*
 * bsp_adc.c
 * H723ZG_V1 ADC1 implementation.
 */
#include "bsp_adc.h"
#include "bsp_error.h"
#include "board_config.h"
#include "stm32h7xx_hal.h"
#include <string.h>

/* Rank constants: LL_ADC_REG_RANK_1..8 are encoded with register offset + bit pos */
static const uint32_t s_rankTable[] = {
    LL_ADC_REG_RANK_1, LL_ADC_REG_RANK_2, LL_ADC_REG_RANK_3, LL_ADC_REG_RANK_4,
    LL_ADC_REG_RANK_5, LL_ADC_REG_RANK_6, LL_ADC_REG_RANK_7, LL_ADC_REG_RANK_8,
};

static ADC_HandleTypeDef s_hadc1;

bool BSP_ADC_Init(BspAdcHandle_t *handle, const BspAdcConfig_t *cfg)
{
    if (handle == NULL || cfg == NULL || cfg->channels == NULL || cfg->num_channels == 0) {
        return false;
    }

    BSP_ADC_DeInit(handle);

    __HAL_RCC_ADC12_CLK_ENABLE();

    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    PeriphClkInit.AdcClockSelection = RCC_ADCCLKSOURCE_CLKP;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
        return false;
    }

    s_hadc1.Instance = ADC1;
    s_hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    s_hadc1.Init.Resolution = (cfg->resolution_bits == 8) ? ADC_RESOLUTION_8B
                            : (cfg->resolution_bits == 10) ? ADC_RESOLUTION_10B
                            : ADC_RESOLUTION_12B;
    s_hadc1.Init.ScanConvMode = (cfg->num_channels > 1) ? ADC_SCAN_ENABLE : ADC_SCAN_DISABLE;
    s_hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    s_hadc1.Init.LowPowerAutoWait = DISABLE;
    s_hadc1.Init.ContinuousConvMode = cfg->continuous ? ENABLE : DISABLE;
    s_hadc1.Init.NbrOfConversion = cfg->num_channels;
    s_hadc1.Init.DiscontinuousConvMode = DISABLE;
    s_hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    s_hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    s_hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
    s_hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    s_hadc1.Init.OversamplingMode = DISABLE;

    if (HAL_ADC_Init(&s_hadc1) != HAL_OK) {
        return false;
    }
    if (HAL_ADCEx_Calibration_Start(&s_hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        return false;
    }

    for (uint8_t rank = 0; rank < cfg->num_channels; rank++) {
        ADC_ChannelConfTypeDef sChan = {0};
        sChan.Channel = cfg->channels[rank];
        sChan.Rank = s_rankTable[rank];
        sChan.SamplingTime = cfg->sample_time;
        sChan.SingleDiff = ADC_SINGLE_ENDED;
        sChan.OffsetNumber = ADC_OFFSET_NONE;
        if (HAL_ADC_ConfigChannel(&s_hadc1, &sChan) != HAL_OK) {
            return false;
        }
    }

    *handle = &s_hadc1;
    return true;
}

bool BSP_ADC_Calibrate(BspAdcHandle_t handle)
{
    (void)handle;
    return (HAL_ADCEx_Calibration_Start(&s_hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) == HAL_OK);
}

bool BSP_ADC_Start(BspAdcHandle_t handle)
{
    (void)handle;
    return (HAL_ADC_Start(&s_hadc1) == HAL_OK);
}

bool BSP_ADC_Stop(BspAdcHandle_t handle)
{
    (void)handle;
    return (HAL_ADC_Stop(&s_hadc1) == HAL_OK);
}

bool BSP_ADC_PollForConversion(BspAdcHandle_t handle, uint32_t timeout_ms)
{
    (void)handle;
    return (HAL_ADC_PollForConversion(&s_hadc1, timeout_ms) == HAL_OK);
}

uint32_t BSP_ADC_ReadValue(BspAdcHandle_t handle)
{
    (void)handle;
    return HAL_ADC_GetValue(&s_hadc1);
}

void BSP_ADC_DeInit(BspAdcHandle_t handle)
{
    (void)handle;
    if (s_hadc1.Instance != NULL) {
        HAL_ADC_DeInit(&s_hadc1);
    }
}
