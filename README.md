# H723Waveforms 便携开发包

## 目录结构

```
├── host/
│   └── H723Waveforms/              ← PC 上位机（开箱即用）
│       ├── H723Waveforms.exe
│       └── _internal/
├── firmware/
│   └── H723ZGT6/
│       ├── build/
│       │   └── H723ZGT6_V1.02.elf   ← 编译好的固件
│       └── project/                 ← 完整 CubeMX 工程（可编译）
│           ├── Core/                  # main.c, it.c, msp.c 等
│           ├── Drivers/               # HAL 库
│           ├── Middlewares/           # USB 设备栈
│           ├── USB_DEVICE/            # CDC 接口
│           └── H723ZGT6.ioc           # CubeMX 配置
├── boards.json                      # 板子清单
├── VERSION.md                       # 版本历史
└── README.md
```

## 使用方式

### 1. 运行上位机
```
host/H723Waveforms/H723Waveforms.exe
```
连接 USB CDC 虚拟串口即可使用。

### 2. 修改固件
用 STM32CubeIDE 打开 `firmware/H723ZGT6/project/`，直接修改源码后编译烧录。

### 3. 仅移植应用层源码到其他板子
复制以下文件到新工程的对应位置即可：
```
Core/Inc/app_protocol.h
Core/Inc/app_wavegen.h
Core/Inc/app_adc.h
Core/Inc/app_spi.h
Core/Inc/app_dac.h
Core/Src/app_protocol.c
Core/Src/app_wavegen.c
Core/Src/app_adc.c
Core/Src/app_spi.c
Core/Src/app_dac.c
```

## 后续兼容更多板子

1. 在 `firmware/` 下新建目录，如 `firmware/H743ZGT6/`
2. 放入该板子的 `build/` 和 `project/`
3. 在 `boards.json` 中追加条目
