# Board: h723zg_v1

## Overview

First-generation custom board based on STM32H723ZGTx.

- MCU: STM32H723ZGTx
- Core clock: 192 MHz
- ADC: 8-channel ADC1 (PA6/PA7/PB0/PB1/PC0/PC1/PC2/PC3)
- DAC: PA4 (DAC1_OUT1)
- SPI: SPI2 on PB12..PB15
- Waveform GPIO: PA0/PA1/PA2/PA3/PA5
- USB CDC: onboard USB FS
- LED: PG7

## Hardware revision history

| Version | Date | Changes |
|---|---|---|
| v1.0.0 | 2026-06-29 | Initial board support |

## Build

```bash
cd tools
make BOARD=h723zg_v1
```

## Flash

```bash
make BOARD=h723zg_v1 flash
```

## Known issues / notes

- PA6 maps to ADC1_IN3 (not IN6).
- PA6/PA7 AF register is explicitly cleared during GPIO init to prevent leakage.
