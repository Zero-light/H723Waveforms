/*
 * bsp_adc.h
 * ADC hardware abstraction.
 */
#ifndef BSP_ADC_H
#define BSP_ADC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef void* BspAdcHandle_t;
typedef uint32_t BspAdcChannel_t;

typedef struct {
    uint8_t  resolution_bits;   /* 12, 10, 8 */
    uint32_t sample_time;       /* platform-specific sampling time constant */
    bool     continuous;
    uint8_t  num_channels;
    const BspAdcChannel_t *channels;
} BspAdcConfig_t;

bool BSP_ADC_Init(BspAdcHandle_t *handle, const BspAdcConfig_t *cfg);
bool BSP_ADC_Calibrate(BspAdcHandle_t handle);
bool BSP_ADC_Start(BspAdcHandle_t handle);
bool BSP_ADC_Stop(BspAdcHandle_t handle);
bool BSP_ADC_PollForConversion(BspAdcHandle_t handle, uint32_t timeout_ms);
uint32_t BSP_ADC_ReadValue(BspAdcHandle_t handle);
void BSP_ADC_DeInit(BspAdcHandle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* BSP_ADC_H */
