#include "app_adc.h"
#include "stm32h7xx_hal.h"

static ADC_HandleTypeDef hadc1 = {0};
static TIM_HandleTypeDef htim3 = {0};
static bool s_initialized_dual = false;
static uint32_t s_sampleRate = 1000;         /* default 1 kHz */

/* ⚠️  Rank encoding table — NEVER use bare integers 1,2,3,...
 *     (see ADC_300mV_ROOT_CAUSE.md)                                */
static const uint32_t RANK_TABLE[] = {
    ADC_REGULAR_RANK_1,   /* index 0 → Rank 1 */
    ADC_REGULAR_RANK_2,   /* index 1 → Rank 2 */
    ADC_REGULAR_RANK_3,
    ADC_REGULAR_RANK_4,
    ADC_REGULAR_RANK_5,
    ADC_REGULAR_RANK_6,
    ADC_REGULAR_RANK_7,
    ADC_REGULAR_RANK_8,
};

/* ── TIM3 init: generates TRGO pulses to trigger ADC ────────────────── */
static void TIM3_Init(uint32_t sample_rate_hz)
{
    __HAL_RCC_TIM3_CLK_ENABLE();

    htim3.Instance = TIM3;

    /* APB1 timer clock = 2 × PCLK1 = 192 MHz */
    uint32_t tim_clk = HAL_RCC_GetPCLK1Freq();
    uint32_t apb1_div = (RCC->D2CFGR & RCC_D2CFGR_D2PPRE1_Msk)
                        >> RCC_D2CFGR_D2PPRE1_Pos;
    if (apb1_div != RCC_D2CFGR_D2PPRE1_DIV1) {
        tim_clk *= 2;
    }

    /* ARR=1 → one timer overflow → one TRGO per period.
     * PSC = (tim_clk / sample_rate) - 1                               */
    uint32_t ticks = tim_clk / sample_rate_hz;
    if (ticks == 0) ticks = 1;

    htim3.Init.Prescaler         = ticks - 1;
    htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3.Init.Period            = 1;          /* overflow every tick */
    htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim3);

    /* TRGO = Update event → triggers ADC on every overflow */
    TIM_MasterConfigTypeDef sMaster = {0};
    sMaster.MasterOutputTrigger = TIM_TRGO_UPDATE;
    sMaster.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMaster);

    s_sampleRate = sample_rate_hz;
}

/* ── Layer 3: dual-channel + TIM3 external trigger ───────────────────── */
void APP_ADC_InitDual(uint32_t sample_rate_hz)
{
    if (s_initialized_dual) return;

    /* Voltage regulator + clock source */
    ADC1->CR = 0UL;
    ADC1->CR |= ADC_CR_ADVREGEN;
    for (volatile uint32_t d = 0; d < 20000; d++) { __NOP(); }
    ADC1->CR |= ADC_CR_ADCALLIN;
    for (volatile uint32_t d = 0; d < 2000; d++) { __NOP(); }

    MODIFY_REG(RCC->D3CCIPR, RCC_D3CCIPR_ADCSEL_Msk,
               (2UL << RCC_D3CCIPR_ADCSEL_Pos));
    __HAL_RCC_ADC12_CLK_ENABLE();

    /* Init TIM3 for hardware triggering */
    TIM3_Init(sample_rate_hz);

    hadc1.Instance = ADC1;

    hadc1.Init.ClockPrescaler        = ADC_CLOCK_ASYNC_DIV4;
    hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode          = ENABLE;
    hadc1.Init.NbrOfConversion       = 2;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv      = ADC_EXTERNALTRIG_T3_TRGO;      /* TIM3 TRGO */
    hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_RISING;
    hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
    hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    hadc1.Init.LeftBitShift          = ADC_LEFTBITSHIFT_NONE;
    hadc1.Init.OversamplingMode      = DISABLE;

    if (HAL_ADC_Init(&hadc1) != HAL_OK) { while (1) { } }

    /* Rank 1 = PA6 (CH3), Rank 2 = PA7 (CH7) */
    ADC_ChannelConfTypeDef sChan = {0};

    sChan.Channel      = ADC_CHANNEL_3;
    sChan.Rank         = RANK_TABLE[0];          /* ADC_REGULAR_RANK_1 */
    sChan.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
    sChan.SingleDiff   = LL_ADC_SINGLE_ENDED;
    if (HAL_ADC_ConfigChannel(&hadc1, &sChan) != HAL_OK) { while (1) { } }

    sChan.Channel      = ADC_CHANNEL_7;
    sChan.Rank         = RANK_TABLE[1];          /* ADC_REGULAR_RANK_2 */
    if (HAL_ADC_ConfigChannel(&hadc1, &sChan) != HAL_OK) { while (1) { } }

    s_initialized_dual = true;
}

/* ── Start TIM3 (begins triggering ADC) ──────────────────────────────── */
void APP_ADC_StartTim3(void)
{
    __HAL_TIM_SET_COUNTER(&htim3, 0);
    HAL_TIM_Base_Start(&htim3);
}

/* ── Stop TIM3 ───────────────────────────────────────────────────────── */
void APP_ADC_StopTim3(void)
{
    HAL_TIM_Base_Stop(&htim3);
}

/* ── One triggered scan: start ADC+Timer, wait, read, stop ───────────── */
void APP_ADC_ReadDual(uint16_t raw[2])
{
    raw[0] = 0xFFFF;
    raw[1] = 0xFFFF;
    if (!s_initialized_dual) return;

    /* Prime ADC for external trigger */
    if (HAL_ADC_Start(&hadc1) != HAL_OK) return;

    /* Fire a single TIM3 pulse → ADC scan */
    APP_ADC_StartTim3();

    /* Spin-wait for first EOC (PA6) */
    uint32_t t = 100000;
    while (!(ADC1->ISR & ADC_ISR_EOC) && --t) { __NOP(); }
    if (t) {
        raw[0] = (uint16_t)ADC1->DR;
        ADC1->ISR = ADC_ISR_EOC;
    }

    /* Spin-wait for second EOC (PA7) — scan auto-continues */
    t = 100000;
    while (!(ADC1->ISR & ADC_ISR_EOC) && --t) { __NOP(); }
    if (t) {
        raw[1] = (uint16_t)ADC1->DR;
        ADC1->ISR = ADC_ISR_EOC;
    }

    APP_ADC_StopTim3();
    HAL_ADC_Stop(&hadc1);
}
