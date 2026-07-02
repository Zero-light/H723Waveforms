/*
 * bsp_dac.c
 * H723ZG_V1 DAC1 implementation.
 */
#include "bsp_dac.h"
#include "bsp_gpio.h"
#include "board_config.h"
#include "stm32h7xx_hal.h"

static DAC_HandleTypeDef s_hdac1;

bool BSP_DAC_Init(BspDacHandle_t *handle)
{
    if (handle == NULL) return false;

    __HAL_RCC_DAC12_CLK_ENABLE();

    s_hdac1.Instance = DAC1;
    if (HAL_DAC_Init(&s_hdac1) != HAL_OK) {
        return false;
    }

    DAC_ChannelConfTypeDef sConfig = {0};
    sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
    sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
    sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
    sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;

    if (HAL_DAC_ConfigChannel(&s_hdac1, &sConfig, BSP_DAC_CHANNEL) != HAL_OK) {
        return false;
    }

    HAL_DAC_Start(&s_hdac1, BSP_DAC_CHANNEL);
    HAL_DAC_SetValue(&s_hdac1, BSP_DAC_CHANNEL, DAC_ALIGN_12B_R, 0);

    *handle = &s_hdac1;
    return true;
}

bool BSP_DAC_SetValue(BspDacHandle_t handle, uint16_t value)
{
    (void)handle;
    if (value > 4095u) return false;
    HAL_DAC_SetValue(&s_hdac1, BSP_DAC_CHANNEL, DAC_ALIGN_12B_R, value);
    return true;
}

void BSP_DAC_DeInit(BspDacHandle_t handle)
{
    (void)handle;
    HAL_DAC_DeInit(&s_hdac1);
}
