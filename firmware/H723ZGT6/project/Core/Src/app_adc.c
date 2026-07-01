/*
 * app_adc.c
 * ADC driver: double-buffered polled acquisition, decoupled from USB TX.
 *
 * While one buffer is being sent via USB (or waiting for USB), the other
 * buffer keeps collecting samples.  Sampling never stops; USB latency
 * adds at most one batch of end-to-end delay.
 *
 * Single-conversion mode (ContinuousConvMode = DISABLE):
 * HAL_ADC_Start sets ADSTART, ADC converts the full scan sequence once,
 * then ADSTART is cleared by hardware.  The next poll cycle restarts.
 * This guarantees rank-1 (PA6) always finishes first in scan mode.
 */
#include "app_adc.h"
#include "app_protocol.h"
#include "main.h"
#include "usbd_cdc_if.h"
#include <string.h>

/* --- Peripheral handles --- */
ADC_HandleTypeDef hadc1;

/* --- Double-buffer (ping-pong) --- */
#define ADC_NUM_BUFS            2

static uint16_t s_sampleBuf[ADC_NUM_BUFS][ADC_BATCH_SIZE];
static uint8_t  s_activeBuf;      /* 0 or 1: which buffer ADC fills */
static uint8_t  s_bufIdx;         /* write position inside active buffer */
static uint8_t  s_txPending;      /* bitmask: bit b = buffer b has unsent data */
static uint16_t s_pendingLen[ADC_NUM_BUFS];
static uint8_t  s_pendingChMask[ADC_NUM_BUFS];
static uint8_t  s_pendingMode[ADC_NUM_BUFS];

/* --- State --- */
static AdcConfig_t s_config = {0};
static volatile bool s_running = false;
static uint16_t s_seqId = 0;
static uint8_t  s_numEnabled = 0;

/* --- DWT rate limiter --- */
static uint32_t s_lastSampleCycles = 0;
static bool     s_firstSample = true;

/* --- Channel map (pin -> ADC1_INx, corrected for STM32H723ZGT6) --- */
static const uint32_t s_channelMap[ADC_SCAN_CHANNELS] = {
    ADC_CHANNEL_3,   /* bit0: PA6 -> ADC1_IN3 */
    ADC_CHANNEL_7,   /* bit1: PA7 -> ADC1_IN7 */
    ADC_CHANNEL_9,   /* bit2: PB0 -> ADC1_IN9 */
    ADC_CHANNEL_5,   /* bit3: PB1 -> ADC1_IN5 */
    ADC_CHANNEL_10,  /* bit4: PC0 -> ADC1_IN10 */
    ADC_CHANNEL_11,  /* bit5: PC1 -> ADC1_IN11 */
    ADC_CHANNEL_12,  /* bit6: PC2 -> ADC1_IN12 */
    ADC_CHANNEL_13,  /* bit7: PC3 -> ADC1_IN13 */
};

/* --- Rank encoding table (HAL requires encoded values, not plain integers) --- */
static const uint32_t s_rankTable[ADC_SCAN_CHANNELS] = {
    LL_ADC_REG_RANK_1,
    LL_ADC_REG_RANK_2,
    LL_ADC_REG_RANK_3,
    LL_ADC_REG_RANK_4,
    LL_ADC_REG_RANK_5,
    LL_ADC_REG_RANK_6,
    LL_ADC_REG_RANK_7,
    LL_ADC_REG_RANK_8,
};

/* ── helpers ──────────────────────────────────────────────────────────── */

/* Try to send one pending buffer.  Returns true if that buffer was sent
 * (or wasn't pending), false if USB was busy and it remains pending. */
static bool flush_pending_buffer(uint8_t bufIndex)
{
    if (!(s_txPending & (1u << bufIndex))) {
        return true;   /* nothing pending */
    }

    uint16_t nSamples = s_pendingLen[bufIndex];
    if (nSamples == 0) {
        s_txPending &= ~(1u << bufIndex);
        return true;
    }

    /* Static to avoid 2 KB stack allocation per call.
     * Single-threaded main loop — no reentrancy concern. */
    static Frame_t frame;
    uint16_t payloadLen = 4 + nSamples * sizeof(uint16_t);
    frame.cmd = CMD_ADC_DATA;
    frame.len = payloadLen;
    frame.payload[0] = (uint8_t)(s_seqId & 0xFF);
    frame.payload[1] = (uint8_t)((s_seqId >> 8) & 0xFF);
    frame.payload[2] = s_pendingChMask[bufIndex];
    frame.payload[3] = s_pendingMode[bufIndex];
    memcpy(&frame.payload[4], s_sampleBuf[bufIndex],
           nSamples * sizeof(uint16_t));

    if (APP_Protocol_SendFrame(&frame)) {
        s_seqId++;
        s_txPending &= ~(1u << bufIndex);
        return true;
    }
    return false;   /* USB busy — try again next poll */
}

/* ================================================================
 * Public API
 * ================================================================ */

void APP_ADC_Init(void)
{
    memset(s_sampleBuf, 0, sizeof(s_sampleBuf));
    memset((void *)&s_config, 0, sizeof(s_config));
    s_running = false;
    s_activeBuf = 0;
    s_bufIdx = 0;
    s_txPending = 0;
    s_seqId = 0;
    s_numEnabled = 0;

    /* Enable DWT cycle counter for precise sample-rate timing.
     * DWT is always available on Cortex-M7; no peripheral clock needed.
     * Unsigned delta handles counter wraparound correctly (~22 s at 192 MHz). */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    s_lastSampleCycles = 0;
    s_firstSample = true;
}

bool APP_ADC_Configure(const AdcConfig_t *cfg)
{
    if (cfg == NULL || cfg->ch_mask == 0) return false;

    if (s_running) APP_ADC_Stop();

    HAL_ADC_DeInit(&hadc1);
    s_config = *cfg;

    /* Count enabled channels */
    s_numEnabled = 0;
    for (uint8_t i = 0; i < ADC_SCAN_CHANNELS; i++) {
        if (cfg->ch_mask & (1u << i)) s_numEnabled++;
    }
    if (s_numEnabled == 0) return false;

    /* ADC1 init: single conversion scan, no external trigger, no DMA */
    __HAL_RCC_ADC12_CLK_ENABLE();

    /* Select ADC kernel clock source: per_ck (HCLK = 192 MHz)
     * With ClockPrescaler DIV4: ADC clock = 192/4 = 48 MHz */
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    PeriphClkInit.AdcClockSelection    = RCC_ADCCLKSOURCE_CLKP;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) return false;

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = (s_numEnabled > 1) ? ADC_SCAN_ENABLE : ADC_SCAN_DISABLE;
    hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    hadc1.Init.LowPowerAutoWait      = DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.NbrOfConversion       = s_numEnabled;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
    hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.OversamplingMode      = DISABLE;

    if (HAL_ADC_Init(&hadc1) != HAL_OK) return false;
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK)
        return false;

    /* Configure each enabled channel with proper rank encoding */
    uint8_t rank = 0;
    for (uint8_t i = 0; i < ADC_SCAN_CHANNELS; i++) {
        if (cfg->ch_mask & (1u << i)) {
            ADC_ChannelConfTypeDef sChan = {0};
            sChan.Channel      = s_channelMap[i];
            sChan.Rank         = s_rankTable[rank++];
            sChan.SamplingTime = ADC_SAMPLETIME_810CYCLES_5;
            sChan.SingleDiff   = ADC_SINGLE_ENDED;
            sChan.OffsetNumber = ADC_OFFSET_NONE;
            if (HAL_ADC_ConfigChannel(&hadc1, &sChan) != HAL_OK) return false;
        }
    }
    return true;
}

bool APP_ADC_Start(void)
{
    if (s_running || s_config.ch_mask == 0) return false;

    s_seqId  = 0;
    s_activeBuf = 0;
    s_bufIdx = 0;
    s_txPending = 0;
    s_firstSample = true;

    /* Don't call HAL_ADC_Start here.  In single-conversion mode
     * HAL_ADC_Start sets state -> BUSY and the ADC auto-stops without
     * anyone reading it.  The stale BUSY state then blocks the first
     * HAL_ADC_Start inside APP_ADC_Poll.  Let Poll handle all starts. */
    s_running = true;
    return true;
}

void APP_ADC_Stop(void)
{
    if (!s_running) return;
    HAL_ADC_Stop(&hadc1);
    s_running = false;
}

bool APP_ADC_IsRunning(void) { return s_running; }

/* ================================================================
 * Poll called from main loop
 * ================================================================ */

void APP_ADC_Poll(void)
{
    if (!s_running || s_numEnabled == 0) return;

    /* ── 1. Rate limiter (DWT cycle counter) ─────────────────────────── */
    if (s_config.sample_rate_hz > 0) {
        uint32_t now = DWT->CYCCNT;
        uint32_t interval = SystemCoreClock / s_config.sample_rate_hz;

        if (!s_firstSample) {
            if ((now - s_lastSampleCycles) < interval) {
                /* Too soon — still flush pending TX */
                for (uint8_t b = 0; b < ADC_NUM_BUFS; b++) {
                    flush_pending_buffer(b);
                }
                return;
            }
        }
        s_firstSample = false;
        s_lastSampleCycles = now;
    }

    /* ── 2. Try to send any pending buffer (non-blocking) ────────────── */
    for (uint8_t b = 0; b < ADC_NUM_BUFS; b++) {
        flush_pending_buffer(b);
    }

    /* ── 3. ALWAYS read ADC (never skip due to pending TX) ────────────── */
    if (HAL_ADC_Start(&hadc1) != HAL_OK) return;

    for (uint8_t i = 0; i < s_numEnabled; i++) {
        if (HAL_ADC_PollForConversion(&hadc1, 100) != HAL_OK) {
            return;   /* timeout — re-start next poll */
        }
        s_sampleBuf[s_activeBuf][s_bufIdx++] =
            (uint16_t)HAL_ADC_GetValue(&hadc1);
    }

    /* ── 4. If active buffer full, mark pending and swap ──────────────── */
    if (s_bufIdx >= ADC_BATCH_SIZE) {
        s_pendingLen[s_activeBuf] = s_bufIdx;
        s_pendingChMask[s_activeBuf] = s_config.ch_mask;
        s_pendingMode[s_activeBuf]  = s_config.mode;
        s_txPending |= (1u << s_activeBuf);

        /* Swap to other buffer */
        s_activeBuf ^= 1u;
        s_bufIdx = 0;
    }
}
