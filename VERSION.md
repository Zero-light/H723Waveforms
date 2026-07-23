# H723Waveforms ? Version History

## V2.0 (2026-07-02) — ADC burst capture + Excel 导出 + 快照功能

> 本版跨越 V1.03.2 → V2.2 共 20 个提交，是项目从"波形发生器 + SPI/DAC"升级为"完整 ADC 采集分析系统"的大版本。 host 侧新增独立的 ADC 面板和快照面板；固件侧从轮询改为 TIM3+DMA burst 架构。

### 固件 (`cubeMX/H723ZGT6/Core`)

- `app_adc.c` / `app_adh.h`: **整体重写** — 从 V1.x 的 `HAL_ADC_PollForConversion` 轮询改为 **TIM3 硬件触发 + DMA1_Stream1 burst 采集**架构。
  - `TIM3_Init(rate)`: TIM3 作 ADC 触发时基，PSC/ARR 由采样率计算（TIM_CLK = 192 MHz）。
  - `ADC_DMA_Init()`: DMA1 Stream1 peripheral→memory，half-word，`DMA_NORMAL` 模式。
  - `ADC_ConfigChannels(ch_mask)`: 按掩码顺序配置 ADC rank（ch0→rank1, ch1→rank2…），禁用→重配的安全序列。
  - `StartBurst(ch, ns)`: 启动 DMA + TIM3，`HAL_ADC_ConvCpltCallback` 中停 TIM3 并置 `s_burstDone`。
  - `APP_ADC_SendBurstData()`: 将 DMA buffer 拆帧通过 USB CDC 发回主机（`CMD_ADC_DATA`），含 seq_id + ch_mask + 交织采样数据。
- `main.c OnFrameReceived`: 新增 `CMD_ADC_CONFIG`（设采样率）、`CMD_ADC_BURST`（启动 burst）；burst 完成在主循环通过 `APP_ADC_IsBurstDone()` 触发回发。
- ADC 通道物理映射（ch_mask bit → ADC channel）：
  - bit 0 (0x01) → ADC_CHANNEL_3  (PA6)；bit 1 (0x02) → ADC_CHANNEL_7 (PA7)；
  - bit 2 (0x04) → ADC_CHANNEL_11 (PC1)；bit 3 (0x08) → ADC_CHANNEL_9 (PB0/CLK)；
  - bit 4 (0x10) → ADC_CHANNEL_4 (PC4/XYNC)。
- 采样率限制放宽：prescaler DIV4→DIV2→DIV1（`fadc = 192 / DIV / (PSC+1)/(ARR+1)`），逐步支持 600K / 1000K+ Hz。
- USB TX 重试、stream-mode 波形显示修复（V1.03.2）。

### Host (`host/`)

- **新文件 `host/widgets/adc_panel.py`** — 独立的 ADC 采集面板：
  - 配置行：采样率（spin, 1k–1M Hz）、样本数（100–8192）、每通道勾选框。
  - 4 通道波形显示（pyqtgraph），每通道独立 Y 轴、Y 轴刻度着色。
  - Excel 导出（`adc_to_excel.py`）：含原始数据 / 电压转换 / 修正电压。
  - 通道名称可编辑、可另存为、可重置；burst 防冻结（超时 10 s）。
- **`adc_to_excel.py`**: 原始数据 → PA6/PA7 电压值（×3.3/4095），电压修正（默认 −0.055 V），通道独立 sheet。
- **ADC 配置持久化**（`_save_config` / `_load_config`）：采样率、样本点、通道启用、通道名、偏移等保存到 `~/.h723_adc_config.json`。
- **波形面板 (`wave_panel.py`)**: 规则模板系统、边沿 0-based、预览区重排、波形 Excel 导入/导出、偏移范围 −3~3 → −10~10 V。
- **SPI 面板**: 寄存器勾选发送、预设 Excel 导入/导出。

---

## V2.1 (2026-07-02) — Y 轴标签 + 偏移加宽 + 心跳清理

**Host**

- `adc_panel.py`: Y 轴刻度标签（`setLabel`），偏移 spin 宽度增加；清理心跳相关 UI 杂音。

---

## V2.2 (2026-07-02) — 二次 burst 修复 + 500kHz + 16k 上限

**Firmware (`app_adc.c`)**

- 修复第二次 burst 失败：每次 `StartBurst` 前完全复位 ADC + DMA（`HAL_ADC_Stop_DMA` + `HAL_DMA_Abort` + `hdma_adc1.Init.Mode = DMA_NORMAL`）。
- 采样率上限 500 kHz，burst 样本上限 16 k（`ADC_BURST_MAX_SAMPLES`）。

**Host**

- `adc_panel.py`: 样本数上限调整至匹配固件 16 k。

---

## V2.3 (2026-07-03) — ADC 三通道 + 电压修正自由调节 + SPI 增强

### Firmware (`cubeMX/H723ZGT6/Core`)

- ADC 第三通道 PC4（bit 2 → ADC_CHANNEL_11）上线；PC4/XYNC 和 PB0/CLK 映射补全。

### Host

- `adc_panel.py`:
  - 第三通道 PC4 + 默认电压修正 −0.055 V + 三通道独立 Y 轴。
  - 电压修正由固定偏移改为**自由可调 spin box** + "一键保存"。
  - 通道名编辑 + "另存为Excel" + 重置按钮 + burst 防冻结。
  - ADC Y 轴刻度着色、通道名称标签样式优化、标签字体/位置微调。
  - ADC 配置持久化（含 voltage correction、offsets）。
- `wave_panel.py`: 波形时序规则 Excel 导入/导出。
- `spi_panel.py`: SPI 寄存器勾选发送 + 预设 Excel 导入/导出。
- ADC 采样率上限提升至 1,000,000 Hz（1 MHz）。
- 清理旧版 V1.0.1 便携包。

---

## V2.4 (2026-07-03) — 波形预览周期控件

**Host**

- `wave_panel.py`: 波形预览区新增"周期"控件，可设重复显示 N 个完整周期。

---

## V2.6 (2026-07-10) — ADC 合并/独立切换 + X 轴范围 + 电压读数修复

### Host (`host/widgets/adc_panel.py`)

- **合并/独立显示切换按钮**（参考快照页面 `btn_toggle_plots` 风格）：
  - 新增 `btn_toggle_merge`（可勾选按钮，文本"合并显示 ↔ 独立显示"）。
  - 使用 `QStackedWidget` 管理两页：
    - Page 0 — 3 个独立 plot（QSplitter 垂直堆叠，X 轴联动，Y 轴各自独立）。
    - Page 1 — 单张合并图（PA6/PA7/PC4 三通道同 XY 轴叠加）。
  - 合并图有独立的十字光标 + 三通道电压同时读数。
  - 模式自动保存/加载（`view_mode` 字段，持久化到 `~/.h723_adc_config.json`）。
- **X 轴初始范围固定 0–1000**：独立图和合并图都加了 `setXRange(0, 1000, padding=0)`，关闭 X 轴 auto-range 防止数据更新时视图跳动。
- **电压读数修复**：点击波形读取电压时，不再叠加偏移（Offset）值。偏移仅用于视觉移动波形位置，电压读数始终显示该样本的真实输入电压。
- **偏移自动保存**：偏移值变更触发 `_schedule_update() + _save_config()`，启动时自动加载。

---

## V2.5 (2026-07-09) — 快照页面(XYNC+CLK+8×8像素) + 固件XYNC帧同步触发

> 今天主对话中的所有改动。

### 新增快照面板 (`host/widgets/snap_panel.py`)

- 独立于 ADC 面板的快照页面，采集 **PA6 + PA7 + CLK(PB0) + XYNC(PC4)** 四通道。
  - 4 个联动 X 轴波形显示：PA6/PA7 模拟电压，CLK/XYNC 数字 0/1。
  - 通道名称可编辑（持久化到 `~/.h723_snap_config.json`）。
- **8×8 像素重排预览**（PA7）：
  - 基于 PIXEL_MAP zigzag 读出顺序，8×8 色阶表格显示各像素原始值（黑=0, 白=4095）。
  - min/max 范围手动 spin box（**90 px 宽**，完整显示 4095）+ 自动拉伸 toggle。
  - 状态栏显示范围 + 当前 TIFF 路径。
- **Excel 导出（5 个 sheet）**：原始数据 / CLK 压缩 / 像素映射 / Pixel 数据 / 像素重排 8×8。
  - **CLK 压缩**：每个 CLK 半周期求 XYNC 多数表决（≥50%→1），PA6/PA7 均值。
  - **像素提取**：从 CLK 压缩表中找第一个 XYNC=1 区域，最后一个 CLK=1 判为 pixel 1，之后每遇 CLK=1 编号 +1，直到 64 像素。
  - **16-bit TIFF**（`I;16` 模式，8×8）：随 Excel 同目录生成，可直接 ImageJ 打开。
- **"另存为Excel"** 独立按钮（`QFileDialog.getSaveFileName`，时间戳文件名，不会被覆盖）。
- **快照多次自动覆盖**：每次快照写固定文件名 `snap_latest.xlsx`（+ `snap_latest.tiff`），不新建带时间戳的文件；只有"另存为Excel"才产出独立保留的文件。

### Firmware (`cubeMX/H723ZGT6/Core/Src/app_adc.c`)

- **新增 `WaitForXyncRise()`** — XYNC（PC4）帧同步等待：
  - 临时将 PC4 从 analog 切到 GPIO input；先等 XYNC=0，再等 **XYNC=1 上升沿**；触发后切回 analog。
  - 两个等待各 500k 循环超时兜底，避免 XYNC 停振时死锁。
- **XYNC 门控 burst**：`StartBurst` 仅在通道掩码**包含 XYNC（bit 4 = 0x10）**时才等帧同步；ADC 页面（掩码 0x01~0x07，不含 XYNC）不受影响，直接采集。
  - 快照页面含 XYNC → burst 起点与帧同步对齐，所有采样点均为有效数据。
  - ADC 页面不含 XYNC → 跳过等待，行为同旧版。

### 文档

- 新增 `ADC采样率问题根因分析与修改记录.md`（采样率自测与根因）。

### 已知问题

- 固件改动需**重新编译并烧录**到 STM32H723 才生效；host .exe 已通过 rebuilt 包含快照面板改动。

---

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
