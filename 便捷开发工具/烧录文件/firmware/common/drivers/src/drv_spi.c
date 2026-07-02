/*
 * drv_spi.c
 * SPI driver implementation using BSP_SPI.
 */
#include "drv_spi.h"
#include "bsp_spi.h"
#include <stddef.h>

static BspSpiHandle_t s_spiHandle = NULL;

void DRV_SPI_Init(void)
{
    /* GPIO and peripheral clocks are enabled in BSP_SPI_Init on first configure. */
    s_spiHandle = NULL;
}

bool DRV_SPI_Configure(const DrvSpiConfig_t *cfg)
{
    if (cfg == NULL) return false;

    BspSpiConfig_t bspCfg = {
        .cpol = cfg->cpol ? true : false,
        .cpha = cfg->cpha ? true : false,
        .prescaler = cfg->prescaler,
        .data_size = cfg->data_size,
    };
    return BSP_SPI_Init(&s_spiHandle, &bspCfg);
}

bool DRV_SPI_WriteReg(uint8_t addr, uint16_t data, uint8_t data_width)
{
    if (s_spiHandle == NULL) return false;

    uint8_t txBuf[3];
    txBuf[0] = addr;
    uint8_t txLen = 2;

    if (data_width == 16) {
        txBuf[1] = (uint8_t)((data >> 8) & 0xFF);
        txBuf[2] = (uint8_t)(data & 0xFF);
        txLen = 3;
    } else {
        txBuf[1] = (uint8_t)(data & 0xFF);
    }

    BSP_SPI_CsLow();
    bool ok = BSP_SPI_Transmit(s_spiHandle, txBuf, txLen, 100);
    BSP_SPI_CsHigh();
    return ok;
}

bool DRV_SPI_WriteRegs(const uint8_t *payload, uint8_t num_regs, uint8_t data_width)
{
    if (s_spiHandle == NULL || payload == NULL || num_regs == 0) return false;

    uint16_t offset = 0;
    bool all_ok = true;

    for (uint8_t i = 0; i < num_regs; i++) {
        uint8_t addr = payload[offset++];
        uint16_t data = 0;

        if (data_width == 16) {
            data = payload[offset] | ((uint16_t)payload[offset + 1] << 8);
            offset += 2;
        } else {
            data = payload[offset++];
        }

        if (!DRV_SPI_WriteReg(addr, data, data_width)) {
            all_ok = false;
            break;
        }
    }

    return all_ok;
}
