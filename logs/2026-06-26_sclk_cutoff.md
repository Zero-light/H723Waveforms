# SCLK 截止边沿功能 — 2026-06-26

## 改动文件

`host/widgets/wave_panel.py`

## 需求

SCLK 在一个周期内可在指定 CLK 编号后停止，剩余采样点保持低电平。
例：80 CLK 周期，第 50 个 CLK 后全部低电平。

## 修改

### 1. 新增 "CLK截止" QSpinBox（第136-143行）

- 控件：`sb_sclk_cutoff`，范围 0-4096，默认 0（=全覆盖）
- Tooltip: `0=全覆盖, N=在第N个CLK后停止(全低)`
- 位置：波形配置区，"有效边沿"下方

### 2. `_fill_sclk()` 使用截止参数（第397-422行）

- 读取 `sb_sclk_cutoff.value()`
- `cutoff_sample = cutoff_edge * 2`（每个 CLK = 2 采样点）
- 采样索引 >= cutoff_sample 的单元格填 "0"
- cutoff_edge == 0 时行为不变（全覆盖）

### 3. `_on_sclk_config_changed()` 移除自动填充（第424-425行）

- 原来：修改 SCLK 初始/边沿自动触发 `_fill_sclk()` → 覆盖手动编辑
- 现在：空函数，只有手动点击 "填充 SCLK" 按钮才刷新

## 根因

`_fill_sclk()` 总是覆盖整个 SCLK 列全部采样点（0 到 n-1），且
`_on_sclk_config_changed` 在 SCLK 初始/边沿改变时自动调用 `_fill_sclk()`，
导致用户手动编辑的表格数据被覆盖。

## 用法

1. 设置点数 = 160（80 CLK）
2. 设置 CLK截止 = 50
3. 点击 "填充 SCLK"
4. 采样 0-99：方波（50 CLK），采样 100-159：全低
5. 可手动编辑任意单元格微调，不会被自动覆盖

## 构建

```bash
cd host && python -m PyInstaller H723Waveforms.spec
```

输出：`host/dist/H723Waveforms/H723Waveforms.exe`
