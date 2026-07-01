#ifndef APP_DAC_H
#define APP_DAC_H

#include <stdint.h>
#include <stdbool.h>

/* DAC1_OUT1 on PA4, 12-bit, 0~3.3 V */
#define DAC_VREF  3.3f
#define DAC_MAX   4095

void APP_DAC_Init(void);
bool APP_DAC_SetValue(uint16_t value);

#endif /* APP_DAC_H */
