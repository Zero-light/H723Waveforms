# Multi-board firmware build system
# Usage:
#   make BOARD=h723zg_v1              # build debug
#   make BOARD=h723zg_v1 PROFILE=release
#   make BOARD=h723zg_v1 clean
#   make BOARD=h723zg_v1 flash

ROOT := $(realpath ..)
BOARD ?= h723zg_v1
PROFILE ?= debug

include $(ROOT)/tools/config/$(BOARD).mk

# Source files
COMMON_SRC := \
    $(wildcard $(COMMON_DIR)/utils/src/*.c) \
    $(wildcard $(COMMON_DIR)/protocol/src/*.c) \
    $(wildcard $(COMMON_DIR)/drivers/src/*.c) \
    $(wildcard $(COMMON_DIR)/app/src/*.c)

BOARD_SRC := \
    $(BOARD_BSP_DIR)/src/main.c \
    $(BOARD_BSP_DIR)/src/bsp_init.c \
    $(BOARD_BSP_DIR)/src/bsp_clock.c \
    $(BOARD_BSP_DIR)/src/bsp_gpio.c \
    $(BOARD_BSP_DIR)/src/bsp_error.c \
    $(BOARD_BSP_DIR)/src/bsp_adc.c \
    $(BOARD_BSP_DIR)/src/bsp_dac.c \
    $(BOARD_BSP_DIR)/src/bsp_spi.c \
    $(BOARD_BSP_DIR)/src/bsp_tim.c \
    $(BOARD_BSP_DIR)/src/bsp_dma.c \
    $(BOARD_BSP_DIR)/src/bsp_usb.c \
    $(BOARD_HAL_DIR)/system_stm32h7xx.c \
    $(BOARD_HAL_DIR)/syscalls.c \
    $(BOARD_HAL_DIR)/sysmem.c

# HAL sources
HAL_SRC := \
    $(HAL_DIR)/Src/stm32h7xx_hal.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_cortex.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_rcc.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_rcc_ex.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_gpio.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_adc.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_adc_ex.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_dac.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_dac_ex.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_spi.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_tim.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_tim_ex.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_dma.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_dma_ex.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_pcd.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_pcd_ex.c \
    $(HAL_DIR)/Src/stm32h7xx_ll_usb.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_pwr.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_pwr_ex.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_flash.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_flash_ex.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_exti.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_i2c.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_i2c_ex.c \
    $(HAL_DIR)/Src/stm32h7xx_hal_hsem.c

# USB device stack
USB_SRC := \
    $(USB_DEV_DIR)/Core/Src/usbd_core.c \
    $(USB_DEV_DIR)/Core/Src/usbd_ctlreq.c \
    $(USB_DEV_DIR)/Core/Src/usbd_ioreq.c \
    $(USB_DEV_DIR)/Class/CDC/Src/usbd_cdc.c \
    $(USB_APP_DIR)/App/usb_device.c \
    $(USB_APP_DIR)/App/usbd_desc.c \
    $(BOARD_BSP_DIR)/src/usbd_cdc_if.c \
    $(USB_APP_DIR)/Target/usbd_conf.c

# Interrupt handler (board-specific)
IT_SRC := $(BOARD_BSP_DIR)/src/stm32h7xx_it.c

# Object files
OBJECTS := \
    $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(COMMON_SRC))) \
    $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(BOARD_SRC))) \
    $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(HAL_SRC))) \
    $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(USB_SRC))) \
    $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(IT_SRC))) \
    $(BUILD_DIR)/startup_stm32h723zgtx.o

# VPATH for source search
VPATH := \
    $(COMMON_DIR)/utils/src \
    $(COMMON_DIR)/protocol/src \
    $(COMMON_DIR)/drivers/src \
    $(COMMON_DIR)/app/src \
    $(BOARD_BSP_DIR)/src \
    $(BOARD_HAL_DIR) \
    $(HAL_DIR)/Src \
    $(USB_DEV_DIR)/Core/Src \
    $(USB_DEV_DIR)/Class/CDC/Src \
    $(USB_APP_DIR)/App \
    $(USB_APP_DIR)/Target \
    $(BOARD_HAL_DIR)

.PHONY: all clean dirs flash info

all: dirs $(TARGET).elf $(TARGET).hex $(TARGET).bin
	$(SZ) $(TARGET).elf
	@echo "[BUILD] $(BOARD)/$(PROFILE) complete: $(TARGET).hex"

dirs:
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: %.c
	@echo "[CC]  $<"
	@$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/startup_stm32h723zgtx.o: $(STARTUP)
	@echo "[AS]  $<"
	@$(AS) $(ASFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJECTS)
	@echo "[LD]  $@"
	@$(LD) $(OBJECTS) $(LDFLAGS) -o $@

$(TARGET).hex: $(TARGET).elf
	@echo "[HEX] $@"
	@$(HEX) $< $@

$(TARGET).bin: $(TARGET).elf
	@echo "[BIN] $@"
	@$(BIN) $< $@

clean:
	@rm -rf $(BUILD_DIR)
	@echo "[CLEAN] $(BOARD)/$(PROFILE)"

flash: $(TARGET).hex
	python $(ROOT)/tools/flash.py --board $(BOARD) --file $(TARGET).hex

info:
	@echo "BOARD      = $(BOARD)"
	@echo "PROFILE    = $(PROFILE)"
	@echo "BUILD_DIR  = $(BUILD_DIR)"
	@echo "TARGET     = $(TARGET)"

# Include auto-generated dependencies
-include $(OBJECTS:.o=.d)
