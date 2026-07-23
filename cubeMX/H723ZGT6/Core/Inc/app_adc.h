#ifndef APP_ADC_H
#define APP_ADC_H

#include <stdint.h>
#include <stdbool.h>

#define ADC_BURST_MAX_SAMPLES  8192    /* 4ch × 8192 × 2B = 64 KB, leaves 64KB DTCM for stack/heap/.bss */
#define ADC_MAX_CHANNELS       4

/* ── Layer 3: dual-channel scan + TIM3 trigger ───────────────────── */
void APP_ADC_InitDual(uint32_t sample_rate_hz);
void APP_ADC_ReadDual(uint16_t raw[2]);

/* ── Layer 4: DMA-based burst capture ────────────────────────────── */
bool APP_ADC_StartBurst(uint8_t ch_mask, uint16_t num_samples);
bool APP_ADC_IsBurstDone(void);

/* After burst done: returns interleaved DMA buffer + metadata.
 * raw_ptr: interleaved data [ch0_s0, ch1_s0, ... chN_s0, ch0_s1, ...]
 * count:   samples per channel
 * num_ch:  number of channels in this burst
 * ch_mask: channel mask passed to StartBurst                     */
void APP_ADC_GetBurstResult(const uint16_t **raw_ptr, uint16_t *count,
                            uint8_t *num_ch, uint8_t *ch_mask);

void APP_ADC_SetSampleRate(uint32_t sample_rate_hz);
void APP_ADC_SetDiffMode(bool enable);
void APP_ADC_SendBurstData(void);
void APP_ADC_DMA_IRQHandler(void);  /* called from DMA1_Stream1 ISR */

#endif /* APP_ADC_H */
