# STM32H723ZGT6 ADC 全链路分析与经验教训总结

> 基于本工程全部日志（2026-06-22 ~ 2026-07-17）中的 ADC 调试记录
> 芯片：STM32H723ZGT6 (Cortex-M7 @ 192MHz, 12-bit ADC)
> VREF+：3.3V

---

## 目录

1. [第一阶段：ADC 基础知识](#一adc-基础知识)
2. [第二阶段：本项目 ADC 全链路讲解（电压数值全程追踪）](#二本项目-adc-全链路讲解)
3. [第三阶段：遇到的错误与根因](#三遇到的错误与根因总结)
4. [第四阶段：可利用的 ADC/DMA 功能](#四可利用的-adcdma-功能)
5. [第五阶段：最佳实践与注意事项](#五最佳实践与注意事项)
6. [第六阶段：最终固件配置速查](#六最终固件配置速查)

---

## 一、ADC 基础知识

### 1.1 什么是 ADC

ADC (Analog-to-Digital Converter) 将模拟电压转换为数字量。STM32H723 内置 **12 位逐次逼近型 (SAR) ADC**，可以采集 0 ~ VREF+ 之间的模拟电压。

### 1.2 核心参数

| 参数 | 含义 | 本项目值 |
|------|------|---------|
| **分辨率** | ADC 输出的二进制位数 | **12 bit** → 0~4095 |
| **参考电压 VREF+** | 最大可测模拟电压 | **3.3V** |
| **LSB (最低有效位)** | 每个数字码对应的电压 | 3.3V / 4096 = **0.806 mV** |
| **采样率** | 每秒采集样本数 | 最大 ~1.42 MHz (3ch) |
| **ADC 时钟** | ADC 内核工作时钟 | **64 MHz** (DIV1) |
| **采样时间** | 采样电容充电时间 | **2.5 ADC 时钟周期** |
| **转换时间** | SAR 逐次逼近时间 | 12.5 ADC 时钟周期 (12-bit) |

### 1.3 电压→数字 转换公式

```text
RAW = (Vin / VREF+) × 4095      (单端模式)
Vin = RAW × VREF+ / 4095        (反算电压)

差分模式 (本项目也支持):
RAW_diff = (Vin_p - Vin_n) / VREF+ × 2048 + 2048
Vin_diff = (RAW_diff - 2048) × VREF+ / 2048
```

**数值示例 (单端)**：

| Vin | RAW | 说明 |
|-----|-----|------|
| 0 V | 0 | GND |
| 1.65 V | ~2048 | 中点 |
| 3.3 V | 4095 | 满量程 |
| 0.300 V | ~372 | 浮空引脚典型噪声值 ⚠️ |

### 1.4 ADC 转换时序

```text
单个通道总时间 = 采样时间 + 逐次逼近转换时间
              = (SMP + 12.5) 个 ADC 时钟周期

本项目最小：2.5 + 12.5 = 15 ADC clk = 15/64MHz = 234 ns

N 通道扫描总时间 = N × (SMP + 12.5) ADC clk
3ch：3 × 15 = 45 ADC clk = 45/64MHz = 703 ns
```

### 1.5 ADC 工作模式

| 模式 | 说明 | 本项目使用 |
|------|------|-----------|
| **单次转换** | 触发一次，转换一个通道 | ❌ |
| **连续转换** | 启动后不停转换 | ❌ |
| **扫描模式** | 按 Rank 顺序轮转多个通道 | ✅ |
| **非连续模式** | 每次触发只转换一个通道 | ❌ |
| **外部触发** | TIM/EXTI 等硬件触发 | ✅ TIM3 TRGO |
| **DMA 传输** | 自动搬运到内存 | ✅ DMA1_Stream1 |

### 1.6 触发源

本项目使用 **TIM3 TRGO (Update 事件)** 作为 ADC 外部触发源：

```text
TIM3_CLK = 192 MHz (APB1 timer clock)
TIM3 配置: ARR=1, PSC = clocks/(rate×2) - 1
每个 TRGO 触发一次 ADC 扫描（采集 N 个通道各一次）
```

---

## 二、本项目 ADC 全链路讲解

### 2.1 系统时钟树（ADC 相关部分）

```text
HSI 64MHz
    │
    ├─→ PLL1 (×24/M=4/P=2) → 192 MHz → SYSCLK / HCLK / APB1 Timer
    │
    └─→ CLKP (per_ck) = HSI 64MHz ──→ ADCSEL=2 ──→ ADC 内核
                                      (D3CCIPR)       │
                                                DIV1 (不分频)
                                                      │
                                                ADC clk = 64 MHz
```

**关键点**：H723 的 ADCSEL 编码与 H743 不同！见下方错误 #3。

### 2.2 信号全链路（从引脚到上位机显示）

```text
┌─────────────────────────────────────────────────────────────────────┐
│ 物理层面                                                              │
│                                                                       │
│  待测信号 ──→ 杜邦线 ──→ PA6 排针 ──→ STM32H723 内部模拟开关            │
│                                        (GPIO MODER=ANALOG 自动打开)     │
│                                            │                          │
│                                   ADC1_IN3 (通道 3)                    │
│                                            │                          │
│                                  采样保持电容 C_sh ≈ 5pF                │
│                                  充电时间 = 2.5 ADC clk = 39 ns         │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ ADC 转换层面 (12-bit SAR)                                             │
│                                                                       │
│  Vin ──→ 采样电容 ──→ 比较器 ──→ SAR 逻辑 ──→ 12-bit 结果              │
│                      逐次逼近 12 个周期                                │
│                                                                       │
│  总时间 = 2.5 + 12.5 = 15 ADC clk = 15/64MHz = 234 ns                │
│                                                                       │
│  RAW = (Vin / 3.3V) × 4095                                           │
│                                                                       │
│  例：Vin = 1.650V → RAW = 1.650/3.3 × 4095 = 2047.5 ≈ 2048           │
│       Vin = 3.300V → RAW = 3.300/3.3 × 4095 = 4095                   │
│       Vin = 0.300V → RAW = 0.300/3.3 × 4095 = 372                    │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ DMA 传输层面                                                          │
│                                                                       │
│  ADC1->DR (16-bit) ──→ DMA1_Stream1 (Periph→Mem, HalfWord)           │
│       │                          │                                    │
│   每个 EOC 触发                  自动递增写入 s_rawBuf[]                │
│   DMA 请求                                                             │
│                                                                       │
│  多通道交织存储格式 (ch_mask=0x07, 3ch, 1000 samples):                    │
│  s_rawBuf = [CH0_s0, CH1_s0, CH2_s0, CH0_s1, CH1_s1, CH2_s1, ...]    │
│              共 3 × 1000 = 3000 个 uint16_t                            │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ USB CDC 传输层面                                                      │
│                                                                       │
│  APP_ADC_SendBurstData() 将 DMA buffer 拆帧发送                         │
│                                                                       │
│  帧格式: [seq_id:2] [ch_mask:1] [reserved:1] [交织数据...]              │
│  每帧最多 (2048-4)/(2×N_ch) 个样本/通道                                  │
│  例：3ch → 每帧最多 340 个样本/通道                                       │
│                                                                       │
│  USB HS (内建 FS PHY) → 虚拟串口 (COM16)                              │
│  波特率无意义(USB CDC 无视)，实际速率受 Full-Speed 12Mbps 限制             │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│ 上位机处理层面                                                        │
│                                                                       │
│  SerialLink 读线程 → 帧解析 → AdcPanel.on_frame()                      │
│       │                                                                 │
│   按 ch_mask 拆分交织数据到各通道 buffer                                │
│       │                                                                 │
│   pyqtgraph 渲染:                                                       │
│     raw → volts = RAW × 3.3 / 4095 - offset                           │
│                                                                       │
│   降采样算法: min/max envelope (N>2000 时)                              │
│   Excel 导出: 原始RAW + 电压值 + 修正电压                                │
│                                                                       │
│   例：RAW = 2048                                                       │
│       → 显示电压 = 2048 × 3.3 / 4095 - (-0.055)                       │
│       → = 1.650 + 0.055 = 1.705 V (含修正偏置)                          │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.3 具体数值走一遍（3 通道扫描 @ 500KHz）

```text
场景：采样率 500 KHz，3 个通道 (PA6=1.650V, PA7=0.800V, PC1=2.500V)

第 1 步 — TIM3 触发
  TIM3_CLK = 192 MHz
  TIM3 输出频率 = 500 KHz
  PSC = 192000000 / (500000 × 2) - 1 = 191
  ARR = 1
  → 每 2 μs 产生一次 TRGO 上升沿

第 2 步 — ADC 响应对应触发
  ADC 收到 TRGO → 启动一次扫描序列
  Rank 1: CH3 (PA6) 采样 → 转换 → RAW = 1.65/3.3×4095 = 2048
  Rank 2: CH7 (PA7) 采样 → 转换 → RAW = 0.80/3.3×4095 = 993
  Rank 3: CH11(PC1) 采样 → 转换 → RAW = 2.50/3.3×4095 = 3102

  单次扫描耗时 = 3 × (2.5 + 12.5) = 45 ADC clk = 703 ns
  703 ns < 2 μs (触发周期) → 不 overrun ✅

第 3 步 — DMA 搬运
  s_rawBuf[0] = 2048  (PA6, 第1次)
  s_rawBuf[1] =  993  (PA7, 第1次)
  s_rawBuf[2] = 3102  (PC1, 第1次)
  s_rawBuf[3] = 2049  (PA6, 第2次)  ← 电压可能有微小波动
  ...

第 4 步 — USB 发送
  seq_id=0: ch_mask=0x07, 数据=[2048,993,3102,2049,992,3101,...]
  每帧 340 spc/ch × 3ch × 2B = 2040 字节 payload

第 5 步 — 上位机显示
  PA6: volts = 2048 × 3.3/4095 - (-0.055) = 1.705V
  PA7: volts =  993 × 3.3/4095 - (-0.055) = 0.855V
  PC1: volts = 3102 × 3.3/4095 - (-0.055) = 2.555V
```

---

## 三、遇到的错误与根因总结

### 错误总览

| # | 错误 | 现象 | 根因 | 修复 | 后果严重程度 |
|---|------|------|------|------|------------|
| 1 | ADC 读数始终 ~300mV | PA6 接什么都读 372 | **ADC 内核时钟源未配置** (ADCSEL=00→PLL2=无时钟) | RCC_ADCCLKSOURCE_CLKP | 🔴 致命 |
| 2 | ADC 读数始终 ~300mV | 同上 | **通道号映射错误** (PA6 用 CH6 而非 CH3) | ADC_CHANNEL_3 | 🔴 致命 |
| 3 | ADC 读数始终 ~300mV | 同上 | **EOC_SEQ_CONV 导致只读最后一个通道** | EOC_SINGLE_CONV | 🔴 致命 |
| 4 | ADCSEL 改 0 卡死 | LED 不亮，while 死等 | **ADCSEL=0 = PLL2_P(无时钟)**，且 ADCALLIN 死等 | 保持 ADCSEL=2 | 🔴 致命 |
| 5 | ADCSEL=0 + BOOST=01 | BURST NACK | **BOOST 值写错** (01 适配 6.25~12.5MHz，48MHz 需 11) | — | 🟡 严重 |
| 6 | DIV4 时 500KHz 丢一半 | 期望 500 点，实际 250 | **Overrun**：扫描时间 2.81μs > 触发周期 2.0μs | DIV4→DIV1 | 🟡 严重 |
| 7 | Rank 参数传裸整数 | SQR1=0x06，SQ1=0 | **`sChan.Rank = rank + 1`** 而非 LL_ADC_REG_RANK_x | 用 RANK_TABLE[] | 🔴 致命 |
| 8 | 第二次 burst 失败 | 第一次正常，第二次无数据 | **DMA 未完全复位** (Mode 残留 CIRCULAR) | HAL_DMA_Abort + DMA_NORMAL | 🟡 严重 |
| 9 | 上位机显示波形空洞 | 大量低电平段 | **磁盘 fsync 阻塞串口读线程** → OS 缓冲区溢出 | 关闭逐帧日志 | 🟡 严重 |
| 10 | 采集卡死（第二次点击无响应） | 按钮灰色 | **序列号竞争**：残留帧提前达到预期样本数 | 增加 burst seq 号校验 | 🟡 严重 |
| 11 | SYSCFG PMCR 误写 | 写入 bit6/7 | I2C_PB8_FMP / PB9_FMP，与 ADC 无关 | 删除 PMCR 操作 | 🟢 无效但无害 |
| 12 | DIV16 编译错误 | 编译失败 | H723 value-line 只有 DIV1/2/4 | 改为 DIV4 | 🟢 编译问题 |
| 13 | HAL_ADC_Start 卡 BUSY | 双缓冲切换失败 | HAL 状态机未正确处理停止→重启 | 去掉 BSP_ADC_Start | 🟢 驱动问题 |

### 错误详情

#### 错误 #1：ADC 内核时钟源未配置 🔴 致命

**现象**：ADC 读数始终 ~300mV (RAW ≈ 372)，无论 PA6 接什么电压。

**根因**：H723 芯片复位后 `RCC->D3CCIPR.ADCSEL = 00` → 选 **PLL2 作为 ADC 内核时钟源**，但 PLL2 从未被配置使能。ADC 内核无时钟 → 转换结果不确定 → 读到的 372 是噪声/随机值。

**修复**：
```c
RCC_PeriphCLKInitTypeDef p = {0};
p.PeriphClockSelection = RCC_PERIPHCLK_ADC;
p.AdcClockSelection    = RCC_ADCCLKSOURCE_CLKP;   // ADCSEL=2, per_ck=HSI 64MHz
HAL_RCCEx_PeriphCLKConfig(&p);
```

**教训**：
- `__HAL_RCC_ADC12_CLK_ENABLE()` 只使能 **APB 总线时钟**（寄存器访问用），不是 ADC 内核时钟
- ADC 内核时钟源必须单独配置
- H723 的 ADCSEL 编码与 H743 不同，不能照搬

#### 错误 #2：PA6 通道号映射错误 🔴 致命

**现象**：同上，读数始终 ~300mV。

**根因**：PA6 在 H723 上对应 `ADC1_IN3` → HAL 宏 `ADC_CHANNEL_3`。但代码中错误地使用了 `ADC_CHANNEL_6`（在 H723 上可能对应其他浮空引脚或内部通道）。

**H723 value-line 实际通道映射（与 H743 不同！）**：

| 引脚 | 实际 ADC 通道 | HAL 宏 | 原代码（错） |
|------|-------------|--------|-------------|
| PA6 | ADC1_IN3 | ADC_CHANNEL_3 | ADC_CHANNEL_6 ❌ |
| PA7 | ADC1_IN7 | ADC_CHANNEL_7 | ADC_CHANNEL_7 ✅ |
| PB0 | ADC1_IN9 | ADC_CHANNEL_9 | ADC_CHANNEL_8 ❌ |
| PC1 | ADC1_IN11 | ADC_CHANNEL_11 | ADC_CHANNEL_9 ❌ |
| PC4 | ADC1_IN4 | ADC_CHANNEL_4 | — |

**教训**：不同 STM32H7 子系列的通道映射完全不同，必须查阅对应型号的 Datasheet。

#### 错误 #3：EOCSelection 错误 🔴 致命

**现象**：双通道时只读到 PA7 的数据，PA6 的数据永远是 PA7 的浮空值 (~300mV)。

**根因**：
```c
hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;  // 等整个序列结束
hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
```
在扫描模式下 EOC_SEQ_CONV 等 **整个序列** 完成后才置 EOC 标志，此时 DR 寄存器中只有 **最后一个通道** 的值 (PA7)。PA6 的数据被完全丢弃。

**修复**：
```c
hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;  // 每个通道完成即置 EOC
```
然后在 APP_ADC_Poll 中循环读取：
```c
for (i = 0; i < numEnabled; i++) {
    HAL_ADC_PollForConversion(&hadc1, 1);
    buf[idx++] = HAL_ADC_GetValue(&hadc1);
}
```

#### 错误 #4-5：ADCSEL 改 0 的失败经历 🔴/🟡

**教训**：**一次只改一个变量**。前面 5 次尝试同时改了 ADCSEL + CCR + 初始化顺序 + 校准 + BOOST，无法定位问题。

**H723 ADCSEL 编码真相**（与 H743 完全不同！）：

| ADCSEL | H743/H753 | **H723（实际）** |
|--------|-----------|-----------------|
| 00 | PLL1_P | **PLL2_P** → 没配置 → 无时钟 |
| 01 | PLL2_P | **PLL3_R** → 无 R 输出 → 无时钟 |
| 10 | PLL3_R | **CLKP = per_ck** = HSI 64MHz ✅ |
| 11 | per_ck | **CLKP = per_ck** = HSI 64MHz ✅ |

H723 根本**没有 PLL1 做 ADC 时钟的选项**！

#### 错误 #6：Overrun 导致有效采样率砍半 🟡

**根因**：
```text
ADC clk = 16 MHz (DIV4)
3ch 扫描时间 = (2.5+12.5) × 3 = 45 ADC clk = 45/16MHz = 2.81 μs
500KHz 触发周期 = 2.0 μs < 2.81 μs → Overrun → 数据覆盖 → 每两次触发才完成一次
```

`ADC_OVR_DATA_OVERWRITTEN` 导致无报警但数据静默丢失。

**修复**：DIV4→DIV1，ADC clk=64MHz，3ch 扫描=703ns < 2.0μs。

#### 错误 #7：Rank 参数编码 🔴 致命

**根因**：HAL 的 Rank 参数不是简单的 1,2,3... 整数，而是包含**寄存器偏移量编码**的位掩码。

```c
// ❌ 错误
sChan.Rank = rank + 1;   // rank=0 → Rank=1 → 左移1位到错误位置

// ✅  正确
static const uint32_t RANK_TABLE[] = {
    ADC_REGULAR_RANK_1, ADC_REGULAR_RANK_2, ...
};
sChan.Rank = RANK_TABLE[rank];
```

`LL_ADC_REG_RANK_1` 包含 `ADC_SQR1_REGOFFSET | 6` 编码，左移 6 位才能写到 SQR1 的 SQ1 正确位域 [10:6]。传 1 则写到 [5:1]，完全是另一个位域。

**调试关键**：直接打印硬件寄存器 `ADC1->SQR1` 发现了问题。
```text
SQR1 = 0x00000006 → SQ1 = 0 → 在采样通道 0 而不是通道 3！
```

#### 错误 #8：第二次 burst 失败 🟡

**根因**：第一次 burst 后 DMA 处于某种残留状态，再次调用 `HAL_ADC_Start_DMA` 时 DMA 没有正确重新配置。

**修复**：每次 burst 前完全复位：
```c
HAL_ADC_Stop_DMA(&hadc1);
HAL_DMA_Abort(&hdma_adc1);
hdma_adc1.Init.Mode = DMA_NORMAL;   // 确保非 CIRCULAR
HAL_DMA_Init(&hdma_adc1);
```

#### 错误 #9：上位机数据丢失 🟡

**根因**：串口读线程每秒处理 ~50 帧数据，每帧调用 `flush()` 强制磁盘同步 → 磁盘 I/O 阻塞串口线程 → OS 串口接收缓冲区 (4096 字节) 溢出 → 大量数据帧丢失。

**修复链**：
1. 关闭逐帧日志 (ADC_DATA_LOG_INTERVAL = 0)
2. 增大串口接收缓冲区到 64KB
3. 增大 batch size 从 64→200 减少帧频率

#### 错误 #10：采集卡死（序列号竞争）🟡

**根因**：
```python
_on_go() → _burst_pending=True
    → QTimer.singleShot(50ms, _send_burst)
    
窗口期内：前一次 burst 的残留帧到达 → _brec 达标
→ _burst_pending=False → 真正的数据到达时被忽略
→ 按钮永久灰掉
```

**修复**：增加 `_burst_seq` / `_burst_seq_ack` 序列号，`on_frame` 中检查序列号匹配。

---

## 四、可利用的 ADC / DMA 功能

### 4.1 ADC 硬件功能清单

#### 4.1.1 过采样 (Oversampling)

```c
hadc1.Init.OversamplingMode = ENABLE;
hadc1.Init.Oversampling.Ratio = ADC_OVERSAMPLING_RATIO_256;  // 2x~256x
hadc1.Init.Oversampling.RightBitShift = ADC_RIGHTBITSHIFT_4; // 右移位数
```

- 硬件自动对同一通道多次采样取平均
- 256x 过采样 + 4 位右移 → 等效 16 位分辨率
- **代价**：有效采样率降低 256 倍

#### 4.1.2 模拟看门狗 (Analog Watchdog)

```c
hadc1.Init.AnalogWDG1HighThreshold = 3000;  // 高阈值 RAW
hadc1.Init.AnalogWDG1LowThreshold  = 1000;  // 低阈值 RAW
```

- 检测电压是否超出预设窗口
- 可触发中断 → 用于过压/欠压保护
- 多通道可独立配置阈值

#### 4.1.3 差分模式

```c
sChan.SingleDiff = LL_ADC_DIFFERENTIAL_ENDED;
```

本项目已支持：PA6=INP3 正端, PA7=INN3 负端

- 直接测量两个输入端之间的电压差
- 有效位数 12-bit signed → 范围 -2048 ~ +2047
- 电压 = (RAW − 2048) × VREF+ / 2048

#### 4.1.4 注入通道 (Injected Channels)

```c
HAL_ADCEx_InjectedStart(&hadc1);
```

- 独立于规则通道的高优先级转换
- 可打断规则序列，适合周期性关键测量（如电流检测）

#### 4.1.5 双 ADC 交替模式 (Dual ADC)

H723 有 ADC1 + ADC2（本项目只用 ADC1）：

```c
// ADC1 和 ADC2 交替触发 → 有效采样率翻倍
hadc1.Init.Multimode = ADC_DUALMODE_INTERL;
```

#### 4.1.6 内部通道

H723 ADC1 内置温度传感器 Vbat/VrefInt 通道：

```c
ADC_CHANNEL_TEMPSENSOR   // 内部温度传感器
ADC_CHANNEL_VREFINT       // 内部参考电压 (~1.21V)
ADC_CHANNEL_VBAT          // 电池电压
```

可用于：芯片温度监控、VDDA 波动校准。

#### 4.1.7 定时器触发 (已使用 TIM3)

支持多种触发源：
- TIM1/2/3/4/5/6/7/8/15 TRGO
- EXTI Line 11
- HRTIM (高分辨率定时器)

### 4.2 DMA 功能清单

#### 4.2.1 MDMA (Master DMA) — 本项目未使用但强烈推荐

H723 有 **MDMA** 可以访问 DTCM（本项目 DMA1 无法直接访问 DTCM）：

```c
// DMA1 只能访问 SRAM1-3，不能访问 DTCM
// MDMA 可以访问所有内存域，包括 DTCM (CPU 最快访问区)
```

**当前限制**：DMA buffer `s_rawBuf` 必须放在 DTCM 以外的 SRAM 区域。

#### 4.2.2 DMA 双缓冲 / 循环模式

```c
hdma_adc1.Init.Mode = DMA_CIRCULAR;  // 循环模式
```

配合 DMA Half-Transfer 中断实现 ping-pong 缓冲：
- 前半 buffer 满 → `HAL_ADC_ConvHalfCpltCallback` → 处理上半
- 后半 buffer 满 → `HAL_ADC_ConvCpltCallback` → 处理下半
- 实现无间断连续采集

**本项目未使用 CIRCULAR**，用的是 `DMA_NORMAL` + burst 模式（有起停开销但更简单）。

#### 4.2.3 DMA FIFO / Burst 传输

```c
hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
hdma_adc1.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
hdma_adc1.Init.MemBurst = DMA_MBURST_SINGLE;
hdma_adc1.Init.PeriphBurst = DMA_PBURST_SINGLE;
```

- DMA FIFO 缓冲可以适应源/目标速率不匹配
- burst 模式可一次传输 4/8/16 节拍减少总线占用

#### 4.2.4 BDMA (Basic DMA)

H723 还有 BDMA，适用于低速外设，减轻 DMA1/DMA2 压力。

### 4.3 ADC 中断

除了 `ConvCpltCallback`，还可以用：

| 回调 | 触发条件 | 用途 |
|------|---------|------|
| `HAL_ADC_ConvHalfCpltCallback` | DMA 半传输 | ping-pong 缓冲 |
| `HAL_ADC_LevelOutOfWindowCallback` | 模拟看门狗 | 过压报警 |
| `HAL_ADC_ErrorCallback` | Overrun 等错误 | 错误处理 |

### 4.4 时钟优化技巧

| 预分频 | ADC clk (per_ck=64MHz) | 3ch 扫描时间 | 最大采样率 |
|--------|----------------------|-------------|-----------|
| DIV1 | 64 MHz | 703 ns | 1.42 MHz ✅ |
| DIV2 | 32 MHz | 1.41 μs | 711 KHz |
| DIV4 | 16 MHz | 2.81 μs | 355 KHz |

**注意事项**：
- DIV1 在 HAL 里走 default 分支（HAL 的 `ADC_ConfigureBoostMode` 中 DIV1 不在 switch-case 列表里），不分频，行为正确但需注意
- BOOST 模式 HAL 自动配置（但需确保 HAL 能正确检测 per_ck 频率）

### 4.5 上位机层面可改进的功能

1. **双缓冲连续流模式**：当前是 burst 模式（一次性采完），可实现连续流模式（FIFO 实时显示）
2. **FFT 频谱分析**：对采集数据做 FFT → 频域显示
3. **触发模式**：支持上升沿/下降沿触发，类似示波器
4. **DMA CIRCULAR 模式**：固件用 ping-pong DMA，上位机持续接收

---

## 五、最佳实践与注意事项

### 5.1 H723 特有的坑

1. **ADCSEL 编码与 H743 完全不同** — 查 HAL 宏比查 RM 快
2. **value-line 没有 DIV8/DIV16** — 只有 DIV1/DIV2/DIV4
3. **BOOST 位值含义**：
   - `BOOST_0` (01) → 6.25~12.5 MHz
   - `BOOST_1` (10) → 12.5~25 MHz
   - `BOOST_1|BOOST_0` (11) → >25 MHz
4. **PMCR 寄存器中没有 PA6SO/PA7SO** — GPIO MODER=ANALOG 自动打开模拟开关
5. **H723 通道映射不同** — PA6=CH3 不是 CH6

### 5.2 调试技巧

1. **直接打印硬件寄存器**是最快排查手段：
   ```c
   printf("ADC1 SQR1=0x%08lX CR=0x%08lX ISR=0x%08lX\n",
          ADC1->SQR1, ADC1->CR, ADC1->ISR);
```
2. **LED 硬件诊断**：板载 LED 随 ADC 值翻转，绕过 USB/上位机链路，快速定位问题在固件还是上位机
3. **强制值注入测试**：固件中插入 `0x1234` → 上位机正确显示 → 证明传输链路完整

### 5.3 安全实践

1. **一次只改一个变量** — 前期 ADC 问题的最大教训
2. **用 HAL 宏，不要自己算 Rank 编码** — `ADC_REGULAR_RANK_x` 而非裸整数
3. **burst 前完全复位 ADC + DMA** — `HAL_ADC_Stop_DMA` + `HAL_DMA_Abort`
4. **不要死等 ADCALLIN** — H723 上行为不确定，用固定延时
5. **关闭不必要的磁盘 I/O** — 逐帧日志会阻塞串口线程

---

## 六、最终固件配置速查

```c
// 时钟
ADCSEL = 2 (RCC_ADCCLKSOURCE_CLKP)  // per_ck = HSI 64MHz
DIV1                                 // ADC clk = 64 MHz

// ADC 配置
ADC_RESOLUTION_12B                   // 12 位
ADC_SCAN_ENABLE                      // 扫描模式
ADC_EXTERNALTRIG_T3_TRGO            // TIM3 触发
ADC_CONVERSIONDATA_DMA_ONESHOT      // DMA one-shot
ADC_OVR_DATA_OVERWRITTEN            // Overrun 覆盖
ADC_SAMPLETIME_2CYCLES_5            // 最短采样时间

// DMA
DMA1_Stream1, Periph→Mem, HalfWord
DMA_NORMAL (非循环)

// TIM3
TIM_CLK = 192 MHz
ARR = 1, PSC = 192M/(rate×2)−1
TRGO = Update 事件
```

---

*文档由 Claude 基于 2026-06-22 至 2026-07-17 期间全部日志文件自动生成，覆盖了 ADC 基础知识、全链路数据流向、全部已知错误及其根因、以及可进一步利用的硬件功能。*
