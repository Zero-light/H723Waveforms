/*
 * drv_adc.h
 * ADC driver: polled multi-channel acquisition, board-agnostic.
 */
#ifndef DRV_ADC_H
#define DRV_ADC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define DRV_ADC_MAX_CHANNELS    2   /* PA6 (CH0) + PA7 (CH1) */
#define DRV_ADC_BATCH_SIZE      200

/* Channel mask bits:
 * bit 0 = PA6 (ADC1_IN3) - always enabled by the driver
 * bit 1 = PA7 (ADC1_IN7) - optional, selected by user
 */
#define DRV_ADC_MASK_PA6        (1u << 0)
#define DRV_ADC_MASK_PA7        (1u << 1)

typedef enum {
    DRV_ADC_MODE_RAW_STREAM = 0,
    DRV_ADC_MODE_PACKED     = 1,
    DRV_ADC_MODE_BURST      = 2,
} DrvAdcMode_t;

typedef struct {
    uint8_t     ch_mask;        /* bit0=PA6 (forced on), bit1=PA7 (optional) */
    uint32_t    sample_rate_hz;
    uint8_t     mode;
} DrvAdcConfig_t;

void DRV_ADC_Init(void);
bool DRV_ADC_Configure(const DrvAdcConfig_t *cfg);
bool DRV_ADC_Start(void);
void DRV_ADC_Stop(void);
void DRV_ADC_Poll(void);
bool DRV_ADC_IsRunning(void);

#ifdef __cplusplus
}
#endif

#endif /* DRV_ADC_H */
