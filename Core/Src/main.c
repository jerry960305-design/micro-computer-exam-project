/* USER CODE BEGIN Header */
/**
 * @file           : main.c
 * @brief          : Main program body for Project Sky-Screen - LPUART1 Version
 */
/* USER CODE END Header */

#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

typedef enum {
    SYS_STATE_NORMAL = 0,
    SYS_STATE_COMBAT
} SystemState_t;

typedef enum {
    WEATHER_SUNNY = 0,
    WEATHER_CLOUDY,
    WEATHER_RAINY,
    WEATHER_STORMY
} WeatherState_t;

#define RING_BUFFER_SIZE 128
typedef struct {
    uint8_t buffer[RING_BUFFER_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} RingBuffer_t;

#define CMD_BUFFER_SIZE 64

TIM_HandleTypeDef htim3;
UART_HandleTypeDef hlpuart1;

volatile SystemState_t current_state = SYS_STATE_NORMAL;
volatile WeatherState_t current_weather = WEATHER_SUNNY;

uint32_t white_ccr = 160;
int32_t white_step = 10;
uint32_t blue_ccr = 40;
int32_t blue_step = 20;

uint32_t last_tick_weather = 0;
uint32_t last_tick_yellow = 0;
uint32_t last_tick_alert = 0;

uint8_t rx_byte;
RingBuffer_t uart_ring_buffer = { .head = 0, .tail = 0 };
char cmd_buffer[CMD_BUFFER_SIZE];
uint8_t cmd_index = 0;

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM3_Init(void);
static void MX_LPUART1_UART_Init(void);

void Update_Weather_System(void);
void UART_SendString(const char *str);
void Process_UART_Command(char *cmd);
bool RingBuffer_Write(uint8_t data);
bool RingBuffer_Read(uint8_t *data);
void Poll_UART_RingBuffer(void);

bool RingBuffer_Write(uint8_t data) {
    uint16_t next_head = (uart_ring_buffer.head + 1) % RING_BUFFER_SIZE;
    if (next_head == uart_ring_buffer.tail) return false;
    uart_ring_buffer.buffer[uart_ring_buffer.head] = data;
    uart_ring_buffer.head = next_head;
    return true;
}

bool RingBuffer_Read(uint8_t *data) {
    if (uart_ring_buffer.head == uart_ring_buffer.tail) return false;
    *data = uart_ring_buffer.buffer[uart_ring_buffer.tail];
    uart_ring_buffer.tail = (uart_ring_buffer.tail + 1) % RING_BUFFER_SIZE;
    return true;
}

void Poll_UART_RingBuffer(void) {
    uint8_t ch;
    while (RingBuffer_Read(&ch)) {
        if (ch == '\r' || ch == '\n') {
            if (cmd_index > 0) {
                cmd_buffer[cmd_index] = '\0';
                Process_UART_Command(cmd_buffer);
                cmd_index = 0;
            }
        } else {
            if (cmd_index < CMD_BUFFER_SIZE - 1) {
                cmd_buffer[cmd_index++] = ch;
            }
        }
    }
}

void UART_SendString(const char *str) {
    HAL_UART_Transmit(&hlpuart1, (uint8_t *)str, strlen(str), HAL_MAX_DELAY);
}

void Update_Weather_System(void) {
    uint32_t current_tick = HAL_GetTick();

    if ((current_tick - last_tick_weather) >= 10) {
        last_tick_weather = current_tick;
        int32_t next_white = (int32_t)white_ccr + white_step;
        if (next_white >= 999) { next_white = 999; white_step = -10; }
        else if (next_white <= 0) { next_white = 0; white_step = 10; }
        white_ccr = (uint32_t)next_white;
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, white_ccr);

        if (current_weather == WEATHER_CLOUDY || current_weather == WEATHER_STORMY) {
            int32_t next_blue = (int32_t)blue_ccr + blue_step;
            if (next_blue >= 999) { next_blue = 999; blue_step = -20; }
            else if (next_blue <= 0) { next_blue = 0; blue_step = 20; }
            blue_ccr = (uint32_t)next_blue;
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, blue_ccr);
        } else {
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0);
        }
    }

    uint32_t yellow_interval = 0;
    if (current_weather == WEATHER_RAINY) yellow_interval = 1000;
    else if (current_weather == WEATHER_STORMY) yellow_interval = 500;

    if (yellow_interval > 0) {
        if ((current_tick - last_tick_yellow) >= yellow_interval) {
            last_tick_yellow = current_tick;
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        }
    } else {
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
    }
}

void Process_UART_Command(char *cmd) {
    int len = strlen(cmd);
    while (len > 0 && (cmd[len - 1] == '\r' || cmd[len - 1] == '\n')) {
        cmd[--len] = '\0';
    }

    if (strcmp(cmd, "sunny") == 0 || strcmp(cmd, "1") == 0) {
        current_state = SYS_STATE_NORMAL;
        current_weather = WEATHER_SUNNY;
        UART_SendString("Mode set to: SUNNY\r\n");
    } else if (strcmp(cmd, "cloudy") == 0 || strcmp(cmd, "2") == 0) {
        current_state = SYS_STATE_NORMAL;
        current_weather = WEATHER_CLOUDY;
        UART_SendString("Mode set to: CLOUDY\r\n");
    } else if (strcmp(cmd, "rainy") == 0 || strcmp(cmd, "3") == 0) {
        current_state = SYS_STATE_NORMAL;
        current_weather = WEATHER_RAINY;
        last_tick_yellow = HAL_GetTick();
        UART_SendString("Mode set to: RAINY\r\n");
    } else if (strcmp(cmd, "stormy") == 0 || strcmp(cmd, "4") == 0) {
        current_state = SYS_STATE_NORMAL;
        current_weather = WEATHER_STORMY;
        last_tick_yellow = HAL_GetTick();
        UART_SendString("Mode set to: STORMY\r\n");
    } else if (strcmp(cmd, "combat") == 0 || strcmp(cmd, "5") == 0) {
        current_state = SYS_STATE_COMBAT;
        UART_SendString("ALERT! System in COMBAT mode!\r\n");
    } else if (strcmp(cmd, "reset") == 0 || strcmp(cmd, "6") == 0) {
        current_state = SYS_STATE_NORMAL;
        // UART 的 reset 指令也同步改為只重置警報，不更改天氣
        UART_SendString("Alert RESET. Returning to previous weather.\r\n");
    } else if (strlen(cmd) > 0) {
        UART_SendString("Unknown Command. Available: sunny, cloudy, rainy, stormy, combat, reset\r\n");
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == LPUART1) {
        RingBuffer_Write(rx_byte);
        HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);
    }
}

int main(void) {
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_TIM3_Init();
    MX_LPUART1_UART_Init();

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);

    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, white_ccr);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0);

    UART_SendString("Commander login\r\n");
    HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);

    while (1) {
        Poll_UART_RingBuffer();

        if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == GPIO_PIN_RESET) {
            HAL_Delay(20);
            if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == GPIO_PIN_RESET) {
                current_weather = (WeatherState_t)((current_weather + 1) % 4);
                last_tick_yellow = HAL_GetTick();
                char msg[32];
                snprintf(msg, sizeof(msg), "Button 1 pressed: Weather %d\r\n", current_weather + 1);
                UART_SendString(msg);
                while(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == GPIO_PIN_RESET);
            }
        }

        if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1) == GPIO_PIN_RESET) {
            HAL_Delay(20);
            if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1) == GPIO_PIN_RESET) {
                current_state = SYS_STATE_COMBAT;
                UART_SendString("Button 2 pressed: COMBAT mode activated!\r\n");
                while(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1) == GPIO_PIN_RESET);
            }
        }

        // 修改 PC13 為高電位觸發 (SET)，且僅重置警報狀態 (COMBAT -> NORMAL)
        if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET) {
            HAL_Delay(20);
            if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET) {
                current_state = SYS_STATE_NORMAL;
                // 移除了 current_weather = WEATHER_SUNNY;
                UART_SendString("Button 3 pressed: Alert Reset, returning to previous weather.\r\n");
                while(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_SET);
            }
        }

        if (current_state == SYS_STATE_NORMAL) {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_3, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_RESET);
            Update_Weather_System();
        } else if (current_state == SYS_STATE_COMBAT) {
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0);

            uint32_t current_tick = HAL_GetTick();
            if ((current_tick - last_tick_alert) >= 100) {
                last_tick_alert = current_tick;
                HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_3);
                HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_4);
            }
        }
    }
}

void SystemClock_Config(void) {
}

static void MX_TIM3_Init(void) {
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 169;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 999;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_Base_Init(&htim3) != HAL_OK) { Error_Handler(); }
    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK) { Error_Handler(); }
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) { Error_Handler(); }
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK) { Error_Handler(); }

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 160;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK) { Error_Handler(); }

    sConfigOC.Pulse = 40;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK) { Error_Handler(); }
    HAL_TIM_MspPostInit(&htim3);
}

static void MX_LPUART1_UART_Init(void) {
    hlpuart1.Instance = LPUART1;
    hlpuart1.Init.BaudRate = 115200;
    hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
    hlpuart1.Init.StopBits = UART_STOPBITS_1;
    hlpuart1.Init.Parity = UART_PARITY_NONE;
    hlpuart1.Init.Mode = UART_MODE_TX_RX;
    hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&hlpuart1) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(LPUART1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(LPUART1_IRQn);
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {}
}
