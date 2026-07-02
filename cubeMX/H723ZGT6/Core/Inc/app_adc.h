#ifndef APP_ADC_H
#define APP_ADC_H

#include <stdint.h>
#include <stdbool.h>

#define ADC_BURST_MAX_SAMPLES  32768
#define ADC_MAX_CHANNELS       2

/* ── Init + read ──────────────────────────────────────────────────── */
void APP_ADC_InitDual(uint32_t sample_rate_hz);
void APP_ADC_ReadDual(uint16_t raw[2]);
void APP_ADC_SetSampleRate(uint32_t sample_rate_hz);

/* ── DMA burst ────────────────────────────────────────────────────── */
bool APP_ADC_StartBurst(uint8_t ch_mask, uint16_t num_samples);
bool APP_ADC_IsBurstDone(void);
void APP_ADC_SendBurstData(void);  /* pack + send via CDC after burst done */
void APP_ADC_DMA_IRQHandler(void);

#endif /* APP_ADC_H */
