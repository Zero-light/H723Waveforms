/*
 * bsp_iwdg.c
 * H723ZG_V1 Independent Watchdog implementation (register level).
 * LSI ~32 kHz, prescaler 256 => 8 ms tick.  Reload=125 => ~1 s timeout.
 *
 * ponytail: use direct register access instead of HAL_IWDG so we do not
 * depend on stm32h7xx_hal_iwdg.c/h, which CubeMX omits when IWDG is not
 * enabled in the .ioc configuration.
 */
#include "bsp_iwdg.h"
#include "stm32h7xx.h"

void BSP_IWDG_Init(void)
{
    /* Enable LSI oscillator; IWDG is clocked by LSI. */
    RCC->CSR |= RCC_CSR_LSION;
    while ((RCC->CSR & RCC_CSR_LSIRDY) == 0) {
        /* wait until LSI is stable */
    }

    /* Unlock IWDG registers for configuration. */
    IWDG1->KR = 0x5555u;

    /* Prescaler = 256 (PR = 6). */
    IWDG1->PR = 6u;

    /* Reload value = 500.  Timeout = (500 + 1) * 256 / 32000 ≈ 4.01 s.
     * Give plenty of margin for initialization and occasional long operations. */
    IWDG1->RLR = 500u;

    /* Wait for prescaler and reload registers to update. */
    while ((IWDG1->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0) {
    }

    /* Start the watchdog.  Once started, it cannot be stopped. */
    IWDG1->KR = 0xCCCCu;
}

void BSP_IWDG_Refresh(void)
{
    /* Reload the counter (kick the dog). */
    IWDG1->KR = 0xAAAAu;
}
