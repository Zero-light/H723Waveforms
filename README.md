> **Note**: The firmware source has been consolidated into the CubeMX IDE project. The old `firmware/` directory has been removed. Use `cubeMX/H723ZGT6/` (or the copy in `便捷开发工具/烧录文件/H723ZGT6/`) as the authoritative STM32CubeIDE project.

## Project structure

```text
STM32H723ZGT6/
├── docs/                   # Architecture and onboarding docs
├── cubeMX/                 # STM32CubeIDE project (authoritative firmware)
│   └── H723ZGT6/
│       ├── Core/Src        # Application, BSP, drivers
│       ├── Core/Inc        # Headers
│       └── *.ioc           # CubeMX configuration
├── 便捷开发工具/             # Release package
│   ├── V1.0.1/src/         # Host application source for the released .exe
│   └── 烧录文件/H723ZGT6/    # Shipped CubeMX project copy
├── host/                   # Latest Python host application source
├── tools/                  # Build scripts and flash scripts (legacy Makefile)
└── logs/                   # Runtime logs
```

## Quick start

### Build and flash firmware

Open `cubeMX/H723ZGT6/` in **STM32CubeIDE**, build the project, and flash via ST-Link.

The copy under `便捷开发工具/烧录文件/H723ZGT6/` is the release build project; keep it in sync with `cubeMX/H723ZGT6/` before shipping a new binary.

### Run host application

```bash
cd host
python main.py
```

To rebuild the standalone `.exe`:

```bash
cd 便捷开发工具/V1.0.1/src
pyinstaller H723Waveforms.spec
```

## Safety notes

- The MCU no longer auto-starts a waveform on power-up. The user must explicitly send **配置 → 数据 → 开始** from the host.
- The firmware rejects all-zero BSRR arrays and waveforms where an enabled channel never goes HIGH.
- Output pins are pulled HIGH before switching to output mode, and forced HIGH before returning to input mode on stop.
- The host warns before applying/importing/sending long-low or all-low patterns.
- A hardware independent watchdog (IWDG) resets the MCU if the main loop hangs; after reset all GPIO default to floating input.

## Architecture

See `docs/ARCHITECTURE.md` for design documentation. Note: some docs may still reference the removed `firmware/` layout.
