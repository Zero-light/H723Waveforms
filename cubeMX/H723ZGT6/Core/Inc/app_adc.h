#ifndef APP_ADC_H
#define APP_ADC_H

#include <stdint.h>
#include <stdbool.h>

/* ── Layer 3: dual-channel scan with TIM3 hardware trigger ────────── */
void APP_ADC_InitDual(uint32_t sample_rate_hz);
void APP_ADC_ReadDual(uint16_t raw[2]);
void APP_ADC_StartTim3(void);
void APP_ADC_StopTim3(void);

#endif /* APP_ADC_H */
