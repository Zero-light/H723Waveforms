# Board-specific configuration for h723zg_v1
BOARD_ID        := h723zg_v1
BOARD_NAME      := STM32H723ZG Custom V1
MCU             := STM32H723ZGTx
MCU_FAMILY      := STM32H7xx
MCU_SERIES      := STM32H723xx

# Toolchain
PREFIX          := arm-none-eabi-
CC              := $(PREFIX)gcc
CXX             := $(PREFIX)g++
AS              := $(PREFIX)gcc -x assembler-with-cpp
CP              := $(PREFIX)objcopy
SZ              := $(PREFIX)size
AR              := $(PREFIX)ar
LD              := $(PREFIX)gcc
HEX             := $(CP) -O ihex
BIN             := $(CP) -O binary -S

# Board directories
BOARD_DIR       := $(ROOT)/firmware/boards/$(BOARD_ID)
BOARD_BSP_DIR   := $(BOARD_DIR)/bsp
BOARD_HAL_DIR   := $(BOARD_DIR)/hal
BOARD_CFG_DIR   := $(BOARD_DIR)/config

# Common directories
COMMON_DIR      := $(ROOT)/firmware/common
COMMON_BSP_INC  := $(COMMON_DIR)/bsp/inc
COMMON_DRV_INC  := $(COMMON_DIR)/drivers/inc
COMMON_PROTO_INC:= $(COMMON_DIR)/protocol/inc
COMMON_APP_INC  := $(COMMON_DIR)/app/inc
COMMON_UTIL_INC := $(COMMON_DIR)/utils/inc

# HAL / CMSIS / Middlewares (shared ST libraries)
# In a fully refactored tree these would live under firmware/common/hal.
# For migration, we point to the existing CubeMX-generated copies.
HAL_DIR         := $(ROOT)/cubeMX/H723ZGT6/Drivers/STM32H7xx_HAL_Driver
CMSIS_DIR       := $(ROOT)/cubeMX/H723ZGT6/Drivers/CMSIS
USB_DEV_DIR     := $(ROOT)/cubeMX/H723ZGT6/Middlewares/ST/STM32_USB_Device_Library
USB_APP_DIR     := $(ROOT)/cubeMX/H723ZGT6/USB_DEVICE

# Startup and linker
STARTUP         := $(BOARD_HAL_DIR)/startup_stm32h723zgtx.s
LDSCRIPT        := $(BOARD_HAL_DIR)/STM32H723ZGTX_FLASH.ld

# Defines
DEFS            := -DUSE_HAL_DRIVER -D$(MCU_SERIES) -DUSE_PWR_LDO_SUPPLY -DBOARD_$(BOARD_ID)

# Include paths
INCS            := \
    -I$(BOARD_CFG_DIR) \
    -I$(BOARD_BSP_DIR)/inc \
    -I$(BOARD_BSP_DIR)/src \
    -I$(COMMON_BSP_INC) \
    -I$(COMMON_DRV_INC) \
    -I$(COMMON_PROTO_INC) \
    -I$(COMMON_APP_INC) \
    -I$(COMMON_UTIL_INC) \
    -I$(HAL_DIR)/Inc \
    -I$(HAL_DIR)/Inc/Legacy \
    -I$(CMSIS_DIR)/Device/ST/STM32H7xx/Include \
    -I$(CMSIS_DIR)/Include \
    -I$(USB_DEV_DIR)/Core/Inc \
    -I$(USB_DEV_DIR)/Class/CDC/Inc \
    -I$(USB_APP_DIR)/App \
    -I$(USB_APP_DIR)/Target \
    -I$(BOARD_BSP_DIR)/inc

# MCU flags
CPU             := -mcpu=cortex-m7
FPU             := -mfpu=fpv5-d16
FLOAT-ABI       := -mfloat-abi=hard
MCU_FLAGS       := $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

# Compiler flags
CFLAGS          := $(MCU_FLAGS) $(DEFS) $(INCS) -O2 -Wall -fdata-sections -ffunction-sections -g
CXXFLAGS        := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS         := $(MCU_FLAGS) $(DEFS) $(INCS) -Wall -fdata-sections -ffunction-sections
LDFLAGS         := $(MCU_FLAGS) -specs=nano.specs -T$(LDSCRIPT) \
                   -Wl,-Map=$(BUILD_DIR)/firmware.map,--cref -Wl,--gc-sections

# Output
PROFILE         ?= debug
BUILD_DIR       := $(ROOT)/firmware/output/$(BOARD_ID)/$(PROFILE)
TARGET          := $(BUILD_DIR)/firmware
