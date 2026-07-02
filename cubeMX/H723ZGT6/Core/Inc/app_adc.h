#ifndef APP_ADC_H
#define APP_ADC_H

#include <stdint.h>
#include <stdbool.h>

/* ADC channel mapping (ADC1_INx -> pin)
 * IN3  -> PA6    IN7  -> PA7
 * IN9  -> PB0    IN5  -> PB1
 * IN10 -> PC0    IN11 -> PC1
 * IN12 -> PC2    IN13 -> PC3
 */
#define ADC_SCAN_CHANNELS       8
#define ADC_BATCH_SIZE          200   /* samples per USB frame */

/* Operating modes */
typedef enum {
    ADC_MODE_RAW_STREAM = 0,
    ADC_MODE_PACKED     = 1,
    ADC_MODE_BURST      = 2,
} AdcMode_t;

typedef struct {
    uint8_t     ch_mask;
    uint32_t    sample_rate_hz;
    uint8_t     mode;
} AdcConfig_t;

void APP_ADC_Init(void);
bool APP_ADC_Configure(const AdcConfig_t *cfg);
bool APP_ADC_Start(void);
void APP_ADC_Stop(void);
void APP_ADC_Poll(void);
bool APP_ADC_IsRunning(void);

/* Burst capture: sample num_samples in one shot, send as CMD_ADC_DATA frame */
bool APP_ADC_BurstCapture(uint8_t ch_mask, uint16_t num_samples);

#endif /* APP_ADC_H */
