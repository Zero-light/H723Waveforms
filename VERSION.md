# H723Waveforms ? Version History

## V1.01 (2026-06-26) ? ADC functional

**Firmware (cubeMX/H723ZGT6/Core)**

- `app_adc.c` / `app_adc.h`: Complete rewrite.
  - Old: DMA1_Stream1 + TIM3 TRGO external trigger + D-Cache invalidate.
  - New: Simple polled HAL_ADC_PollForConversion in main loop,
    buffer 64 samples, send via USB CDC. No DMA, no TIM3, no D-Cache.
  - Multi-channel scan supported in continuous conversion mode.
- `main.c` MX_DMA_Init: Removed DMA1_Stream1 NVIC (no longer needed).
- `main.c` MX_GPIO_Init: Added PA6/PA7 analog switch OPEN bits in SYSCFG_PMCR
  (was missing ? root cause of ADC reading ~300 mV instead of actual voltage).
- `stm32h7xx_it.c`: Removed DMA1_Stream1_IRQHandler and hdma_adc1 extern.

**Host (host/)**

- `widgets/adc_panel.py`:
  - Default sample rate: 50 kHz ? 10 kHz (fits USB FS bandwidth).
  - _max_sample_rate: Added USB FS throughput cap (32000/N).
- `docs/protocol.md`: ADC_CONFIG payload corrected to match code.

**Known issue fixed**

STM32H723 SYSCFG_PMCR register was only opening PA0/PA1 analog switches.
PA6 (ADC1_IN6) switch remained closed ? ADC could not sense the actual pin
voltage ? reading ~300 mV instead of the applied voltage.  This had been
misdiagnosed as a VREF+/wiring issue since 2026-06-23.

---

## V1.00 (2026-06-22) ? Initial
- Waveform generator: TIM2+DMA on PA0-PA5.
- SPI batch register write: SPI1 on PB12-PB15.
- DAC output: DAC1_OUT1 on PA4.
- ADC placeholder (pan-disabled).
- USB CDC binary frame protocol.
- PyInstaller standalone EXE.

## V1.02 (2026-06-26) — ADC poll fix (real root cause)

**Firmware**

- pp_adc.c:
  - EOCSelection changed from ADC_EOC_SEQ_CONV to ADC_EOC_SINGLE_CONV.
    This is the real root cause: with EOC_SEQ_CONV + scan mode, every
    HAL_ADC_PollForConversion + HAL_ADC_GetValue only returned the last
    channel's value (CH7/PA7). PA6's (CH6) data was never read.
  - APP_ADC_Poll() now loops through all enabled channels, polling and
    reading each one individually.
  - Removed the incorrect SYSCFG->PMCR |= ((1u << 6) | (1u << 7)) line.
    On STM32H723, bit 6 = I2C_PB8_FMP, bit 7 = I2C_PB9_FMP — completely
    unrelated to PA6/PA7. The analog switches for PA6/PA7 are controlled by
    GPIO MODER=ANALOG (already configured correctly).
- main.c (firmware + cubeMX): Removed same incorrect PMCR bits from MX_GPIO_Init.

**Correction to V1.01**

The V1.01 claim that adding (1u << 6)/(1u << 7) to SYSCFG PMCR was the
"root cause fix for ADC reading ~300mV" was incorrect. Those bits are
I2C_PB8_FMP / I2C_PB9_FMP, not PA6SO/PA7SO. The ~300mV reading was caused
by the EOC_SEQ_CONV scan bug (only reading PA7, which was floating).
