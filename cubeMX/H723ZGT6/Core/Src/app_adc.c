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
static bool s_diffMode   = false;   /* false = single-ended, true = ch0 differential (INP3/INN3 on PA6/PA7) */

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
    hadc1.Init.ClockPrescaler        = ADC_CLOCK_ASYNC_DIV1;  /* 64 MHz ADC clk (per_ck=HSI 64MHz /1) */
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = ENABLE;
    hadc1.Init.NbrOfConversion       = ADC_MAX_CHANNELS;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv      = ADC_EXTERNALTRIG_T3_TRGO;
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_RISING;
    hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_ONESHOT;
    hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.LeftBitShift          = ADC_LEFTBITSHIFT_NONE;
    hadc1.Init.OversamplingMode      = DISABLE;

    if (HAL_ADC_Init(&hadc1) != HAL_OK) { while (1) { } }

    ADC_ConfigChannels(0x03);  /* PA6/PA7 (diff or single) + PC1 */

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

/* XYNC = PC4. 等 XYNC=1 再启动 burst，对齐帧同步。 */
static void WaitForXyncRise(void)
{
    /* 临时把 PC4 改成 GPIO input（它平时是 analog / ADC1_IN4） */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = GPIO_PIN_4;
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* 先等到 XYNC=0（确保后面检测到的是上升沿，不是已经在高的电平） */
    uint32_t t = 500000;
    while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_4) == GPIO_PIN_SET && --t) { __NOP(); }
    /* 再等到 XYNC=1 */
    t = 500000;
    while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_4) == GPIO_PIN_RESET && --t) { __NOP(); }
}

bool APP_ADC_StartBurst(uint8_t ch_mask, uint16_t num_samples)
{
    if (!s_initialized || num_samples == 0 || num_samples > ADC_BURST_MAX_SAMPLES)
        return false;

    uint8_t num_ch = 0;
    for (uint8_t m = ch_mask; m; m &= m - 1) num_ch++;  /* popcount */
    if (num_ch == 0 || num_ch > ADC_MAX_CHANNELS) return false;

    /* ── 等 XYNC 上升沿后再采，对齐帧同步 ─────────────────────── */
    /* 只在通道掩码包含 XYNC (bit4 = 0x10) 时才等；ADC 页面不含 XYNC，直接采 */
    if (ch_mask & 0x10) {
        WaitForXyncRise();
    }

    /* burst 完成后再把 PC4 交还给 analog（ADC_ConfigChannels 会重新配置它） */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = GPIO_PIN_4;
    gpio.Mode  = GPIO_MODE_ANALOG;
    gpio.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &gpio);

    ADC_ConfigChannels(ch_mask);

    uint32_t total = (uint32_t)num_samples * num_ch;

    s_burstDone   = false;
    s_burstErr    = false;
    s_burstCount  = num_samples;
    s_burstChMask = ch_mask;
    s_burstNumCh  = num_ch;

    /* Full reset: ensure ADC+DMA are stopped clean before restart */
    HAL_ADC_Stop_DMA(&hadc1);
    HAL_DMA_Abort(&hdma_adc1);
    hdma_adc1.Init.Mode = DMA_NORMAL;
    HAL_DMA_Init(&hdma_adc1);

    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_rawBuf, total) != HAL_OK) {
        s_burstDone = true;
        s_burstErr  = true;
        return false;
    }

    __HAL_TIM_SET_COUNTER(&htim3, 0);
    HAL_TIM_Base_Start(&htim3);

    return true;
}

bool APP_ADC_IsBurstDone(void) { return s_burstDone; }

void APP_ADC_GetBurstResult(const uint16_t **raw_ptr, uint16_t *count,
                             uint8_t *num_ch, uint8_t *ch_mask)
{
    if (raw_ptr) *raw_ptr = s_rawBuf;
    if (count)   *count   = s_burstCount;
    if (num_ch)  *num_ch  = s_burstNumCh;
    if (ch_mask) *ch_mask = s_burstChMask;
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

    /* H723: SysClk=192MHz, APB1=/2 → APB1 timer clock = 192 MHz
     * ARR=1 → update_rate = tim_clk / (PSC+1) / 2                   */
    uint32_t tim_clk = 192000000UL;
    uint32_t ticks = tim_clk / (sample_rate_hz * 2u);
    if (ticks < 2) ticks = 2;

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
        sChan.Channel    = ADC_CHANNEL_3;
        sChan.SingleDiff = s_diffMode ? LL_ADC_DIFFERENTIAL_ENDED
                                       : LL_ADC_SINGLE_ENDED;
        sChan.SamplingTime = s_diffMode ? ADC_SAMPLETIME_8CYCLES_5
                                         : ADC_SAMPLETIME_2CYCLES_5;
        sChan.Rank    = RANK_TABLE[rank++];
        HAL_ADC_ConfigChannel(&hadc1, &sChan);
    }
    /* ch_mask bit1 (PA7 standalone single-ended): only valid when diff mode
     * is OFF. When diff mode is on, PA7 is the negative leg of ch3 (INN3)
     * and can no longer be enabled as a separate channel. */
    if ((ch_mask & 0x02) && !s_diffMode) {
        sChan.Channel      = ADC_CHANNEL_7;
        sChan.SingleDiff   = LL_ADC_SINGLE_ENDED;
        sChan.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
        sChan.Rank    = RANK_TABLE[rank++];
        HAL_ADC_ConfigChannel(&hadc1, &sChan);
    }
    if (ch_mask & 0x04) {
        sChan.Channel    = ADC_CHANNEL_11;  /* PC1 = ADC1_INP11  (ADC CH2) */
        sChan.SingleDiff = LL_ADC_SINGLE_ENDED;
        sChan.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
        sChan.Rank    = RANK_TABLE[rank++];
        HAL_ADC_ConfigChannel(&hadc1, &sChan);
    }
    if (ch_mask & 0x08) {
        sChan.Channel = ADC_CHANNEL_9;   /* PB0 = ADC1_IN9  (CLK via dupont) */
        sChan.Rank    = RANK_TABLE[rank++];
        HAL_ADC_ConfigChannel(&hadc1, &sChan);
    }
    if (ch_mask & 0x10) {
        sChan.Channel = ADC_CHANNEL_4;   /* PC4 = ADC1_IN4  (XYNC via dupont) */
        sChan.Rank    = RANK_TABLE[rank++];
        HAL_ADC_ConfigChannel(&hadc1, &sChan);
    }

    if (rank > 0)
        MODIFY_REG(ADC1->SQR1, ADC_SQR1_L_Msk, ((uint32_t)(rank - 1)) << ADC_SQR1_L_Pos);
}

/* ── Host-driven sample rate reconfig ─────────────────────────────── */
void APP_ADC_SetDiffMode(bool enable)
{
    s_diffMode = enable;
}

void APP_ADC_SetSampleRate(uint32_t sample_rate_hz)
{
    if (sample_rate_hz == 0) return;
    uint32_t tim_clk = 192000000UL;
    uint32_t ticks = tim_clk / (sample_rate_hz * 2u);
    if (ticks < 2) ticks = 2;

    HAL_TIM_Base_Stop(&htim3);
    htim3.Init.Prescaler = ticks - 1;
    htim3.Init.Period    = 1;
    HAL_TIM_Base_Init(&htim3);
    s_sampleRate = sample_rate_hz;
}

/* ── Send DMA buffer as CMD_ADC_DATA frames ───────────────────────── */
void APP_ADC_SendBurstData(void)
{
    uint8_t  num_ch = s_burstNumCh;
    uint16_t spc    = s_burstCount;
    if (num_ch == 0 || spc == 0) return;

    uint16_t max_per_frame = (FRAME_MAX_PAYLOAD - 4) / (2u * num_ch);
    uint16_t seq_id = 0;

    for (uint32_t offset = 0; offset < spc; offset += max_per_frame) {
        uint16_t chunk = (spc - offset) < max_per_frame
                       ? (uint16_t)(spc - offset) : max_per_frame;
        Frame_t frame = { .cmd = CMD_ADC_DATA };
        uint16_t pos = 0;
        frame.payload[pos++] = (uint8_t)(seq_id & 0xFF);
        frame.payload[pos++] = (uint8_t)((seq_id >> 8) & 0xFF);
        frame.payload[pos++] = s_burstChMask;
        frame.payload[pos++] = 0;
        for (uint16_t i = 0; i < chunk; i++)
            for (uint8_t c = 0; c < num_ch; c++) {
                uint16_t s = s_rawBuf[(offset + i) * num_ch + c];
                frame.payload[pos++] = (uint8_t)(s & 0xFF);
                frame.payload[pos++] = (uint8_t)((s >> 8) & 0xFF);
            }
        frame.len = pos;
        APP_Protocol_SendFrame(&frame);
        seq_id++;
        for (volatile uint32_t d = 0; d < 10000; d++) { __NOP(); }
    }
    s_burstDone = false;
}
