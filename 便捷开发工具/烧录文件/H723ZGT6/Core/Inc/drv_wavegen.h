/*
 * drv_wavegen.h
 * Waveform generator driver using TIM + DMA + GPIO BSRR, board-agnostic.
 */
#ifndef DRV_WAVEGEN_H
#define DRV_WAVEGEN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#define DRV_WAVE_MAX_POINTS     8192

typedef struct {
    uint32_t sample_rate_hz;
    uint16_t num_points;
    uint8_t  ch_mask;
} DrvWaveConfig_t;

void DRV_WaveGen_Init(void);
bool DRV_WaveGen_Configure(const DrvWaveConfig_t *cfg);
bool DRV_WaveGen_LoadData(const uint32_t *data, uint16_t len);
bool DRV_WaveGen_Start(void);
void DRV_WaveGen_Stop(void);
bool DRV_WaveGen_IsRunning(void);
uint32_t DRV_WaveGen_BuildBSRR(uint8_t state);

#ifdef __cplusplus
}
#endif

#endif /* DRV_WAVEGEN_H */
