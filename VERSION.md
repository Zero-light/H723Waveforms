# H723Waveforms ? Version History

## V1.03.1 (2026-06-30) — WAVE_CTRL state-machine fix + IOC/packaging corrections

**Firmware (`cubeMX/H723ZGT6/Core/Src/drv_wavegen.c`)**

- `DRV_WaveGen_Configure()`: validate `ch_mask` (reject 0 and reserved bits); validate timer frequency BEFORE overwriting `s_config` so a failed Configure leaves no stale state.
- `DRV_WaveGen_Start()`: validate `ch_mask`; check return values of `BSP_DMA_Start` / `BSP_TIM_BaseStart` and roll back (DMA stop, pins→input) on failure; `s_running` is only set to `true` after all hardware operations succeed — a half-started Start can no longer wedge the state machine.
- `DRV_WaveGen_Stop()`: reset `s_data_valid = false` so that after a Stop, the old waveform data cannot be re-used for a new Start without an explicit LoadData.

**IOC (`cubeMX/H723ZGT6/H723ZGT6.ioc`)**

- PA4: changed from `GPIO_Output` to `GPIO_Input` with `RESERVED_for_DAC1_OUT1` label — prevents CubeMX from generating code that drives PA4 as push-pull while the firmware uses it as DAC analog output.

**Clock (`cubeMX/H723ZGT6/Core/Src/bsp_clock.c`)**

- Added comment block explaining that the firmware uses HSI (not HSE as configured in the IOC).

**Host packaging (`host/H723Waveforms.spec`)**

- Added `openpyxl` to `hiddenimports` — PyInstaller was not bundling openpyxl automatically, causing intermittent EXE startup crashes (empty log files).

---

## V1.03 (2026-06-30) — GPIO/waveform safety hardening

**Goal**: prevent board damage (VCC-to-GND short, sustained sink current, thermal runaway) caused by software misconfiguration.

**Firmware (`cubeMX/H723ZGT6/Core` and `便捷开发工具/烧录文件/H723ZGT6/Core`)**

- `app_main.c`:
  - Removed the power-on auto-start of a default 1 kHz waveform on PA1.
  - Added a 60 s software watchdog: waveform auto-stops if the host never sends a stop command.
  - `CMD_WAVE_CONFIG` now rejects `ch_mask == 0` or reserved bits.
  - `CMD_WAVE_DATA` now rejects oversized payloads instead of silently truncating.
- `drv_wavegen.c`:
  - Waveform output pins are pulled HIGH via BSRR **before** switching to `OUTPUT_PP`.
  - `DRV_WaveGen_Stop()` now forces all enabled pins HIGH before returning them to floating input.
  - `DRV_WaveGen_LoadData()` rejects all-zero BSRR arrays and patterns where an enabled channel never goes HIGH.
  - Default GPIO speed for wave pins reduced from `VERY_HIGH` to `MEDIUM`.
- `bsp_init.c`:
  - Added `BSP_IWDG_Init()` call after clock init.
  - On-board LED PG7 is set HIGH before its `OUTPUT_PP` initialization.
- New files `bsp_iwdg.c` / `bsp_iwdg.h`:
  - Independent watchdog: ~1 s timeout. Refreshed every main-loop iteration.
  - If the main loop hangs, the MCU resets and all GPIO return to floating input.

**Host (`host/` and `便捷开发工具/V1.0.1/src/`)

- `widgets/wave_panel.py`:
  - Fixed CLK marker bug (`"?"` / `"???"` string comparisons).
  - New table rows and invalid cell input now default to HIGH (`"1"`) instead of LOW.
  - Added `_check_waveform_safety()` / `_warn_and_confirm()` helpers.
  - "应用时序", Excel import, and "加载模式到设备" now warn about long-low / all-low / never-high channels and let the user cancel.
  - Existing all-zero BSRR interception is preserved.

**Project layout**

- Deleted the unused `firmware/` directory; the CubeMX project is now the single source of truth.
- Rebuilt `H723Waveforms.exe` for both `host/dist/` and `便捷开发工具/V1.0.1/`.

---

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
