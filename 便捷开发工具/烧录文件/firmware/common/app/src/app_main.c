/*
 * app_main.c
 * Application orchestration: protocol dispatch, main loop, heartbeat.
 */
#include "app_main.h"
#include "bsp.h"
#include "drv_adc.h"
#include "drv_dac.h"
#include "drv_spi.h"
#include "drv_wavegen.h"
#include "protocol.h"
#include "board_config.h"
#include <string.h>

static void OnFrameReceived(const ProtoFrame_t *frame);

void APP_Run(void)
{
    BSP_Init();

    DRV_WaveGen_Init();
    DRV_SPI_Init();
    DRV_ADC_Init();
    DRV_DAC_Init();

    PROTO_Init(OnFrameReceived);
    BSP_USB_Init();

    /* Default 1 kHz square wave on PA1 (SCLK), falling edge active. */
    DRV_WaveGen_Configure(&(DrvWaveConfig_t){ .sample_rate_hz = 2000, .num_points = 2, .ch_mask = 0x02 });
    uint32_t defaultWave[2] = {
        (1u << BSP_WAVE_CH1_BIT),
        (1u << (BSP_WAVE_CH1_BIT + 16)),
    };
    DRV_WaveGen_LoadData(defaultWave, 2);
    DRV_WaveGen_Start();

    uint32_t tickLast = BSP_GetTick();
    uint32_t tickTest = BSP_GetTick();

    while (1) {
        DRV_ADC_Poll();

        if (BSP_GetTick() - tickTest >= 1000) {
            static ProtoFrame_t testFrame;
            testFrame.cmd = 0xFF;
            testFrame.len = 4;
            memcpy(testFrame.payload, "TEST", 4);
            PROTO_SendFrame(&testFrame);
            tickTest = BSP_GetTick();
        }

        if (BSP_GetTick() - tickLast >= 500) {
            BSP_GPIO_LedToggle();
            tickLast = BSP_GetTick();
        }
    }
}

static void OnFrameReceived(const ProtoFrame_t *frame)
{
    if (frame == NULL) return;

    switch (frame->cmd) {
        case CMD_WAVE_CONFIG: {
            if (frame->len < 7) {
                PROTO_SendAck(CMD_WAVE_CONFIG, false);
                break;
            }
            DrvWaveConfig_t cfg = {0};
            cfg.sample_rate_hz = ((uint32_t)frame->payload[0])
                               | ((uint32_t)frame->payload[1] << 8)
                               | ((uint32_t)frame->payload[2] << 16)
                               | ((uint32_t)frame->payload[3] << 24);
            cfg.num_points = ((uint16_t)frame->payload[4])
                           | ((uint16_t)frame->payload[5] << 8);
            cfg.ch_mask = frame->payload[6];
            PROTO_SendAck(CMD_WAVE_CONFIG, DRV_WaveGen_Configure(&cfg));
            break;
        }

        case CMD_WAVE_DATA: {
            if (frame->len == 0 || (frame->len % 4) != 0) {
                PROTO_SendAck(CMD_WAVE_DATA, false);
                break;
            }
            uint16_t num_words = frame->len / 4;
            if (num_words > DRV_WAVE_MAX_POINTS) num_words = DRV_WAVE_MAX_POINTS;
            uint32_t data[DRV_WAVE_MAX_POINTS];
            for (uint16_t i = 0; i < num_words; i++) {
                data[i] = ((uint32_t)frame->payload[i*4])
                        | ((uint32_t)frame->payload[i*4 + 1] << 8)
                        | ((uint32_t)frame->payload[i*4 + 2] << 16)
                        | ((uint32_t)frame->payload[i*4 + 3] << 24);
            }
            PROTO_SendAck(CMD_WAVE_DATA, DRV_WaveGen_LoadData(data, num_words));
            break;
        }

        case CMD_WAVE_CTRL: {
            if (frame->len < 1) {
                PROTO_SendAck(CMD_WAVE_CTRL, false);
                break;
            }
            bool ok;
            if (frame->payload[0]) {
                ok = DRV_WaveGen_Start();
            } else {
                DRV_WaveGen_Stop();
                ok = true;
            }
            PROTO_SendAck(CMD_WAVE_CTRL, ok);
            break;
        }

        case CMD_SPI_CONFIG: {
            if (frame->len < 3) {
                PROTO_SendAck(CMD_SPI_CONFIG, false);
                break;
            }
            DrvSpiConfig_t cfg = {0};
            cfg.cpol = (frame->payload[0] >> 1) & 1;
            cfg.cpha = frame->payload[0] & 1;
            cfg.prescaler = frame->payload[1];
            cfg.data_size = frame->payload[2];
            PROTO_SendAck(CMD_SPI_CONFIG, DRV_SPI_Configure(&cfg));
            break;
        }

        case CMD_SPI_XFER: {
            if (frame->len < 2) {
                PROTO_SendAck(CMD_SPI_XFER, false);
                break;
            }
            PROTO_SendAck(CMD_SPI_XFER,
                DRV_SPI_WriteRegs(&frame->payload[2], frame->payload[0], frame->payload[1]));
            break;
        }

        case CMD_ADC_CONFIG: {
            if (frame->len < 6) {
                PROTO_SendAck(CMD_ADC_CONFIG, false);
                break;
            }
            DrvAdcConfig_t cfg = {0};
            /* bit0 = PA6 (always enabled), bit1 = PA7 (optional).
             * Ignore all other bits. */
            uint8_t user_mask = frame->payload[0];
            cfg.ch_mask = DRV_ADC_MASK_PA6;
            if (user_mask & DRV_ADC_MASK_PA7) {
                cfg.ch_mask |= DRV_ADC_MASK_PA7;
            }
            cfg.sample_rate_hz = ((uint32_t)frame->payload[1])
                               | ((uint32_t)frame->payload[2] << 8)
                               | ((uint32_t)frame->payload[3] << 16)
                               | ((uint32_t)frame->payload[4] << 24);
            cfg.mode = frame->payload[5];
            PROTO_SendAck(CMD_ADC_CONFIG, DRV_ADC_Configure(&cfg));
            break;
        }

        case CMD_ADC_CTRL: {
            if (frame->len < 1) {
                PROTO_SendAck(CMD_ADC_CTRL, false);
                break;
            }
            bool ok;
            if (frame->payload[0]) {
                ok = DRV_ADC_Start();
            } else {
                DRV_ADC_Stop();
                ok = true;
            }
            PROTO_SendAck(CMD_ADC_CTRL, ok);
            break;
        }

        case CMD_DAC_SET: {
            if (frame->len < 2) {
                PROTO_SendAck(CMD_DAC_SET, false);
                break;
            }
            uint16_t value = ((uint16_t)frame->payload[0])
                           | ((uint16_t)frame->payload[1] << 8);
            PROTO_SendAck(CMD_DAC_SET, DRV_DAC_SetValue(value));
            break;
        }

        default:
            PROTO_SendAck(frame->cmd, false);
            break;
    }
}
