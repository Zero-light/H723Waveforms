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

/* Software watchdog: auto-stop waveform after WAVE_TIMEOUT_MS if the host
 * never sends a stop command.  Prevents a forgotten running waveform from
 * heating the board or external load indefinitely. */
#define WAVE_TIMEOUT_MS  60000u
static volatile uint32_t s_tickWaveStart = 0;

/* Deferred ACK: PROTO_SendAck may fail inside USB ISR when TxState is busy */
static volatile uint8_t  s_pendingAckCmd;
static volatile bool     s_pendingAckOk;
static volatile bool     s_pendingAckFlag;

void APP_Run(void)
{
    BSP_Init();

    DRV_WaveGen_Init();
    DRV_SPI_Init();
    DRV_ADC_Init();
    DRV_DAC_Init();

    PROTO_Init(OnFrameReceived);
    BSP_USB_Init();

    /* Safety: do NOT auto-start any waveform on power-up.  Waveform output
     * pins were already initialized as floating inputs in BSP_Init, so the
     * board starts in a safe high-impedance state.  The user must explicitly
     * send CMD_WAVE_CONFIG + CMD_WAVE_DATA + CMD_WAVE_CTRL(START). */

    uint32_t tickLast = BSP_GetTick();
    uint32_t tickTest = BSP_GetTick();

    while (1) {
        BSP_IWDG_Refresh();

        /* Retry any pending ACK until USB TX is free.  All command handlers now
         * set this flag instead of calling PROTO_SendAck directly. */
        if (s_pendingAckFlag) {
            if (PROTO_SendAck(s_pendingAckCmd, s_pendingAckOk)) {
                s_pendingAckFlag = false;
            }
        }

        /* Drain received frames from the ISR-fed queue.  Only process a new
         * frame when the previous ACK has been flushed, keeping the single-slot
         * deferred ACK safe and preventing lost acknowledgements. */
        ProtoFrame_t rxFrame;
        if (!s_pendingAckFlag && PROTO_GetFrame(&rxFrame)) {
            OnFrameReceived(&rxFrame);
        }

        DRV_ADC_Poll();

        /* Software timeout: auto-stop waveform after WAVE_TIMEOUT_MS */
        if (DRV_WaveGen_IsRunning() && s_tickWaveStart > 0) {
            if (BSP_GetTick() - s_tickWaveStart >= WAVE_TIMEOUT_MS) {
                DRV_WaveGen_Stop();
                s_tickWaveStart = 0;
            }
        }

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
                s_pendingAckCmd = CMD_WAVE_CONFIG;
                s_pendingAckOk  = false;
                s_pendingAckFlag = true;
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
            /* Safety: reject zero mask or reserved bits.  A zero mask would
             * leave no channel enabled but still start the timer/DMA, while
             * reserved bits map to non-existent pins. */
            if (cfg.ch_mask == 0 || (cfg.ch_mask & ~0x1Fu) != 0) {
                s_pendingAckCmd = CMD_WAVE_CONFIG;
                s_pendingAckOk  = false;
                s_pendingAckFlag = true;
                break;
            }
            bool ok = DRV_WaveGen_Configure(&cfg);
            s_pendingAckCmd = CMD_WAVE_CONFIG;
            s_pendingAckOk  = ok;
            s_pendingAckFlag = true;
            break;
        }

        case CMD_WAVE_DATA: {
            if (frame->len == 0 || (frame->len % 4) != 0) {
                s_pendingAckCmd = CMD_WAVE_DATA;
                s_pendingAckOk  = false;
                s_pendingAckFlag = true;
                break;
            }
            uint16_t num_words = frame->len / 4;
            /* Safety: reject oversized payloads instead of silently truncating.
             * A truncated waveform could change the intended timing and leave
             * channels at unsafe levels. */
            if (num_words > DRV_WAVE_MAX_POINTS) {
                s_pendingAckCmd = CMD_WAVE_DATA;
                s_pendingAckOk  = false;
                s_pendingAckFlag = true;
                break;
            }
            /* ponytail: static buffer avoids 32 KB stack array that
             * overflows the Cortex-M7 stack (default <4 KB on H723). */
            static uint32_t s_parseBuf[DRV_WAVE_MAX_POINTS];
            for (uint16_t i = 0; i < num_words && i < DRV_WAVE_MAX_POINTS; i++) {
                s_parseBuf[i] = ((uint32_t)frame->payload[i*4])
                              | ((uint32_t)frame->payload[i*4 + 1] << 8)
                              | ((uint32_t)frame->payload[i*4 + 2] << 16)
                              | ((uint32_t)frame->payload[i*4 + 3] << 24);
            }
            bool ok = DRV_WaveGen_LoadData(s_parseBuf, num_words);
            s_pendingAckCmd = CMD_WAVE_DATA;
            s_pendingAckOk  = ok;
            s_pendingAckFlag = true;
            break;
        }

        case CMD_WAVE_CTRL: {
            if (frame->len < 1) {
                s_pendingAckCmd = CMD_WAVE_CTRL;
                s_pendingAckOk  = false;
                s_pendingAckFlag = true;
                break;
            }
            bool ok;
            if (frame->payload[0]) {
                ok = DRV_WaveGen_Start();
                if (ok) s_tickWaveStart = BSP_GetTick();
            } else {
                DRV_WaveGen_Stop();
                s_tickWaveStart = 0;
                ok = true;
            }
            s_pendingAckCmd = CMD_WAVE_CTRL;
            s_pendingAckOk  = ok;
            s_pendingAckFlag = true;
            break;
        }

        case CMD_SPI_CONFIG: {
            if (frame->len < 3) {
                s_pendingAckCmd = CMD_SPI_CONFIG;
                s_pendingAckOk  = false;
                s_pendingAckFlag = true;
                break;
            }
            DrvSpiConfig_t cfg = {0};
            cfg.cpol = (frame->payload[0] >> 1) & 1;
            cfg.cpha = frame->payload[0] & 1;
            cfg.prescaler = frame->payload[1];
            cfg.data_size = frame->payload[2];
            bool ok = DRV_SPI_Configure(&cfg);
            s_pendingAckCmd = CMD_SPI_CONFIG;
            s_pendingAckOk  = ok;
            s_pendingAckFlag = true;
            break;
        }

        case CMD_SPI_XFER: {
            if (frame->len < 2) {
                s_pendingAckCmd = CMD_SPI_XFER;
                s_pendingAckOk  = false;
                s_pendingAckFlag = true;
                break;
            }
            bool ok = DRV_SPI_WriteRegs(&frame->payload[2], frame->payload[0], frame->payload[1]);
            s_pendingAckCmd = CMD_SPI_XFER;
            s_pendingAckOk  = ok;
            s_pendingAckFlag = true;
            break;
        }

        case CMD_ADC_CONFIG: {
            if (frame->len < 6) {
                s_pendingAckCmd = CMD_ADC_CONFIG;
                s_pendingAckOk  = false;
                s_pendingAckFlag = true;
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
            bool ok = DRV_ADC_Configure(&cfg);
            s_pendingAckCmd = CMD_ADC_CONFIG;
            s_pendingAckOk  = ok;
            s_pendingAckFlag = true;
            break;
        }

        case CMD_ADC_CTRL: {
            if (frame->len < 1) {
                s_pendingAckCmd = CMD_ADC_CTRL;
                s_pendingAckOk  = false;
                s_pendingAckFlag = true;
                break;
            }
            bool ok;
            if (frame->payload[0]) {
                ok = DRV_ADC_Start();
            } else {
                DRV_ADC_Stop();
                ok = true;
            }
            s_pendingAckCmd = CMD_ADC_CTRL;
            s_pendingAckOk  = ok;
            s_pendingAckFlag = true;
            break;
        }

        case CMD_DAC_SET: {
            if (frame->len < 2) {
                s_pendingAckCmd = CMD_DAC_SET;
                s_pendingAckOk  = false;
                s_pendingAckFlag = true;
                break;
            }
            uint16_t value = ((uint16_t)frame->payload[0])
                           | ((uint16_t)frame->payload[1] << 8);
            bool ok = DRV_DAC_SetValue(value);
            s_pendingAckCmd = CMD_DAC_SET;
            s_pendingAckOk  = ok;
            s_pendingAckFlag = true;
            break;
        }

        default:
            s_pendingAckCmd = frame->cmd;
            s_pendingAckOk  = false;
            s_pendingAckFlag = true;
            break;
    }
}
