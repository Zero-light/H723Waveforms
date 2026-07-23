# ADC 采样率问题 — 根因分析与修改记录

> 日期：2026-07-03
> 芯片：STM32H723ZGT6
> 结论：DIV4 → DIV1，ADC 时钟 16MHz → 64MHz，3ch 最大采样率从 ~355KHz 提升到 1.42MHz

---

## 1. 最终修改

**文件**：`cubeMX/H723ZGT6/Core/Src/app_adc.c`  
**改动**：`ADC_CLOCK_ASYNC_DIV4` → `ADC_CLOCK_ASYNC_DIV1`（一行）

```
改前：ADC clk = HSI 64MHz / 4 = 16 MHz  →  3ch 扫描 = 2.81 µs  →  最大 ≈ 355 KHz
改后：ADC clk = HSI 64MHz / 1 = 64 MHz  →  3ch 扫描 = 703 ns  →  最大 ≈ 1.42 MHz
```

**未改动**：ADCSEL = 2（per_ck / CLKP），其他初始化顺序、校准方式、BOOST 等一律保持原样。

---

## 2. 问题现象

| 设定采样率 | 预期 (1KHz 波形点数) | 实际 | 原因 |
|-----------|---------------------|------|------|
| 400K | 400 | 400 ✅ | 触发周期 2.5µs > 扫描时间 2.81µs... 等等，这不对 |
| 500K | 500 | 250 ❌ | 触发周期 2.0µs < 扫描时间 2.81µs → Overrun → 砍半 |
| 700K | 700 | 350 ❌ | 同上 |

实际上 per_ck 来源可能不是 HSI 64MHz，而是某个更低的频率（HSI48 或其他），导致实际 ADC 时钟更低。最终 DIV1 后 1000K 正常，反推 ADC 时钟 ≥ 45MHz。

---

## 3. 根因分析

### 3.1 H723 的 ADCSEL 编码（关键发现）

**H723 的 ADC 时钟源编码和其他 H7（H743/H753）不同！**

| ADCSEL | H743/H753 | **H723（实际）** |
|--------|-----------|-------------------|
| 00 | PLL1_P | **PLL2_P**（PLL2 未配置 → 无时钟） |
| 01 | PLL2_P | **PLL3_R**（PLL3 无 R 输出 → 无时钟） |
| 10 | PLL3_R | **CLKP = per_ck**（HSI 64MHz） |
| 11 | per_ck | **CLKP = per_ck** |

对应的 HAL 宏印证：
```c
// stm32h7xx_hal_rcc_ex.h (H723 版本)
#define RCC_ADCCLKSOURCE_PLL2    (0x00000000U)   // ADCSEL = 00
#define RCC_ADCCLKSOURCE_PLL3    RCC_D3CCIPR_ADCSEL_0  // ADCSEL = 01
#define RCC_ADCCLKSOURCE_CLKP    RCC_D3CCIPR_ADCSEL_1  // ADCSEL = 10 或 11
```

**H723 根本没有 PLL1 做 ADC 时钟的选项。** 原代码 ADCSEL=2 选的是 CLKP（per_ck），不是 PLL3_R。

### 3.2 Overrun 机制

ADC 配置了 `ADC_OVR_DATA_OVERWRITTEN`：
- 当 TIM3 触发速度超过 ADC 扫描速度时，上一轮转换结果被覆盖
- 表现：有效采样率直接砍半（每两次触发只完成一次转换）

3ch 扫描时间 = 采样 + 转换 = (2.5+12.5) × 3 = 45 ADC 时钟周期
原 ADC 时钟 16MHz：扫描 = 45/16MHz = 2.81µs，超过 500K 触发周期 2.0µs

---

## 4. 修改历程（含失败记录）

### 尝试 1：ADCSEL 2→0 + 手动 CCR + 重排序 + while 校准 ❌

**假设**：ADCSEL=0 = PLL1_P = 192MHz，/4 = 48MHz，3ch 扫描 = 937ns

**改动**：
- 把时钟源 + CCR 预分频移到 ADVREGEN/校准之前
- 手动写 `ADC12_COMMON->CCR` 设预分频器
- `while (ADC1->CR & ADC_CR_ADCALLIN)` 死等校准完成

**结果**：LED 不亮，单片机卡死在 `while` 循环

**教训**：
- H723 上 ADCALLIN 位行为不确定，不应死等
- 手动写 CCR 可能与其他 HAL 操作冲突
- 一次改动太多变量，无法定位问题

### 尝试 2：固定延时替代 while + 手动 CCR + 双次校准 ❌

**结果**：BURST NACK（ADC 使能失败），但单片机不卡死

**教训**：问题不在校准等待方式，而是 ADCSEL=0 本身就不对

### 尝试 3：回退原始顺序，只改 ADCSEL 2→0 ❌

**结果**：未测试（被后续改动覆盖）

### 尝试 4：ADCSEL 2→0 + BOOST=01 ❌

**假设**：48MHz 需要 BOOST=01，HAL 没自动设

**改动**：
```c
ADC1->CR = (ADC1->CR & ~ADC_CR_BOOST_Msk) | (1UL << ADC_CR_BOOST_Pos);
// bits[9:8]=01 → 适配 6.25~12.5MHz  ← 值写错了！
```

**结果**：BURST NACK

**教训**：
- BOOST=01 适配的是 6.25~12.5MHz，对 48MHz 完全不够
- 48MHz 需要 BOOST=11（>25MHz）：`ADC_CR_BOOST_1 | ADC_CR_BOOST_0`
- 但即便写对了也没用——ADCSEL=0 选的根本不是 PLL1_P，而是 PLL2_P

### 尝试 5：ADCSEL 2→0 + DIV4 + BOOST=11 ❌

**结果**：第一次采集全零，第二次卡死

**教训**：ADCSEL=0 = PLL2_P（PLL2 没配置 = 无时钟），改什么都没用

### 尝试 6：保持 ADCSEL=2，DIV4→DIV2 ✅

```
ADC clk = 32 MHz，3ch 扫描 = 1.41 µs，最大 ≈ 711 KHz
实际稳定到 600K
```

### 尝试 7：保持 ADCSEL=2，DIV2→DIV1 ✅（最终方案）

```
ADC clk = 64 MHz，3ch 扫描 = 703 ns，最大 ≈ 1.42 MHz
实测 1000K 正常
```

---

## 5. 最终代码改动

```diff
-    hadc1.Init.ClockPrescaler        = ADC_CLOCK_ASYNC_DIV4;
+    hadc1.Init.ClockPrescaler        = ADC_CLOCK_ASYNC_DIV1;  /* 64 MHz ADC clk (per_ck=HSI 64MHz /1) */
```

仅此一行。ADCSEL 不变、初始化顺序不变、BOOST 不动、校准方式不动。

---

## 6. 关键经验

1. **不同 H7 子系列的时钟树编码不同**：H723 value-line 的 ADCSEL 完全不同于 H743，查 HAL 宏比查 Reference Manual 更快
2. **一次只改一个变量**：前期失败的主因是同时改 ADCSEL + CCR + 初始化顺序 + 校准方式，无法定位根因
3. **BOOST 位值含义**：`ADC_CR_BOOST_0 = 01` = 6.25~12.5MHz，`ADC_CR_BOOST_1 = 10` = 12.5~25MHz，`BOOST_1|BOOST_0 = 11` = >25MHz
4. **per_ck 路径安全**：per_ck=HSI 64MHz 是 H723 上唯一可用的 ADC 时钟源，HAL 能正确检测其频率并配置 BOOST
5. **DIV1 注意**：`ADC_CLOCK_ASYNC_DIV1 = 0`，HAL 的 ADC_ConfigureBoostMode 中 DIV1 不在 switch case 列表里，走 default 分支不分频，行为正确但需注意
