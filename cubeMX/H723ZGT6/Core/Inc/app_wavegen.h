#ifndef APP_WAVEGEN_H
#define APP_WAVEGEN_H

#include <stdint.h>
#include <stdbool.h>

/* Waveform buffer: max 8192 points, each point is a 32-bit BSRR mask */
#define WAVE_MAX_POINTS     8192

/* Bit positions in BSRR mask for each channel */
#define WAVE_CH0_BIT        0   /* PA0  - XYNC */
#define WAVE_CH1_BIT        1   /* PA1  - SCLK (下降沿触发) */
#define WAVE_CH2_BIT        2   /* PA2  - SH_R */
#define WAVE_CH3_BIT        3   /* PA3  - SH_S */
#define WAVE_CH4_BIT        5   /* PA5  - RST */

typedef struct {
    uint32_t sample_rate_hz;    /* Update rate in Hz */
    uint16_t num_points;        /* Valid points in buffer */
    uint8_t  ch_mask;           /* Enabled channels bitmask */
} WaveConfig_t;

/* Waveform sample buffer (defined in app_wavegen.c). */
extern uint32_t s_waveBuf[WAVE_MAX_POINTS];

void APP_WaveGen_Init(void);
bool APP_WaveGen_Configure(const WaveConfig_t *cfg);
bool APP_WaveGen_LoadData(const uint32_t *data, uint16_t len);
bool APP_WaveGen_Start(void);
bool APP_WaveGen_OneShot(void);     /* run one buffer, then auto-stop + pull pins LOW */
void APP_WaveGen_Stop(void);
bool APP_WaveGen_IsRunning(void);
const WaveConfig_t* APP_WaveGen_GetConfig(void);

#endif /* APP_WAVEGEN_H */
