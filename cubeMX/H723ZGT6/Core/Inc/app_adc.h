#ifndef APP_ADC_H
#define APP_ADC_H

#include <stdint.h>
#include <stdbool.h>

#define ADC_BURST_MAX_SAMPLES  16384   /* 64 KB, safe for DTCM */
#define ADC_MAX_CHANNELS       2

/* ── Layer 3: dual-channel scan + TIM3 trigger ───────────────────── */
void APP_ADC_InitDual(uint32_t sample_rate_hz);
void APP_ADC_ReadDual(uint16_t raw[2]);

/* ── Layer 4: DMA-based burst capture ────────────────────────────── */
bool APP_ADC_StartBurst(uint8_t ch_mask, uint16_t num_samples);
bool APP_ADC_IsBurstDone(void);

/* After burst done: raw0/raw1 point into internal DMA buffer (read-only). */
void APP_ADC_GetBurstResult(const uint16_t **raw0, const uint16_t **raw1,
                            uint16_t *count);

void APP_ADC_SetSampleRate(uint32_t sample_rate_hz);
void APP_ADC_SendBurstData(void);
void APP_ADC_DMA_IRQHandler(void);  /* called from DMA1_Stream1 ISR */

#endif /* APP_ADC_H */
