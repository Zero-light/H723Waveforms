/*
 * drv_wavegen.c
 * Waveform generator driver implementation using BSP_TIM/BSP_DMA/BSP_GPIO.
 */
#include "drv_wavegen.h"
#include "bsp_tim.h"
#include "bsp_dma.h"
#include "bsp_gpio.h"

#include "board_config.h"
#include <string.h>


static uint32_t s_waveBuf[DRV_WAVE_MAX_POINTS] __attribute__((aligned(32)));
static DrvWaveConfig_t s_config = {0};
static volatile bool s_running = false;
/* ponytail: set false on configure/init, true on LoadData.  Start rejects
 * starting with stale data after a reconfiguration. */
static volatile bool s_data_valid = false;
static BspTimHandle_t s_timHandle = NULL;
static BspDmaHandle_t s_dmaHandle = NULL;

/* Map logical channel index (0..4) to GPIO pin on port A. */
static const uint16_t s_wavePinMap[] = {
    GPIO_PIN_0,  /* CH0 */
    GPIO_PIN_1,  /* CH1 */
    GPIO_PIN_2,  /* CH2 */
    GPIO_PIN_3,  /* CH3 */
    GPIO_PIN_5,  /* CH4 */
};
#define WAVE_PIN_COUNT  (sizeof(s_wavePinMap) / sizeof(s_wavePinMap[0]))

uint32_t DRV_WaveGen_BuildBSRR(uint8_t state)
{
    uint32_t bsrr = 0;
    if (state & (1u << 0)) bsrr |= (1u << BSP_WAVE_CH0_BIT);
    if (state & (1u << 1)) bsrr |= (1u << BSP_WAVE_CH1_BIT);
    if (state & (1u << 2)) bsrr |= (1u << BSP_WAVE_CH2_BIT);
    if (state & (1u << 3)) bsrr |= (1u << BSP_WAVE_CH3_BIT);
    if (state & (1u << 4)) bsrr |= (1u << BSP_WAVE_CH4_BIT);

    if (!(state & (1u << 0))) bsrr |= (1u << (BSP_WAVE_CH0_BIT + 16));
    if (!(state & (1u << 1))) bsrr |= (1u << (BSP_WAVE_CH1_BIT + 16));
    if (!(state & (1u << 2))) bsrr |= (1u << (BSP_WAVE_CH2_BIT + 16));
    if (!(state & (1u << 3))) bsrr |= (1u << (BSP_WAVE_CH3_BIT + 16));
    if (!(state & (1u << 4))) bsrr |= (1u << (BSP_WAVE_CH4_BIT + 16));
    return bsrr;
}

static void s_configureWavePins(uint8_t ch_mask, BspGpioMode_t mode)
{
    for (uint8_t ch = 0; ch < WAVE_PIN_COUNT; ch++) {
        if (ch_mask & (1u << ch)) {
            /* Safety: pull pin HIGH via BSRR before switching mode to OUTPUT.
             * HAL_GPIO_Init writes MODER first; without this, a push-pull pin
             * briefly drives the default ODR value (0) to the external load,
             * which can look like a short-to-ground on low-impedance circuits. */
            if (mode == BSP_GPIO_MODE_OUTPUT_PP || mode == BSP_GPIO_MODE_OUTPUT_OD) {
                GPIOA->BSRR = s_wavePinMap[ch];
            }
            const BspGpioPin_t pin = {
                GPIOA, s_wavePinMap[ch], mode,
                BSP_GPIO_PULL_NONE, BSP_GPIO_SPEED_MEDIUM, 0
            };
            BSP_GPIO_InitPin(&pin);
        }
    }
}

void DRV_WaveGen_Init(void)
{
    memset(s_waveBuf, 0, sizeof(s_waveBuf));
    memset((void *)&s_config, 0, sizeof(s_config));
    s_running = false;
    s_data_valid = false;

    BSP_TIM_Init(&s_timHandle);

    BspDmaConfig_t dmaCfg = {
        .periph_addr = (void *)&GPIOA->BSRR,
        .mem_addr = s_waveBuf,
        .length = 0,
        .mode = BSP_DMA_MODE_CIRCULAR,
    };
    BSP_DMA_Init(&s_dmaHandle, &dmaCfg);
    BSP_TIM_LinkDma(s_timHandle, s_dmaHandle);
}

bool DRV_WaveGen_Configure(const DrvWaveConfig_t *cfg)
{
    if (cfg == NULL || cfg->num_points == 0 || cfg->num_points > DRV_WAVE_MAX_POINTS)
        return false;
    /* Safety: reject zero mask or reserved bits (same check as Start). */
    if (cfg->ch_mask == 0 || (cfg->ch_mask & ~0x1Fu) != 0)
        return false;

    if (s_running) DRV_WaveGen_Stop();

    /* Validate the timer frequency BEFORE overwriting s_config so a failed
     * Configure does not leave stale/inconsistent state. */
    if (!BSP_TIM_SetFrequency(s_timHandle, cfg->sample_rate_hz))
        return false;

    s_config = *cfg;
    s_data_valid = false;
    return true;
}

bool DRV_WaveGen_LoadData(const uint32_t *data, uint16_t len)
{
    if (data == NULL || len == 0 || len > DRV_WAVE_MAX_POINTS) return false;
    if (s_running) return false;

    /* Safety: reject an all-zero BSRR array.  A full-zero pattern writes
     * nothing to BSRR, so enabled channels remain at their last driven
     * level (likely LOW after a previous falling edge), creating a
     * sustained sink-current path through external loads. */
    bool all_zero = true;
    for (uint16_t i = 0; i < len; i++) {
        if (data[i] != 0) { all_zero = false; break; }
    }
    if (all_zero) return false;

    memcpy(s_waveBuf, data, len * sizeof(uint32_t));
    /* ponytail: flush D-Cache so DMA reads the new waveform data.  On
     * Cortex-M7 with D-Cache enabled, the DMA sees physical RAM which
     * may still hold the default 2-point wave from boot if the cache
     * hasn't been written back. */
    SCB_CleanDCache_by_Addr((uint32_t *)s_waveBuf, len * sizeof(uint32_t));

    s_config.num_points = len;
    s_data_valid = true;
    return true;
}

bool DRV_WaveGen_Start(void)
{
    /* ponytail: validate state first.  After a failed Start we must NOT leave
     * s_running==true — the deferred-ACK loop would skip LoadData/Configure
     * frames while s_pendingAckFlag remains set, and the next Start attempt
     * would hit s_running==true and fail again. */
    if (s_running)  return false;
    if (s_config.num_points == 0 || s_config.num_points > DRV_WAVE_MAX_POINTS)
        return false;
    if (!s_data_valid) return false;
    /* ch_mask must have at least one enabled channel within valid range */
    if (s_config.ch_mask == 0 || (s_config.ch_mask & ~0x1Fu) != 0)
        return false;

    /* Enable only the GPIO channels selected by ch_mask as outputs.
     * Other wave pins remain high-impedance inputs from bsp_init. */
    s_configureWavePins(s_config.ch_mask, BSP_GPIO_MODE_OUTPUT_PP);

    BspDmaConfig_t dmaCfg = {
        .periph_addr = (void *)&GPIOA->BSRR,
        .mem_addr = s_waveBuf,
        .length = s_config.num_points,
        .mode = BSP_DMA_MODE_CIRCULAR,
    };
    if (!BSP_DMA_Start(s_dmaHandle, &dmaCfg)) {
        /* DMA start failed — return pins to input so they don't float */
        s_configureWavePins(s_config.ch_mask, BSP_GPIO_MODE_INPUT);
        return false;
    }
    BSP_TIM_EnableDmaUpdate(s_timHandle);
    if (!BSP_TIM_BaseStart(s_timHandle)) {
        BSP_DMA_Stop(s_dmaHandle);
        BSP_TIM_DisableDmaUpdate(s_timHandle);
        s_configureWavePins(s_config.ch_mask, BSP_GPIO_MODE_INPUT);
        return false;
    }

    s_running = true;
    return true;
}

void DRV_WaveGen_Stop(void)
{
    if (!s_running) return;

    /* Stop DMA first so no more BSRR writes arrive, then immediately set
     * all enabled wave pins HIGH to prevent them from resting at their last
     * driven level (which is often LOW for an active channel). */
    BSP_DMA_Stop(s_dmaHandle);
    for (uint8_t ch = 0; ch < WAVE_PIN_COUNT; ch++) {
        if (s_config.ch_mask & (1u << ch)) {
            GPIOA->BSRR = s_wavePinMap[ch];
        }
    }

    BSP_TIM_BaseStop(s_timHandle);
    BSP_TIM_DisableDmaUpdate(s_timHandle);

    s_running = false;
    /* ponytail: invalidate data so a stale LoadData from before the stop
     * cannot be reused for a new Start without an explicit reload.  This
     * closes the loop where Configure→LoadData→Start→Stop→Start could
     * succeed with the old waveform data. */
    s_data_valid = false;

    /* Return enabled wave pins to high-impedance input so they do not
     * continue to be driven after the waveform generator is stopped. */
    s_configureWavePins(s_config.ch_mask, BSP_GPIO_MODE_INPUT);
}

bool DRV_WaveGen_IsRunning(void)
{
    return s_running;
}
