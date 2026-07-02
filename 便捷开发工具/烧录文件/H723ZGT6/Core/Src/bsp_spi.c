/*
 * bsp_spi.c
 * H723ZG_V1 SPI2 implementation.
 */
#include "bsp_spi.h"
#include "bsp_gpio.h"
#include "board_config.h"
#include "stm32h7xx_hal.h"

static SPI_HandleTypeDef s_hspi2;

bool BSP_SPI_Init(BspSpiHandle_t *handle, const BspSpiConfig_t *cfg)
{
    if (handle == NULL || cfg == NULL) return false;

    __HAL_RCC_SPI2_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* SCK/MISO/MOSI as AF5 */
    const BspGpioPin_t spi_pins[] = {
        { BSP_SPI_SCK_PORT,  BSP_SPI_SCK_PIN,  BSP_GPIO_MODE_AF_PP, BSP_GPIO_PULL_NONE, BSP_GPIO_SPEED_VERY_HIGH, BSP_SPI_SCK_AF },
        { BSP_SPI_MISO_PORT, BSP_SPI_MISO_PIN, BSP_GPIO_MODE_AF_PP, BSP_GPIO_PULL_NONE, BSP_GPIO_SPEED_VERY_HIGH, BSP_SPI_MISO_AF },
        { BSP_SPI_MOSI_PORT, BSP_SPI_MOSI_PIN, BSP_GPIO_MODE_AF_PP, BSP_GPIO_PULL_NONE, BSP_GPIO_SPEED_VERY_HIGH, BSP_SPI_MOSI_AF },
    };
    for (uint8_t i = 0; i < 3; i++) {
        BSP_GPIO_InitPin(&spi_pins[i]);
    }

    /* CS software controlled */
    const BspGpioPin_t cs_pin = {
        BSP_SPI_CS_PORT, BSP_SPI_CS_PIN,
        BSP_GPIO_MODE_OUTPUT_PP, BSP_GPIO_PULL_UP, BSP_GPIO_SPEED_VERY_HIGH, 0
    };
    BSP_GPIO_InitPin(&cs_pin);
    BSP_SPI_CsHigh();

    if (s_hspi2.Instance != NULL) {
        HAL_SPI_DeInit(&s_hspi2);
    }

    s_hspi2.Instance = BSP_SPI_INSTANCE;
    s_hspi2.Init.Mode = SPI_MODE_MASTER;
    s_hspi2.Init.Direction = SPI_DIRECTION_2LINES;
    s_hspi2.Init.DataSize = (cfg->data_size == 16) ? SPI_DATASIZE_16BIT : SPI_DATASIZE_8BIT;
    s_hspi2.Init.CLKPolarity = cfg->cpol ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW;
    s_hspi2.Init.CLKPhase = cfg->cpha ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
    s_hspi2.Init.NSS = SPI_NSS_SOFT;

    uint32_t prescaler;
    switch (cfg->prescaler & 0x07u) {
        case 0: prescaler = SPI_BAUDRATEPRESCALER_2; break;
        case 1: prescaler = SPI_BAUDRATEPRESCALER_4; break;
        case 2: prescaler = SPI_BAUDRATEPRESCALER_8; break;
        case 3: prescaler = SPI_BAUDRATEPRESCALER_16; break;
        case 4: prescaler = SPI_BAUDRATEPRESCALER_32; break;
        case 5: prescaler = SPI_BAUDRATEPRESCALER_64; break;
        case 6: prescaler = SPI_BAUDRATEPRESCALER_128; break;
        default: prescaler = SPI_BAUDRATEPRESCALER_256; break;
    }
    s_hspi2.Init.BaudRatePrescaler = prescaler;
    s_hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
    s_hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
    s_hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    s_hspi2.Init.CRCPolynomial = 0x0;
    s_hspi2.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    s_hspi2.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;

    if (HAL_SPI_Init(&s_hspi2) != HAL_OK) {
        return false;
    }
    __HAL_SPI_ENABLE(&s_hspi2);

    *handle = &s_hspi2;
    return true;
}

bool BSP_SPI_Transmit(BspSpiHandle_t handle, const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    (void)handle;
    return (HAL_SPI_Transmit(&s_hspi2, (uint8_t *)data, len, timeout_ms) == HAL_OK);
}

void BSP_SPI_CsLow(void)
{
    BSP_GPIO_WritePin(BSP_SPI_CS_PORT, BSP_SPI_CS_PIN, false);
}

void BSP_SPI_CsHigh(void)
{
    BSP_GPIO_WritePin(BSP_SPI_CS_PORT, BSP_SPI_CS_PIN, true);
}

void BSP_SPI_DeInit(BspSpiHandle_t handle)
{
    (void)handle;
    HAL_SPI_DeInit(&s_hspi2);
}
