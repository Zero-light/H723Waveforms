/*
 * app_spi.c
 * Software bit-bang SPI on GPIOs — bypasses H7 SPI hardware entirely.
 *
 * Pin mapping:
 *   PB2  = MOSI  (output)
 *   PC10 = SCK   (output)
 *   PC11 = MISO  (input, not used for TX)
 *   PC12 = CS    (output, software controlled)
 *
 * Timing per register: CS↓ → 24 SCK (addr 8 + data 16, MSB first) → CS↑
 */
#include "app_spi.h"
#include "stm32h7xx_hal.h"
#include "usbd_cdc_if.h"
#include <string.h>
#include <stdio.h>

/* ── GPIO pins (PB12=CS, PB13=SCK, PB14=MISO, PB15=MOSI) ───────────────── */
#define BB_MOSI_PORT   GPIOB
#define BB_MOSI_PIN    GPIO_PIN_15

#define BB_SCK_PORT    GPIOB
#define BB_SCK_PIN     GPIO_PIN_13

#define BB_MISO_PORT   GPIOB
#define BB_MISO_PIN    GPIO_PIN_14

#define BB_CS_PORT     GPIOB
#define BB_CS_PIN      GPIO_PIN_12

/* ── delay: ~2.6µs @ 192MHz → SCLK ≈ 128 kHz ─────────────────────────── */
#define BB_DELAY()   do { for (volatile uint32_t _d = 0; _d < 500; _d++) { __NOP(); } } while(0)

/* ── state ─────────────────────────────────────────────────────────────── */
static bool s_spiReady = false;
static bool s_cpol = false;

/* ── helpers ───────────────────────────────────────────────────────────── */

static inline void bb_mosi(uint8_t bit) {
    HAL_GPIO_WritePin(BB_MOSI_PORT, BB_MOSI_PIN, bit ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static inline void bb_sck(uint8_t bit) {
    HAL_GPIO_WritePin(BB_SCK_PORT, BB_SCK_PIN, bit ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static inline void bb_cs(uint8_t bit) {
    HAL_GPIO_WritePin(BB_CS_PORT, BB_CS_PIN, bit ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Send one bit, MSB first.  Mode 0: idle LOW, data on falling, sample on rising. */
static void bb_send_bit(uint8_t val) {
    bb_mosi(val);
    BB_DELAY();                /* data setup */
    bb_sck(1);                 /* rising edge → slave samples */
    BB_DELAY();                /* SCK high */
    bb_sck(0);                 /* falling edge */
    BB_DELAY();                /* SCK low */
}

/* Send 24 bits: addr(8) + data(16), MSB first, continuous SCK, CS↓→CS↑ */
static void bb_tx_reg(uint8_t addr, uint16_t data) {
    /* CS↓ */
    bb_cs(0);

    /* addr byte — 8 bits, MSB first */
    for (int i = 7; i >= 0; i--) {
        bb_send_bit((addr >> i) & 1);
    }
    /* data word — 16 bits, MSB first, no gap */
    for (int i = 15; i >= 0; i--) {
        bb_send_bit((data >> i) & 1);
    }
    /* total 24 SCK complete, CS↑ */
    bb_cs(1);
    BB_DELAY();  /* ensure CS high before next frame */
}

/* ── public API ────────────────────────────────────────────────────────── */

void APP_SPI_Init(void) {
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;

    /* PB15 = MOSI */
    g.Pin = BB_MOSI_PIN;
    HAL_GPIO_Init(BB_MOSI_PORT, &g);
    HAL_GPIO_WritePin(BB_MOSI_PORT, BB_MOSI_PIN, GPIO_PIN_RESET);

    /* PB13 = SCK, idle low (MODE0) */
    g.Pin = BB_SCK_PIN;
    HAL_GPIO_Init(BB_SCK_PORT, &g);
    HAL_GPIO_WritePin(BB_SCK_PORT, BB_SCK_PIN, GPIO_PIN_RESET);

    /* PB12 = CS, idle high */
    g.Pin = BB_CS_PIN;
    HAL_GPIO_Init(BB_CS_PORT, &g);
    HAL_GPIO_WritePin(BB_CS_PORT, BB_CS_PIN, GPIO_PIN_SET);

    /* PB14 = MISO (input, unused for TX) */
    g.Pin = BB_MISO_PIN;
    g.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(BB_MISO_PORT, &g);

    s_spiReady = true;  /* auto-ready, no host config required */
}

bool APP_SPI_Configure(const SpiConfig_t *cfg) {
    if (cfg == NULL) return false;
    s_cpol = (cfg->cpol != 0);
    /* prescaler ignored in bit-bang mode; SCLK fixed ~128 kHz */
    (void)cfg->prescaler;
    s_spiReady = true;
    return true;
}

bool APP_SPI_WriteReg(uint8_t addr, uint16_t data, uint8_t data_width) {
    if (!s_spiReady) return false;
    (void)data_width;
    bb_tx_reg(addr, data);
    return true;
}

bool APP_SPI_WriteRegs(const uint8_t *payload, uint8_t num_regs, uint8_t data_width) {
    if (!s_spiReady || payload == NULL || num_regs == 0) return false;
    (void)data_width;

    /* ── debug: print num_regs ──────────────────────────────────── */
    {
        char msg[64];
        int n = snprintf(msg, sizeof(msg),
                         "[SPI DBG] num_regs=%u, data_width=%u\r\n",
                         (unsigned)num_regs, (unsigned)data_width);
        CDC_Transmit_HS((uint8_t *)msg, (uint16_t)n);
    }

    uint16_t offset = 0;
    for (uint8_t i = 0; i < num_regs; i++) {
        uint8_t  addr = payload[offset++];
        uint16_t data = payload[offset] | ((uint16_t)payload[offset + 1] << 8);
        offset += 2;

        /* ── debug: print each register ─────────────────────────── */
        {
            char msg[64];
            int n = snprintf(msg, sizeof(msg),
                             "[SPI DBG] reg[%u]: addr=0x%02X data=0x%04X\r\n",
                             (unsigned)i, (unsigned)addr, (unsigned)data);
            CDC_Transmit_HS((uint8_t *)msg, (uint16_t)n);
        }

        bb_tx_reg(addr, data);

        /* Inter-register gap ~20µs */
        for (volatile uint32_t d = 0; d < 2000; d++) { __NOP(); }
    }
    return true;
}
