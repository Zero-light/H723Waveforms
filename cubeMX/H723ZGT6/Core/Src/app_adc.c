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

/* --- Debug: GPIO toggle on each ADC sample for oscilloscope verification --- */
#define ADC_DEBUG_PORT   GPIOB
#define ADC_DEBUG_PIN    GPIO_PIN_12  /* PB12 — scope probe here */

/* DWT cycle counter ticks at HCLK.  Hard-coded to match SystemClock_Config:
 * HSI=64MHz, PLLM=4, PLLN=24, PLLP=2 → SYSCLK=192MHz → HCLK=192MHz.
 * ponytail: HAL_RCC_GetHCLKFreq() returns VCO freq (384 MHz) on some HAL
 * versions due to PLLP-divider decoding bug.  If the clock tree changes,
 * update this constant.  Upgrade path: call HAL_RCC_GetSysClockFreq() and
 * verify against a known timer. */
#define APP_ADC_HCLK_HZ  192000000UL

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

/* 鈹€鈹€ helpers 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€ */

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
     * Single-threaded main loop 鈥?no reentrancy concern. */
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
    return false;   /* USB busy 鈥?try again next poll */
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

    /* Debug pin: toggle on each ADC sample for oscilloscope rate measurement.
     * Frequency on PC4 = sample_rate_hz / 2 (one full cycle = 2 toggles). */
    GPIO_InitTypeDef dbg = {0};
    dbg.Pin   = ADC_DEBUG_PIN;
    dbg.Mode  = GPIO_MODE_OUTPUT_PP;
    dbg.Pull  = GPIO_NOPULL;
    dbg.Speed = GPIO_SPEED_FREQ_MEDIUM;
    HAL_GPIO_Init(ADC_DEBUG_PORT, &dbg);
    HAL_GPIO_WritePin(ADC_DEBUG_PORT, ADC_DEBUG_PIN, GPIO_PIN_RESET);
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

    /* 鈹€鈹€ 1. Rate limiter (DWT cycle counter) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€ */
    if (s_config.sample_rate_hz > 0) {
        uint32_t now = DWT->CYCCNT;
        /* DWT->CYCCNT ticks at HCLK (192 MHz per SystemClock_Config).
         * HAL_RCC_GetHCLKFreq() is avoided — known to return VCO (384 MHz)
         * on some STM32H7 HAL versions due to PLLP divider decode bug. */
        uint32_t interval = APP_ADC_HCLK_HZ / s_config.sample_rate_hz;

        if (!s_firstSample) {
            if ((now - s_lastSampleCycles) < interval) {
                /* Too soon 鈥?still flush pending TX */
                for (uint8_t b = 0; b < ADC_NUM_BUFS; b++) {
                    flush_pending_buffer(b);
                }
                return;
            }
        }
        s_firstSample = false;
        s_lastSampleCycles = now;
    }

    /* 鈹€鈹€ 2. Try to send any pending buffer (non-blocking) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€ */
    for (uint8_t b = 0; b < ADC_NUM_BUFS; b++) {
        flush_pending_buffer(b);
    }

    /* 鈹€鈹€ 3. ALWAYS read ADC (never skip due to pending TX) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€ */
    if (HAL_ADC_Start(&hadc1) != HAL_OK) return;

    for (uint8_t i = 0; i < s_numEnabled; i++) {
        if (HAL_ADC_PollForConversion(&hadc1, 100) != HAL_OK) {
            return;   /* timeout 鈥?re-start next poll */
        }
        s_sampleBuf[s_activeBuf][s_bufIdx++] =
            (uint16_t)HAL_ADC_GetValue(&hadc1);
        /* Toggle debug pin — scope measures 2× toggle rate = sample rate */
        HAL_GPIO_TogglePin(ADC_DEBUG_PORT, ADC_DEBUG_PIN);
    }

    /* 鈹€鈹€ 4. If active buffer full, mark pending and swap 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€ */
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

/* ================================================================
 * Burst capture (called from main.c OnFrameReceived)
 * ================================================================
 *
 * Stops stream (if running), reconfigures ADC for continuous single-channel
 * scan, captures num_samples at max speed, sends as one CMD_ADC_DATA frame.
 *
 * This is a blocking call (busy-wait on ADC).  num_samples is capped so the
 * entire capture fits in FRAME_MAX_PAYLOAD (max ~1020 uint16 samples per frame).
 * For multi-channel burst, samples are interleaved: ch0, ch1, ch0, ch1, ...
 *
 * Returns true on success, false on config failure or USB busy.
 */
bool APP_ADC_BurstCapture(uint8_t ch_mask, uint16_t num_samples)
{
    if (ch_mask == 0) return false;

    /* Count enabled channels */
    uint8_t num_en = 0;
    for (uint8_t i = 0; i < ADC_SCAN_CHANNELS; i++) {
        if (ch_mask & (1u << i)) num_en++;
    }
    if (num_en == 0) return false;

    /* Cap num_samples to fit in one USB frame payload */
    uint16_t maxSamples = (FRAME_MAX_PAYLOAD - 4) / (sizeof(uint16_t) * num_en);
    if (num_samples > maxSamples) num_samples = maxSamples;
    if (num_samples == 0) return false;

    uint16_t totalValues = num_samples * num_en;  /* total uint16 values */

    /* Stop any running stream */
    bool wasRunning = s_running;
    if (wasRunning) {
        HAL_ADC_Stop(&hadc1);
        s_running = false;
    }

    /* Ensure ADC is on */
    if (hadc1.State == HAL_ADC_STATE_RESET) {
        /* Re-init if we need to */
    }

    /* 1. Configure ADC: scan + continuous + fastest sampling */
    ADC_HandleTypeDef *hadc = &hadc1;

    hadc->Instance = ADC1;
    hadc->Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;   /* 48 MHz */
    hadc->Init.Resolution            = ADC_RESOLUTION_12B;
    hadc->Init.ScanConvMode          = (num_en > 1) ? ADC_SCAN_ENABLE : ADC_SCAN_DISABLE;
    hadc->Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    hadc->Init.LowPowerAutoWait      = DISABLE;
    hadc->Init.ContinuousConvMode    = ENABLE;        /* continuous for burst */
    hadc->Init.NbrOfConversion       = num_en;
    hadc->Init.DiscontinuousConvMode = DISABLE;
    hadc->Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    hadc->Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc->Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
    hadc->Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    hadc->Init.OversamplingMode      = DISABLE;

    if (HAL_ADC_Init(hadc) != HAL_OK) return false;

    /* 2. Configure each enabled channel */
    /* Rank table from existing code */
    static const uint32_t rankTbl[ADC_SCAN_CHANNELS] = {
        LL_ADC_REG_RANK_1, LL_ADC_REG_RANK_2, LL_ADC_REG_RANK_3, LL_ADC_REG_RANK_4,
        LL_ADC_REG_RANK_5, LL_ADC_REG_RANK_6, LL_ADC_REG_RANK_7, LL_ADC_REG_RANK_8,
    };
    static const uint32_t chanMap[ADC_SCAN_CHANNELS] = {
        ADC_CHANNEL_3,  ADC_CHANNEL_7,  ADC_CHANNEL_9, ADC_CHANNEL_5,
        ADC_CHANNEL_10, ADC_CHANNEL_11, ADC_CHANNEL_12, ADC_CHANNEL_13,
    };

    uint8_t rankIdx = 0;
    for (uint8_t i = 0; i < ADC_SCAN_CHANNELS; i++) {
        if (ch_mask & (1u << i)) {
            ADC_ChannelConfTypeDef chCfg = {0};
            chCfg.Channel      = chanMap[i];
            chCfg.Rank         = rankTbl[rankIdx++];
            chCfg.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;   /* fastest: 2.5 ADC clk */
            chCfg.SingleDiff   = ADC_SINGLE_ENDED;
            chCfg.OffsetNumber = ADC_OFFSET_NONE;
            if (HAL_ADC_ConfigChannel(hadc, &chCfg) != HAL_OK) return false;
        }
    }

    /* 3. Allocate local buffer for all samples */
    uint16_t buf[512];  /* stack buffer; max ~512 uint16 = ~256 samples for 1 ch */
    if (totalValues > 512) totalValues = 512;   /* cap to stack size */
    uint16_t captured = 0;

    /* 4. Start continuous conversion */
    if (HAL_ADC_Start(hadc) != HAL_OK) return false;

    /* 5. Poll all samples */
    for (uint16_t s = 0; s < num_samples; s++) {
        for (uint8_t ch = 0; ch < num_en; ch++) {
            if (HAL_ADC_PollForConversion(hadc, 100) != HAL_OK) {
                HAL_ADC_Stop(hadc);
                return false;
            }
            buf[captured++] = (uint16_t)HAL_ADC_GetValue(hadc);
        }
    }

    /* 6. Stop ADC */
    HAL_ADC_Stop(hadc);

    /* 7. Build and send CMD_ADC_DATA frame */
    Frame_t frame;
    uint16_t payloadLen = 4 + captured * sizeof(uint16_t);
    if (payloadLen > FRAME_MAX_PAYLOAD) payloadLen = FRAME_MAX_PAYLOAD;
    frame.cmd = CMD_ADC_DATA;
    frame.len = payloadLen;
    frame.payload[0] = 0;  /* seq_id low */
    frame.payload[1] = 0;  /* seq_id high */
    frame.payload[2] = ch_mask;
    frame.payload[3] = 0;  /* mode = RAW_STREAM */
    /* Copy sample data */
    uint16_t copyBytes = captured * sizeof(uint16_t);
    if (copyBytes > FRAME_MAX_PAYLOAD - 4) copyBytes = FRAME_MAX_PAYLOAD - 4;
    memcpy(&frame.payload[4], buf, copyBytes);

    bool sentOk = APP_Protocol_SendFrame(&frame);

    /* 8. Restore stream state if it was running */
    /* (Caller will re-send config if stream is wanted again) */

    return sentOk;
}
