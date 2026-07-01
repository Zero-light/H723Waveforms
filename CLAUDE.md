# CLAUDE.md — H723Waveforms 便携开发包

## 项目概述

STM32H723ZGT6 波形发生器 + ADC 采集，带 PC 上位机。
USB CDC 通信，支持波形生成、ADC 采集、SPI/DAC 控制。

## 芯片信息

- 主控：STM32H723ZGT6
- 开发工具：STM32CubeIDE（.cproject + .ioc）
- 通信：USB CDC 虚拟串口

## 项目结构

```
H723Waveforms_开发包/
├── firmware/
│   └── H723ZGT6/
│       ├── build/          ← 编译好的 .elf 固件
│       └── project/        ← 完整 CubeMX 工程
│           ├── Core/       ← 应用层源码
│           ├── Drivers/    ← HAL 库
│           ├── Middlewares/← USB 设备栈
│           └── USB_DEVICE/ ← CDC 接口
├── host/
│   └── H723Waveforms/      ← PC 上位机（开箱即用）
│       ├── H723Waveforms.exe
│       └── _internal/      ← Python 运行时（gitignore）
├── boards.json
├── README.md
└── VERSION.md
```

## 应用层模块（可移植）

当移植到其他板子时，复制以下文件：
- `Core/Inc/app_protocol.h`
- `Core/Src/app_protocol.c`
- `Core/Inc/app_wavegen.h`
- `Core/Src/app_wavegen.c`
- `Core/Inc/app_adc.h`
- `Core/Src/app_adc.c`
- `Core/Inc/app_spi.h`
- `Core/Src/app_spi.c`
- `Core/Inc/app_dac.h`
- `Core/Src/app_dac.c`

## Git 仓库

- GitHub: `Zero-light/H723Waveforms`
- 默认分支: main
