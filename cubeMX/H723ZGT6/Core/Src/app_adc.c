#include "app_adc.h"
#include "app_protocol.h"
#include "stm32h7xx_hal.h"

/* ── Peripherals ────────────────────────────────────────────────────── */
static ADC_HandleTypeDef  hadc1       = {0};
static TIM_HandleTypeDef  htim3       = {0};
static DMA_HandleTypeDef  hdma_adc1   = {0};

/* ── State ──────────────────────────────────────────────────────────── */
static bool s_initialized = false;
static uint32_t s_sampleRate = 1000;

/* DMA buffer: 2 channels x 32768 max = 65536 half-words (128 KB) */
static uint16_t s_rawBuf[ADC_BURST_MAX_SAMPLES * ADC_MAX_CHANNELS];

/* Burst state */
static volatile bool s_burstDone    = false;
static volatile bool s_burstErr     = false;
static uint16_t     s_burstCount   = 0;
static uint8_t      s_burstChMask  = 0;
static uint8_t      s_burstNumCh   = 0;

/* ⚠️ Rank table (ADC_300mV_ROOT_CAUSE.md) */
static const uint32_t RANK_TABLE[] = {
    ADC_REGULAR_RANK_1, ADC_REGULAR_RANK_2,
    ADC_REGULAR_RANK_3, ADC_REGULAR_RANK_4,
    ADC_REGULAR_RANK_5, ADC_REGULAR_RANK_6,
    ADC_REGULAR_RANK_7, ADC_REGULAR_RANK_8,
};

static void TIM3_Init(uint32_t sample_rate_hz);
static void ADC_DMA_Init(void);
static void ADC_ConfigChannels(uint8_t ch_mask);

/* ═══════════════════════════════════════════════════════════════════════
 *  Layer 3: dual-channel + TIM3 init
 * ═══════════════════════════════════════════════════════════════════════ */

void APP_ADC_InitDual(uint32_t sample_rate_hz)
{
    if (s_initialized) return;

    ADC1->CR = 0UL;
    ADC1->CR |= ADC_CR_ADVREGEN;
    for (volatile uint32_t d = 0; d < 20000; d++) { __NOP(); }
    ADC1->CR |= ADC_CR_ADCALLIN;
    for (volatile uint32_t d = 0; d < 2000; d++) { __NOP(); }

    MODIFY_REG(RCC->D3CCIPR, RCC_D3CCIPR_ADCSEL_Msk,
               (2UL << RCC_D3CCIPR_ADCSEL_Pos));
    __HAL_RCC_ADC12_CLK_ENABLE();

    TIM3_Init(sample_rate_hz);
    ADC_DMA_Init();

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_ASYNC_DIV4;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = ENABLE;
    hadc1.Init.NbrOfConversion       = 2;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv      = ADC_EXTERNALTRIG_T3_TRGO;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_RISING;
    hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_ONESHOT;
    hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.LeftBitShift          = ADC_LEFTBITSHIFT_NONE;
    hadc1.Init.OversamplingMode      = DISABLE;

    if (HAL_ADC_Init(&hadc1) != HAL_OK) { while (1) { } }

    ADC_ConfigChannels(0x03);

    s_initialized = true;
}

void APP_ADC_ReadDual(uint16_t raw[2])
{
    raw[0] = 0xFFFF; raw[1] = 0xFFFF;
    if (!s_initialized) return;

    MODIFY_REG(ADC1->CFGR, ADC_CFGR_DMNGT_Msk, 0UL);
    if (HAL_ADC_Start(&hadc1) != HAL_OK) return;

    __HAL_TIM_SET_COUNTER(&htim3, 0);
    HAL_TIM_Base_Start(&htim3);

    uint32_t t = 100000;
    while (!(ADC1->ISR & ADC_ISR_EOC) && --t) { __NOP(); }
    if (t) { raw[0] = (uint16_t)ADC1->DR; ADC1->ISR = ADC_ISR_EOC; }

    t = 100000;
    while (!(ADC1->ISR & ADC_ISR_EOC) && --t) { __NOP(); }
    if (t) { raw[1] = (uint16_t)ADC1->DR; ADC1->ISR = ADC_ISR_EOC; }

    HAL_TIM_Base_Stop(&htim3);
    HAL_ADC_Stop(&hadc1);

    /* Restore DMA one-shot mode */
    MODIFY_REG(ADC1->CFGR, ADC_CFGR_DMNGT_Msk,
               ADC_CFGR_DMNGT_0);   /* = ADC_CONVERSIONDATA_DMA_ONESHOT */
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Layer 4: DMA burst capture
 * ═══════════════════════════════════════════════════════════════════════ */

bool APP_ADC_StartBurst(uint8_t ch_mask, uint16_t num_samples)
{
    if (!s_initialized || num_samples == 0 || num_samples > ADC_BURST_MAX_SAMPLES)
        return false;

    uint8_t num_ch = 0;
    for (uint8_t i = 0; i < ADC_MAX_CHANNELS; i++)
        if (ch_mask & (1u << i)) num_ch++;
    if (num_ch == 0) return false;

    ADC_ConfigChannels(ch_mask);

    uint32_t total = (uint32_t)num_samples * num_ch;

    s_burstDone   = false;
    s_burstErr    = false;
    s_burstCount  = num_samples;
    s_burstChMask = ch_mask;
    s_burstNumCh  = num_ch;

    hdma_adc1.Init.Mode = DMA_NORMAL;
    HAL_DMA_Abort(&hdma_adc1);
    HAL_DMA_Init(&hdma_adc1);

    /* Use HAL_ADC_Start_DMA so DMA callbacks are linked to ADC handle */
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_rawBuf, total);

    __HAL_TIM_SET_COUNTER(&htim3, 0);
    HAL_TIM_Base_Start(&htim3);

    return true;
}

bool APP_ADC_IsBurstDone(void) { return s_burstDone; }

void APP_ADC_GetBurstResult(const uint16_t **raw0, const uint16_t **raw1,
                             uint16_t *count)
{
    if (raw0) *raw0 = s_rawBuf;
    if (raw1) *raw1 = (s_burstNumCh > 1) ? (s_rawBuf + s_burstCount) : NULL;
    if (count) *count = s_burstCount;
}

void APP_ADC_DMA_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_adc1); }

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    HAL_TIM_Base_Stop(&htim3);
    HAL_ADC_Stop_DMA(&hadc1);
    s_burstDone = true;
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc) { (void)hadc; }

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    (void)hadc;
    HAL_TIM_Base_Stop(&htim3);
    HAL_ADC_Stop_DMA(&hadc1);
    s_burstErr = true;
    s_burstDone = true;
}

/* ═══════════════════════════════════════════════════════════════════════
 *  Internal helpers
 * ═══════════════════════════════════════════════════════════════════════ */

static void TIM3_Init(uint32_t sample_rate_hz)
{
    __HAL_RCC_TIM3_CLK_ENABLE();
    htim3.Instance = TIM3;

    uint32_t tim_clk = HAL_RCC_GetPCLK1Freq();
    uint32_t apb1_div = (RCC->D2CFGR & RCC_D2CFGR_D2PPRE1_Msk)
                        >> RCC_D2CFGR_D2PPRE1_Pos;
    if (apb1_div != RCC_D2CFGR_D2PPRE1_DIV1) tim_clk *= 2;

    uint32_t ticks = tim_clk / sample_rate_hz;
    if (ticks == 0) ticks = 1;

    htim3.Init.Prescaler         = ticks - 1;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 1;
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim3);

    TIM_MasterConfigTypeDef sMaster = {0};
    sMaster.MasterOutputTrigger = TIM_TRGO_UPDATE;
    sMaster.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMaster);

    s_sampleRate = sample_rate_hz;
}

static void ADC_DMA_Init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_adc1.Instance                 = DMA1_Stream1;
    hdma_adc1.Init.Request             = DMA_REQUEST_ADC1;
    hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode                = DMA_NORMAL;
    hdma_adc1.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_adc1.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

    HAL_DMA_Init(&hdma_adc1);
    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
}

static void ADC_ConfigChannels(uint8_t ch_mask)
{
    /* Disable ADC if running (H7 uses ADDIS to request stop) */
    if (ADC1->CR & ADC_CR_ADEN) {
        ADC1->CR |= ADC_CR_ADDIS;
        while (ADC1->CR & ADC_CR_ADEN) { }
    }

    uint8_t rank = 0;
    ADC_ChannelConfTypeDef sChan = {0};
    sChan.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
    sChan.SingleDiff   = LL_ADC_SINGLE_ENDED;

    if (ch_mask & 0x01) {
        sChan.Channel = ADC_CHANNEL_3;
        sChan.Rank    = RANK_TABLE[rank++];
        HAL_ADC_ConfigChannel(&hadc1, &sChan);
    }
    if (ch_mask & 0x02) {
        sChan.Channel = ADC_CHANNEL_7;
        sChan.Rank    = RANK_TABLE[rank++];
        HAL_ADC_ConfigChannel(&hadc1, &sChan);
    }

    if (rank > 0)
        MODIFY_REG(ADC1->SQR1, ADC_SQR1_L_Msk, ((uint32_t)(rank - 1)) << ADC_SQR1_L_Pos);
}

/* ── Layer 5: host-driven sample rate reconfig ────────────────────── */
void APP_ADC_SetSampleRate(uint32_t sample_rate_hz)
{
    if (sample_rate_hz == 0) return;

    uint32_t tim_clk = HAL_RCC_GetPCLK1Freq();
    uint32_t apb1_div = (RCC->D2CFGR & RCC_D2CFGR_D2PPRE1_Msk)
                        >> RCC_D2CFGR_D2PPRE1_Pos;
    if (apb1_div != RCC_D2CFGR_D2PPRE1_DIV1) tim_clk *= 2;

    uint32_t ticks = tim_clk / sample_rate_hz;
    if (ticks == 0) ticks = 1;

    __HAL_TIM_SET_PRESCALER(&htim3, ticks - 1);
    s_sampleRate = sample_rate_hz;
}

/* ── Layer 5: send DMA buffer as CMD_ADC_DATA frames ──────────────── */
void APP_ADC_SendBurstData(void)
{
    uint8_t num_ch = s_burstNumCh;
    uint16_t spc  = s_burstCount;

    if (num_ch == 0 || spc == 0) return;

    /* Max samples per frame = (2048 - 4) / (2 * num_ch) */
    uint16_t max_per_frame = (FRAME_MAX_PAYLOAD - 4) / (2u * num_ch);
    uint16_t seq_id = 0;

    for (uint32_t offset = 0; offset < spc; offset += max_per_frame) {
        uint16_t chunk = (uint16_t)((spc - offset) < max_per_frame
                                    ? (spc - offset) : max_per_frame);

        Frame_t frame = { .cmd = CMD_ADC_DATA };
        uint16_t pos = 0;

        frame.payload[pos++] = (uint8_t)(seq_id & 0xFF);
        frame.payload[pos++] = (uint8_t)((seq_id >> 8) & 0xFF);
        frame.payload[pos++] = s_burstChMask;
        frame.payload[pos++] = 0;  /* mode */

        /* Interleave: [CH0, CH1, CH0, CH1, ...] */
        for (uint16_t i = 0; i < chunk; i++) {
            for (uint8_t c = 0; c < num_ch; c++) {
                uint16_t s = s_rawBuf[(offset + i) * num_ch + c];
                frame.payload[pos++] = (uint8_t)(s & 0xFF);
                frame.payload[pos++] = (uint8_t)((s >> 8) & 0xFF);
            }
        }

        frame.len = pos;
        APP_Protocol_SendFrame(&frame);
        seq_id++;

        /* Small gap to let USB ISR complete */
        for (volatile uint32_t d = 0; d < 10000; d++) { __NOP(); }
    }
}
