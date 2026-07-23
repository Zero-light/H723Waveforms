/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : TIM2+DMA waveform generator with USB CDC protocol.
  *                   Clock: HSI 64 MHz + HSI48 for USB.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "stm32h7xx_hal.h"
#include "usb_device.h"
#include "app_protocol.h"
#include "app_wavegen.h"
#include "app_spi.h"

#include "app_dac.h"
#include "app_adc.h"
#include "usbd_cdc_if.h"
#include <stdio.h>
#include <string.h>

/* Provide symbols required by other .c files still in the build */
TIM_HandleTypeDef htim2;
DMA_HandleTypeDef hdma_tim2_up;

void Error_Handler(void)
{
    while (1) { }
}

/* Private function prototypes */
static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_TIM2_Init(void);

/* Protocol callback forward declaration */
static void OnFrameReceived(const Frame_t *frame);

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
    /* MCU Configuration--------------------------------------------------------*/
    HAL_Init();
    SystemClock_Config();

    /* Initialize all configured peripherals */
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_TIM2_Init();

    /* USER CODE BEGIN 2 */
    /* Init waveform generator, SPI, ADC and protocol stack */
    APP_WaveGen_Init();
    APP_SPI_Init();
    APP_DAC_Init();
    APP_ADC_InitDual(1000);  /* Layer 3: TIM3-triggered, 1 kHz sample rate */
    APP_Protocol_Init(OnFrameReceived);

    /* Start USB CDC device */
    MX_USB_DEVICE_Init();

    /* ── Layer 0: Enable ADC12 clock (shared by ADC1/ADC2 on H723) ── */
    __HAL_RCC_ADC12_CLK_ENABLE();

    /* Default waveform: 1 kHz square wave on SCLK (PA1), 下降沿触发 */
    /* 周期 = 2 个Update: 先高电平(SET), 再低电平(RESET), 下降沿有效 */
    /* Configure TIM2 for 2 kHz update rate @ 192 MHz timer clock (APB1=96MHz, timer=192MHz) */
    __HAL_TIM_SET_PRESCALER(&htim2, 0);
    __HAL_TIM_SET_AUTORELOAD(&htim2, 95999);   /* 192 MHz / 96000 = 2 kHz */
    HAL_TIM_GenerateEvent(&htim2, TIM_EVENTSOURCE_UPDATE);

    static uint32_t s_defaultWave[2];
    s_defaultWave[0] = (1u << 1);   /* SET  PA1/SCLK -> 高电平 */
    s_defaultWave[1] = (1u << 17);  /* RESET PA1/SCLK -> 低电平 (下降沿) */

    APP_WaveGen_LoadData(s_defaultWave, 2);
    APP_WaveGen_Start();
    /* Note: APP_WaveGen_Start sets s_running = true so future
     * APP_WaveGen_Configure will properly call APP_WaveGen_Stop. */
    /* USER CODE END 2 */

    /* Infinite loop */
    uint32_t tickLast = HAL_GetTick();
    uint32_t tickTest = HAL_GetTick();
    bool firstHeartbeat = true;
    while (1)
    {
        if (APP_ADC_IsBurstDone()) { APP_ADC_SendBurstData(); }

        /* Process received USB frames in main-loop context (not in ISR) */
        Frame_t rxFrame;
        while (APP_Protocol_GetPendingFrame(&rxFrame)) {
            OnFrameReceived(&rxFrame);
        }

        /* Heartbeat: SQR1 dump once, then "TEST" every 1s */
        if (HAL_GetTick() - tickTest >= 1000) {
            static Frame_t testFrame;
            if (firstHeartbeat) {
                int n = snprintf((char *)testFrame.payload,
                    FRAME_MAX_PAYLOAD - 2,
                    "ADC SQR1=0x%08lX",
                    ADC1->SQR1);
                testFrame.cmd = 0xFF;
                testFrame.len = (uint16_t)(n < 0 ? 0 : n);
                APP_Protocol_SendFrame(&testFrame);
                firstHeartbeat = false;
            } else {
                testFrame.cmd = 0xFF;
                testFrame.len = 4;
                memcpy(testFrame.payload, "TEST", 4);
                APP_Protocol_SendFrame(&testFrame);
            }
            tickTest = HAL_GetTick();
        }

        /* Heartbeat on PG7: 1 Hz slow blink (non-blocking) */
        if (HAL_GetTick() - tickLast >= 500) {
            HAL_GPIO_TogglePin(GPIOG, GPIO_PIN_7);
            tickLast = HAL_GetTick();
        }
    }
}

/**
  * @brief  Protocol frame callback — handles commands from host PC.
  */
static void OnFrameReceived(const Frame_t *frame)
{
    if (frame == NULL) return;

    switch (frame->cmd)
    {
        case CMD_WAVE_CONFIG:
        {
            /* Payload: sample_rate_hz (4B) + num_points (2B) + ch_mask (1B) */
            if (frame->len < 7) {
                APP_Protocol_SendAck(CMD_WAVE_CONFIG, false);
                break;
            }
            WaveConfig_t cfg = {0};
            cfg.sample_rate_hz = ((uint32_t)frame->payload[0])
                               | ((uint32_t)frame->payload[1] << 8)
                               | ((uint32_t)frame->payload[2] << 16)
                               | ((uint32_t)frame->payload[3] << 24);
            cfg.num_points = ((uint16_t)frame->payload[4])
                           | ((uint16_t)frame->payload[5] << 8);
            cfg.ch_mask = frame->payload[6];

            bool ok = APP_WaveGen_Configure(&cfg);
            APP_Protocol_SendAck(CMD_WAVE_CONFIG, ok);
            break;
        }

        case CMD_WAVE_DATA:
        {
            /* Payload: array of 32-bit BSRR masks */
            if (frame->len == 0 || (frame->len % 4) != 0) {
                APP_Protocol_SendAck(CMD_WAVE_DATA, false);
                break;
            }
            uint16_t num_words = frame->len / 4;
            /* s_waveBuf 在 app_wavegen.c 中定义为 WAVE_MAX_POINTS (8192)，
             * 直接用帧数据解码到静态缓冲区，避免中间截断。 */
            if (num_words == 0 || num_words > WAVE_MAX_POINTS) {
                APP_Protocol_SendAck(CMD_WAVE_DATA, false);
                break;
            }
            for (uint16_t i = 0; i < num_words; i++) {
                s_waveBuf[i] = ((uint32_t)frame->payload[i*4])
                        | ((uint32_t)frame->payload[i*4 + 1] << 8)
                        | ((uint32_t)frame->payload[i*4 + 2] << 16)
                        | ((uint32_t)frame->payload[i*4 + 3] << 24);
            }
            bool ok = APP_WaveGen_LoadData(s_waveBuf, num_words);
            APP_Protocol_SendAck(CMD_WAVE_DATA, ok);
            break;
        }

        case CMD_WAVE_CTRL:
        {
            /* Payload[0]: 1 = start, 0 = stop
             * Payload[1]: 1 = one-shot (auto-stop + pulldown after one buffer) */
            if (frame->len < 1) {
                APP_Protocol_SendAck(CMD_WAVE_CTRL, false);
                break;
            }
            bool ok;
            if (frame->payload[0]) {
                if (frame->len >= 2 && frame->payload[1]) {
                    ok = APP_WaveGen_OneShot();
                } else {
                    ok = APP_WaveGen_Start();
                }
            } else {
                APP_WaveGen_Stop();
                ok = true;
            }
            APP_Protocol_SendAck(CMD_WAVE_CTRL, ok);
            break;
        }

        case CMD_ADC_CONFIG:
        {
            /* payload: ch_mask(1) + rate(4) + mode(1)
             * mode bit0: 1 = differential (PA6=INP3, PA7=INN3),
             *            0 = single-ended (default, used by snap panel) */
            if (frame->len < 6) { APP_Protocol_SendAck(CMD_ADC_CONFIG, false); break; }
            uint32_t rate = ((uint32_t)frame->payload[1])
                          | ((uint32_t)frame->payload[2] << 8)
                          | ((uint32_t)frame->payload[3] << 16)
                          | ((uint32_t)frame->payload[4] << 24);
            bool diff = (frame->payload[5] & 0x01) != 0;
            APP_ADC_SetDiffMode(diff);
            APP_ADC_SetSampleRate(rate);
            APP_Protocol_SendAck(CMD_ADC_CONFIG, true);
            break;
        }

        case CMD_ADC_BURST:
        {
            if (frame->len < 5) { APP_Protocol_SendAck(CMD_ADC_BURST, false); break; }
            uint8_t  ch = frame->payload[0];
            uint32_t ns = ((uint32_t)frame->payload[1])
                        | ((uint32_t)frame->payload[2] << 8)
                        | ((uint32_t)frame->payload[3] << 16)
                        | ((uint32_t)frame->payload[4] << 24);
            bool ok = APP_ADC_StartBurst(ch, (uint16_t)(ns > ADC_BURST_MAX_SAMPLES
                                                         ? ADC_BURST_MAX_SAMPLES : ns));
            APP_Protocol_SendAck(CMD_ADC_BURST, ok);
            break;
        }

        case CMD_SPI_CONFIG:
        {
            /* Payload: [cpol_cpha_byte, prescaler, data_size] */
            if (frame->len < 5) {
                APP_Protocol_SendAck(CMD_SPI_CONFIG, false);
                break;
            }
            SpiConfig_t cfg = {0};
            cfg.cpol = (frame->payload[0] >> 1) & 1;
            cfg.cpha = frame->payload[0] & 1;
            cfg.prescaler = frame->payload[1];
            cfg.data_size = frame->payload[2];
            bool ok = APP_SPI_Configure(&cfg);
            APP_Protocol_SendAck(CMD_SPI_CONFIG, ok);
            break;
        }

        case CMD_SPI_XFER:
        {
            /* Batch register write format:
             * payload[0] = num_regs
             * payload[1] = data_width (8 or 16)
             * payload[2..] = addr, data, addr, data ...
             */
            if (frame->len < 2) {
                APP_Protocol_SendAck(CMD_SPI_XFER, false);
                break;
            }
            uint8_t num_regs = frame->payload[0];
            uint8_t data_width = frame->payload[1];
            bool ok = APP_SPI_WriteRegs(&frame->payload[2], num_regs, data_width);
            APP_Protocol_SendAck(CMD_SPI_XFER, ok);
            break;
        }

        case CMD_DAC_SET:
        {
            /* Payload: value(2B LE, 12-bit) */
            if (frame->len < 2) {
                APP_Protocol_SendAck(CMD_DAC_SET, false);
                break;
            }
            uint16_t value = ((uint16_t)frame->payload[0])
                           | ((uint16_t)frame->payload[1] << 8);
            bool ok = APP_DAC_SetValue(value);
            APP_Protocol_SendAck(CMD_DAC_SET, ok);
            break;
        }

        default:
            /* Unknown command — send NACK */
            APP_Protocol_SendAck(frame->cmd, false);
            break;
    }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* Enable HSI48 for USB clock source */
    __HAL_RCC_HSI48_ENABLE();
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_HSI48RDY) == RESET) { }

    /* Scale voltage regulator for 192 MHz */
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0) != HAL_OK)
    {
        Error_Handler();
    }

    /* Configure main PLL: HSI 64 MHz -> PLL1 -> 192 MHz
     * M=4, N=24, P=2  => 64/4*24/2 = 192 MHz
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 4;
    RCC_OscInitStruct.PLL.PLLN = 24;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                                |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    /* FLASH_LATENCY_4 for 192 MHz @ 3.3V */
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 0;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 4294967295;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
    {
        Error_Handler();
    }
    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
    {
        Error_Handler();
    }
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
  * Enable DMA controller clock + TIM2 DMA stream
  */
static void MX_DMA_Init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();
    /* H723 DMAMUX1 shares DMA1 clock (no separate RCC bit) */
    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
}


/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3, GPIO_PIN_RESET);

    /* DAC pin: PA4 as analog */
    GPIO_InitTypeDef GPIO_InitStructAnalog = {0};
    GPIO_InitStructAnalog.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStructAnalog.Pull = GPIO_NOPULL;

    GPIO_InitStructAnalog.Pin = GPIO_PIN_4;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStructAnalog);

    /* ADC pins: PA6 (ADC1_IN3) + PA7 (ADC1_IN7) as analog */
    GPIO_InitStructAnalog.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStructAnalog);

    /* PC1 (ADC1_INP11) + PC4 (ADC1_IN4) as analog — ADC CH2 + XYNC snapshot */
    GPIO_InitStructAnalog.Pin = GPIO_PIN_1 | GPIO_PIN_4;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStructAnalog);

    /* PB0 (ADC1_IN9) as analog — CLK snapshot input */
    GPIO_InitStructAnalog.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStructAnalog);

    __HAL_RCC_SYSCFG_CLK_ENABLE();

    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* PG7 on-board LED */
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitStruct.Pin = GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOG, GPIO_PIN_7, GPIO_PIN_SET);
}





