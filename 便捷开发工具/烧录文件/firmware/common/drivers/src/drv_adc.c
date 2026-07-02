/*
 * drv_adc.c
 * ADC driver: double-buffered polled acquisition, decoupled from USB TX.
 *
 * ponytail: the previous design stopped ADC sampling whenever the 64-sample
 * batch buffer was full and waiting for USB TX.  When the host serial-reader
 * thread was GIL-blocked by PyQtGraph rendering (~80 ms timer), USB stayed
 * busy, the firmware spun retrying, and ADC samples were lost — creating
 * periodic gaps that rendered as triangular peaks on the display.
 *
 * Double-buffering decouples the two: while one buffer is being sent (or
 * waiting for USB), the other buffer keeps collecting samples.  Sampling
 * never stops; USB latency adds at most one batch of end-to-end delay.
 */
#include "drv_adc.h"
#include "bsp_adc.h"
#include "bsp.h"
#include "protocol.h"
#include "board_config.h"
#include <string.h>
#include <stdio.h>

/* Debug: print GPIOA / ADC registers once at first poll */
#define DRV_ADC_DEBUG_GPIO      1

/* Single-conversion mode (DEFAULT = 1).
 * Continuous scan + EOC_SINGLE_CONV cannot guarantee PA6-before-PA7 read
 * order because the rate-limit period (100 µs) is not an integer multiple of
 * the per-channel conversion time (~17 µs).  Single-conversion restarts the
 * sequence each poll cycle, so rank-1 (PA6) ALWAYS finishes first. */
#define DRV_ADC_USE_SINGLE_CONV 1

/* Number of ping-pong buffers (must be ≥ 2 for decoupling) */
#define DRV_ADC_NUM_BUFS        2

/* Diagnostic heartbeat: print sample-rate info every N samples (0=off) */
#define DRV_ADC_DIAG_INTERVAL   1000

static uint16_t s_sampleBuf[DRV_ADC_NUM_BUFS][DRV_ADC_BATCH_SIZE];
static uint8_t  s_activeBuf;      /* 0 or 1: which buffer ADC fills */
static uint8_t  s_bufIdx;         /* write position inside active buffer */
static uint8_t  s_txPending;      /* bitmask: bit b = buffer b has unsent data */
static uint16_t s_pendingLen[DRV_ADC_NUM_BUFS];
static uint8_t  s_pendingChMask[DRV_ADC_NUM_BUFS];
static uint8_t  s_pendingMode[DRV_ADC_NUM_BUFS];

static DrvAdcConfig_t s_config = {0};
static volatile bool s_running = false;
static uint16_t s_seqId = 0;
static uint8_t  s_numEnabled = 0;
static BspAdcHandle_t s_adcHandle = NULL;
static uint32_t s_totalSamples = 0;    /* total ADC samples collected since Start */
static uint32_t s_diagNext = 0;        /* next sample count at which to print diag */

/* DWT-based rate limiter state — reset on each DRV_ADC_Start(). */
static uint32_t s_lastSampleCycles = 0;
static bool     s_firstSample = true;

/* Map logical channel index to board-specific ADC channel constant. */
static const BspAdcChannel_t s_channelMap[DRV_ADC_MAX_CHANNELS] = {
    BSP_ADC_CH0_CHANNEL,    /* PA6 -> ADC1_IN3 */
    BSP_ADC_CH1_CHANNEL,    /* PA7 -> ADC1_IN7 */
};

/* ── helpers ──────────────────────────────────────────────────────────── */

static uint16_t calc_payload_len(uint8_t nSamples)
{
    return (uint16_t)(4U + (uint16_t)nSamples * sizeof(uint16_t));
}

/* Try to send one pending buffer.  Returns true if that buffer was sent
 * (or wasn't pending), false if USB was busy and it remains pending. */
static bool flush_pending_buffer(uint8_t bufIndex)
{
    if (!(s_txPending & (1u << bufIndex))) {
        return true;   /* nothing pending — success by definition */
    }

    uint16_t nSamples = s_pendingLen[bufIndex];
    if (nSamples == 0) {
        s_txPending &= ~(1u << bufIndex);
        return true;
    }

    /* Static to avoid 2 KB stack allocation per call.  Single-threaded
     * main loop — no reentrancy concern. */
    static ProtoFrame_t frame;
    frame.cmd = CMD_ADC_DATA;
    frame.len = calc_payload_len(nSamples);
    frame.payload[0] = (uint8_t)(s_seqId & 0xFF);
    frame.payload[1] = (uint8_t)((s_seqId >> 8) & 0xFF);
    frame.payload[2] = s_pendingChMask[bufIndex];
    frame.payload[3] = s_pendingMode[bufIndex];
    memcpy(&frame.payload[4], s_sampleBuf[bufIndex],
           nSamples * sizeof(uint16_t));

    if (PROTO_SendFrame(&frame)) {
        s_seqId++;
        s_txPending &= ~(1u << bufIndex);
        return true;
    }
    return false;   /* USB busy — try again next poll */
}

/* ── public API ───────────────────────────────────────────────────────── */

void DRV_ADC_Init(void)
{
    memset(s_sampleBuf, 0, sizeof(s_sampleBuf));
    memset((void *)&s_config, 0, sizeof(s_config));
    s_running = false;
    s_activeBuf = 0;
    s_bufIdx = 0;
    s_txPending = 0;
    s_seqId = 0;
    s_numEnabled = 0;
    s_adcHandle = NULL;

    /* Enable DWT cycle counter for precise sample-rate timing.
     * DWT is always available on Cortex-M7; no peripheral clock needed.
     * Wraps every ~22 s at 192 MHz — unsigned delta handles it correctly. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    s_lastSampleCycles = 0;
    s_firstSample = true;
}

bool DRV_ADC_Configure(const DrvAdcConfig_t *cfg)
{
    if (cfg == NULL) return false;

    if (s_running) DRV_ADC_Stop();

    /* PA6 is always enabled; mask out any illegal bits. */
    s_config = *cfg;
    s_config.ch_mask = DRV_ADC_MASK_PA6;
    if (cfg->ch_mask & DRV_ADC_MASK_PA7) {
        s_config.ch_mask |= DRV_ADC_MASK_PA7;
    }

    s_numEnabled = 1;  /* PA6 */
    if (s_config.ch_mask & DRV_ADC_MASK_PA7) {
        s_numEnabled = 2;
    }

    BspAdcChannel_t channels[DRV_ADC_MAX_CHANNELS];
    uint8_t rank = 0;
    channels[rank++] = s_channelMap[0];                 /* PA6 always first */
    if (s_config.ch_mask & DRV_ADC_MASK_PA7) {
        channels[rank++] = s_channelMap[1];             /* PA7 optional */
    }

    BspAdcConfig_t bspCfg = {
        .resolution_bits = 12,
        .sample_time = ADC_SAMPLETIME_810CYCLES_5,
        .continuous = (DRV_ADC_USE_SINGLE_CONV == 0),
        .num_channels = s_numEnabled,
        .channels = channels,
    };

    if (!BSP_ADC_Init(&s_adcHandle, &bspCfg)) return false;
    return true;
}

bool DRV_ADC_Start(void)
{
    if (s_running || s_config.ch_mask == 0) return false;
    s_seqId = 0;
    s_activeBuf = 0;
    s_bufIdx = 0;
    s_txPending = 0;
    s_firstSample = true;
    s_totalSamples = 0;
    s_diagNext = DRV_ADC_DIAG_INTERVAL;
    /* ponytail: don't start the ADC here.  In single-conversion mode
     * HAL_ADC_Start sets state → BUSY and the ADC auto-stops without
     * anyone reading it.  The stale BUSY state then blocks the first
     * BSP_ADC_Start inside DRV_ADC_Poll.  Let DRV_ADC_Poll handle the
     * first (and all subsequent) conversions. */
    s_running = true;
    return true;
}

void DRV_ADC_Stop(void)
{
    if (!s_running) return;
    BSP_ADC_Stop(s_adcHandle);
    s_running = false;
}

bool DRV_ADC_IsRunning(void)
{
    return s_running;
}

/* ── main poll (called from APP_Run every loop iteration) ─────────────── */

void DRV_ADC_Poll(void)
{
    if (!s_running || s_numEnabled == 0) return;

#if DRV_ADC_DEBUG_GPIO
    {
        static bool s_gpioReported = false;
        static uint8_t s_gpioRetry = 0;
        if (!s_gpioReported) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "[ADC DBG] MODER=0x%08X AFR0=0x%08X CH=%u\r\n"
                     "[ADC DBG] ADC1 SQR1=0x%08X CR=0x%08X ISR=0x%08X CFGR=0x%08X\r\n",
                     (unsigned)BSP_GPIO_ReadModer(BSP_ADC_CH0_PIN_PORT),
                     (unsigned)BSP_GPIO_ReadAfr(BSP_ADC_CH0_PIN_PORT, 0),
                     (unsigned)s_numEnabled,
                     (unsigned)ADC1->SQR1, (unsigned)ADC1->CR,
                     (unsigned)ADC1->ISR, (unsigned)ADC1->CFGR);
            if (BSP_USB_Transmit((const uint8_t *)msg, (uint16_t)strlen(msg))) {
                s_gpioReported = true;
            } else if (++s_gpioRetry > 100) {
                s_gpioReported = true;
            }
        }
    }
#endif

    /* ── 1. Rate limiter ─────────────────────────────────────────────── */
    if (s_config.sample_rate_hz > 0) {
        uint32_t now = DWT->CYCCNT;
        uint32_t interval = BOARD_SYSCLK_HZ / s_config.sample_rate_hz;

        if (!s_firstSample) {
            if ((now - s_lastSampleCycles) < interval) {
                /* Too soon — but still try to flush pending TX */
                for (uint8_t b = 0; b < DRV_ADC_NUM_BUFS; b++) {
                    flush_pending_buffer(b);
                }
                return;
            }
        }
        s_firstSample = false;
        s_lastSampleCycles = now;
    }

    /* ── 2. Try to send any pending buffer (non-blocking) ────────────── */
    for (uint8_t b = 0; b < DRV_ADC_NUM_BUFS; b++) {
        flush_pending_buffer(b);
    }

    /* ── 3. ALWAYS read ADC (never skip due to pending TX) ───────────── */
#if DRV_ADC_USE_SINGLE_CONV
    /* ponytail: don't call BSP_ADC_Stop() here — HAL_ADC_Stop disables
     * the ADC (ADDIS), forcing the next HAL_ADC_Start to re-enable it
     * (~20 µs ADRDY wait).  In single-conversion mode the ADC auto-stops
     * after the sequence (ADSTART cleared by hw), so we just keep ADEN=1
     * and restart with ADSTART next cycle. */
    if (!BSP_ADC_Start(s_adcHandle)) return;
    for (uint8_t i = 0; i < s_numEnabled; i++) {
        if (!BSP_ADC_PollForConversion(s_adcHandle, 100)) {
            return;   /* timeout — ADC will be re-started next poll */
        }
        s_sampleBuf[s_activeBuf][s_bufIdx++] =
            (uint16_t)BSP_ADC_ReadValue(s_adcHandle);
    }
#else
    for (uint8_t i = 0; i < s_numEnabled; i++) {
        if (!BSP_ADC_PollForConversion(s_adcHandle, 100)) return;
        s_sampleBuf[s_activeBuf][s_bufIdx++] =
            (uint16_t)BSP_ADC_ReadValue(s_adcHandle);
    }
#endif

    /* ── 4. Diagnostic: LED = ADC level (ON when PA6 > ~1.65V) ──────── */
    {
        static uint16_t s_lastLedVal = 0;
        uint16_t latest = s_sampleBuf[s_activeBuf][s_bufIdx - 1];
        if ((latest > 2048) != (s_lastLedVal > 2048)) {
            s_lastLedVal = latest;
            BSP_GPIO_LedToggle();
        }
    }

    /* ── 5. If active buffer full, mark it pending and swap ──────────── */
    if (s_bufIdx >= DRV_ADC_BATCH_SIZE) {
        s_pendingLen[s_activeBuf] = s_bufIdx;
        s_pendingChMask[s_activeBuf] = s_config.ch_mask;
        s_pendingMode[s_activeBuf]  = s_config.mode;
        s_txPending |= (1u << s_activeBuf);

        /* Swap to other buffer */
        s_activeBuf ^= 1u;
        s_bufIdx = 0;
    }
}
