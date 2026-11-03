/**
 * @file main.c
 * @brief STM32 HAL 庫綜合控制範例（包含 PWM 呼吸燈、狀態機、天氣模式、環形緩衝區 UART 指令解析）
 */

#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* --- 硬體周邊句柄宣告 --- */
TIM_HandleTypeDef htim3;
UART_HandleTypeDef huart2;

/* --- 系統狀態與天氣模式列舉 --- */
typedef enum {
    SYS_STATE_NORMAL = 0, // 正常巡航狀態
    SYS_STATE_COMBAT      // 戰術交戰狀態
} SystemState_t;

typedef enum {
    WEATHER_SUNNY = 0,    // 1. 晴空萬里
    WEATHER_CLOUDY,       // 2. 藍天白雲
    WEATHER_RAINY,        // 3. 下雨
    WEATHER_STORMY,       // 4. 狂風暴雨
    WEATHER_MAX           // 模式總數
} WeatherState_t;

/* --- 環形緩衝區 (Ring Buffer) 定義 --- */
#define RING_BUFFER_SIZE 64
typedef struct {
    uint8_t buffer[RING_BUFFER_SIZE]; // 緩衝區資料陣列
    volatile uint16_t head;           // 寫入指標
    volatile uint16_t tail;           // 讀取指標
} RingBuffer_t;

/* --- 全域變數宣告 --- */
SystemState_t currentSystemState = SYS_STATE_NORMAL; // 當前系統狀態，預設正常巡航
WeatherState_t currentWeather = WEATHER_SUNNY;       // 當前天氣模式，預設晴空萬里

RingBuffer_t uartRxRingBuffer;                       // UART 接收環形緩衝區
uint8_t uartRxSingleByte;                            // 用於中斷接收的單字元變數

/* 呼吸燈相關全域變數 */
uint32_t lastWhiteBreathTick = 0;                    // 白燈呼吸燈上次更新時間
int32_t whiteBrightness = 160;                       // 白燈當前亮度 (CCR1)
int32_t whiteStep = 10;                              // 白燈呼吸步長

uint32_t lastBlueBreathTick = 0;                     // 藍燈呼吸燈上次更新時間
int32_t blueBrightness = 0;                          // 藍燈當前亮度 (CCR2)
int32_t blueStep = 20;                               // 藍燈呼吸步長

/* 閃爍/爆閃相關全域變數 */
uint32_t lastYellowFlashTick = 0;                    // 黃燈閃爍上次更新時間
GPIO_PinState yellowLedState = GPIO_PIN_RESET;       // 黃燈當前狀態

uint32_t lastCombatFlashTick = 0;                    // 戰術交戰紅燈/蜂鳴器上次更新時間
GPIO_PinState combatState = GPIO_PIN_RESET;          // 戰術交戰交替狀態

/* --- 函式原型宣告 --- */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM3_Init(void);

void Process_Buttons(void);
void Update_LEDs_And_Buzzer(void);
void Poll_UART_RingBuffer(void);
void Execute_Command(char *cmd);
void Send_UART_String(const char *str);

int main(void)
{
    /* 1. 初始化 HAL 庫 */
    HAL_Init();

    /* 2. 設定系統時脈 (170MHz) */
    SystemClock_Config();

    /* 3. 初始化所有周邊硬體 */
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_TIM3_Init();

    /* 4. 啟動 TIM3 PWM 通道 1 與通道 2 */
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);

    /* 5. 啟動 UART 中斷接收（單字元接收） */
    HAL_UART_Receive_IT(&huart2, &uartRxSingleByte, 1);

    /* 6. 開機時透過 UART 發送 Commander login 提示訊息 */
    Send_UART_String("Commander login\r\n");

    /* 主迴圈 */
    while (1)
    {
        /* 處理按鈕輸入（含軟體去彈跳） */
        Process_Buttons();

        /* 非阻塞式輪詢環形緩衝區並解析指令 */
        Poll_UART_RingBuffer();

        /* 更新各項燈光、呼吸燈與蜂鳴器狀態 */
        Update_LEDs_And_Buzzer();
    }
}

/**
 * @brief 系統時脈設定 (170MHz)
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /** 設定主內部調節器輸出電壓 */
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1);

    /** 初始化 RCC 振盪器 */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
    RCC_OscInitStruct.PLL.PLLN = 85;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /** 初始化 CPU, AHB 與 APB 匯流排時脈 */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_SYSCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief TIM3 初始化函式 (產生 1kHz PWM 波)
 */
static void MX_TIM3_Init(void)
{
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};

    /* 假設定時器輸入時脈經除頻後使計數頻鐘對應 Prescaler = 169, Period = 999 */
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 169;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 999;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
    {
        Error_Handler();
    }

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }

    /* 設定 PWM 通道 1 (白燈，預設 pulse = 160) */
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 160;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }

    /* 設定 PWM 通道 2 (藍燈，預設 pulse = 400) */
    sConfigOC.Pulse = 400;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
    {
        Error_Handler();
    }

    HAL_TIM_MspPostInit(&htim3);
}

/**
 * @brief USART2 初始化函式 (115200 8N1)
 */
static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.AdvancedInit.InitState = UART_ADVINIT_NO_INIT;
    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }
}

/**
 * @brief GPIO 初始化函式 (按鈕與 LED 腳位)
 */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 啟用 GPIO 時鐘 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* 設定 PC2(綠燈), PC3(紅燈), PC4(蜂鳴器) 初始為低電位 (Output) */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4, GPIO_PIN_RESET);

    /* 設定 PA5(黃燈) 初始為低電位 (Output) */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

    /* 配置 PC2, PC3, PC4 為數位輸出 */
    GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* 配置 PA5 為數位輸出 */
    GPIO_InitStruct.Pin = GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* 配置 PC0, PC1, PC13 為數位輸入，並設定內部上拉 (GPIO_PULLUP) */
    GPIO_InitStruct.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

/**
 * @brief 按鈕檢測與處理函式 (包含 20ms 去彈跳與放開等待)
 */
void Process_Buttons(void)
{
    /* 檢測 Button 1 (PC0): 循環切換 4 種天氣模式 */
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == GPIO_PIN_RESET)
    {
        HAL_Delay(20); // 20ms 軟體去彈跳
        if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == GPIO_PIN_RESET)
        {
            /* 循環切換天氣模式 (0 -> 1 -> 2 -> 3 -> 0) */
            currentWeather = (WeatherState_t)((currentWeather + 1) % WEATHER_MAX);

            /* 透過 UART 提示當前切換的天氣模式 */
            switch (currentWeather)
            {
                case WEATHER_SUNNY:  Send_UART_String("Weather: Sunny (1)\r\n"); break;
                case WEATHER_CLOUDY: Send_UART_String("Weather: Cloudy (2)\r\n"); break;
                case WEATHER_RAINY:  Send_UART_String("Weather: Rainy (3)\r\n"); break;
                case WEATHER_STORMY: Send_UART_String("Weather: Stormy (4)\r\n"); break;
                default: break;
            }

            /* 等待按鈕放開，避免連續觸發 */
            while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0) == GPIO_PIN_RESET)
            {
                HAL_Delay(10);
            }
        }
    }

    /* 檢測 Button 2 (PC1): 切換至戰術交戰 (COMBAT) 狀態 */
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1) == GPIO_PIN_RESET)
    {
        HAL_Delay(20); // 20ms 軟體去彈跳
        if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1) == GPIO_PIN_RESET)
        {
            currentSystemState = SYS_STATE_COMBAT; // 設定系統狀態為戰術交戰
            Send_UART_String("System State: COMBAT MODE\r\n"); // UART 提示

            /* 等待按鈕放開 */
            while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1) == GPIO_PIN_RESET)
            {
                HAL_Delay(10);
            }
        }
    }

    /* 檢測 Button 3 (PC13): 重置系統回到正常巡航 (NORMAL) 狀態 */
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET)
    {
        HAL_Delay(20); // 20ms 軟體去彈跳
        if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET)
        {
            currentSystemState = SYS_STATE_NORMAL; // 重置系統狀態為正常巡航
            Send_UART_String("System State: NORMAL (Reset)\r\n"); // UART 提示

            /* 等待按鈕放開 */
            while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET)
            {
                HAL_Delay(10);
            }
        }
    }
}

/**
 * @brief 更新 LED、呼吸燈與蜂鳴器輸出（基於 HAL_GetTick() 非阻塞控制）
 */
void Update_LEDs_And_Buzzer(void)
{
    uint32_t currentTick = HAL_GetTick(); // 取得當前系統時間戳記

    /* 根據系統狀態進行對應的輸出控制 */
    if (currentSystemState == SYS_STATE_COMBAT)
    {
        /* --- 狀態：戰術交戰 (COMBAT) --- */
        /* 綠燈(PC2)、黃燈(PA5)、白/藍 PWM 燈光強制熄滅 */
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_RESET); // 綠燈熄滅
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET); // 黃燈熄滅
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);      // 白燈 PWM 歸零
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0);      // 藍燈 PWM 歸零

        /* 紅燈(PC3) 與 蜂鳴器(PC4) 同步以 100ms 間隔高速爆閃與鳴叫 */
        if (currentTick - lastCombatFlashTick >= 100)
        {
            lastCombatFlashTick = currentTick; // 更新計時基準
            combatState = (combatState == GPIO_PIN_RESET) ? GPIO_PIN_SET : GPIO_PIN_RESET; // 切換狀態
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_3, combatState); // 紅燈同步切換
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, combatState); // 蜂鳴器同步切換
        }
    }
    else
    {
        /* --- 狀態：正常巡航 (NORMAL) --- */
        /* 綠燈(PC2) 恆亮，紅燈(PC3) 與 蜂鳴器(PC4) 保持靜音與熄滅 */
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);   // 綠燈恆亮
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_3, GPIO_PIN_RESET); // 紅燈熄滅
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_RESET); // 蜂鳴器靜音

        /* ---------------- 天氣系統邏輯 (白燈通用處理) ---------------- */
        /* 白燈：2 秒一個週期的呼吸燈 (10ms 更新一次 CCR，步長 10) */
        if (currentTick - lastWhiteBreathTick >= 10)
        {
            lastWhiteBreathTick = currentTick;
            whiteBrightness += whiteStep;
            if (whiteBrightness >= 999) { whiteBrightness = 999; whiteStep = -10; }
            else if (whiteBrightness <= 0) { whiteBrightness = 0; whiteStep = 10; }
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, (uint32_t)whiteBrightness);
        }

        /* ---------------- 各天氣模式的藍燈與黃燈控制 ---------------- */
        switch (currentWeather)
        {
            case WEATHER_SUNNY:
                /* 藍燈熄滅，黃燈熄滅 */
                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0);
                HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
                break;

            case WEATHER_CLOUDY:
                /* 藍燈：1 秒一個週期的呼吸燈 (10ms 更新一次 CCR，步長 20)；黃燈熄滅 */
                if (currentTick - lastBlueBreathTick >= 10)
                {
                    lastBlueBreathTick = currentTick;
                    blueBrightness += blueStep;
                    if (blueBrightness >= 999) { blueBrightness = 999; blueStep = -20; }
                    else if (blueBrightness <= 0) { blueBrightness = 0; blueStep = 20; }
                    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, (uint32_t)blueBrightness);
                }
                HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
                break;

            case WEATHER_RAINY:
                /* 藍燈熄滅；黃燈(PA5) 每 1000ms 閃爍一次 */
                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0);
                if (currentTick - lastYellowFlashTick >= 1000)
                {
                    lastYellowFlashTick = currentTick;
                    yellowLedState = (yellowLedState == GPIO_PIN_RESET) ? GPIO_PIN_SET : GPIO_PIN_RESET;
                    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, yellowLedState);
                }
                break;

            case WEATHER_STORMY:
                /* 藍燈：1 秒週期呼吸燈 */
                if (currentTick - lastBlueBreathTick >= 10)
                {
                    lastBlueBreathTick = currentTick;
                    blueBrightness += blueStep;
                    if (blueBrightness >= 999) { blueBrightness = 999; blueStep = -20; }
                    else if (blueBrightness <= 0) { blueBrightness = 0; blueStep = 20; }
                    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, (uint32_t)blueBrightness);
                }
                /* 黃燈(PA5) 每 500ms 高速閃爍一次 */
                if (currentTick - lastYellowFlashTick >= 500)
                {
                    lastYellowFlashTick = currentTick;
                    yellowLedState = (yellowLedState == GPIO_PIN_RESET) ? GPIO_PIN_SET : GPIO_PIN_RESET;
                    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, yellowLedState);
                }
                break;

            default:
                break;
        }
    }
}

/**
 * @brief UART 接收中斷回呼函式 (將收到的資料寫入環形緩衝區)
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        uint16_t nextHead = (uartRxRingBuffer.head + 1) % RING_BUFFER_SIZE;

        /* 若緩衝區未滿，則將字元寫入 */
        if (nextHead != uartRxRingBuffer.tail)
        {
            uartRxRingBuffer.buffer[uartRxRingBuffer.head] = uartRxSingleByte;
            uartRxRingBuffer.head = nextHead;
        }

        /* 重新啟動下一個位元組的中斷接收 */
        HAL_UART_Receive_IT(&huart2, &uartRxSingleByte, 1);
    }
}

/**
 * @brief 非阻塞輪詢環形緩衝區並解析指令
 */
void Poll_UART_RingBuffer(void)
{
    static char cmdLine[32];
    static uint8_t cmdIndex = 0;

    /* 當 head 與 tail 不相等時代表緩衝區有新資料 */
    while (uartRxRingBuffer.tail != uartRxRingBuffer.head)
    {
        uint8_t c = uartRxRingBuffer.buffer[uartRxRingBuffer.tail];
        uartRxRingBuffer.tail = (uartRxRingBuffer.tail + 1) % RING_BUFFER_SIZE;

        /* 檢測換行字元 (\r 或 \n) 作為完整指令的結尾 */
        if (c == '\r' || c == '\n')
        {
            if (cmdIndex > 0)
            {
                cmdLine[cmdIndex] = '\0'; // 加上字串結束字元
                Execute_Command(cmdLine);  // 執行指令解析
                cmdIndex = 0;              // 重設指令索引
            }
        }
        else
        {
            /* 收集字元至暫存陣列，預防緩衝區溢位 */
            if (cmdIndex < sizeof(cmdLine) - 1)
            {
                cmdLine[cmdIndex++] = (char)c;
            }
            else
            {
                cmdIndex = 0; // 溢位防護重置
            }
        }
    }
}

/**
 * @brief 執行解析接收到的命令字串
 * @param cmd 傳入的命令字串
 */
void Execute_Command(char *cmd)
{
    /* 比對指令內容 (支援文字與數字代號) */
    if (strcmp(cmd, "sunny") == 0 || strcmp(cmd, "1") == 0)
    {
        currentWeather = WEATHER_SUNNY;
        Send_UART_String("CMD OK: Sunny Mode\r\n");
    }
    else if (strcmp(cmd, "cloudy") == 0 || strcmp(cmd, "2") == 0)
    {
        currentWeather = WEATHER_CLOUDY;
        Send_UART_String("CMD OK: Cloudy Mode\r\n");
    }
    else if (strcmp(cmd, "rainy") == 0 || strcmp(cmd, "3") == 0)
    {
        currentWeather = WEATHER_RAINY;
        Send_UART_String("CMD OK: Rainy Mode\r\n");
    }
    else if (strcmp(cmd, "stormy") == 0 || strcmp(cmd, "4") == 0)
    {
        currentWeather = WEATHER_STORMY;
        Send_UART_String("CMD OK: Stormy Mode\r\n");
    }
    else if (strcmp(cmd, "combat") == 0 || strcmp(cmd, "5") == 0)
    {
        currentSystemState = SYS_STATE_COMBAT;
        Send_UART_String("CMD OK: Combat Mode Activated\r\n");
    }
    else if (strcmp(cmd, "reset") == 0 || strcmp(cmd, "6") == 0)
    {
        currentSystemState = SYS_STATE_NORMAL;
        Send_UART_String("CMD OK: System Reset to Normal\r\n");
    }
    else
    {
        Send_UART_String("Unknown Command\r\n");
    }
}

/**
 * @brief 透過 UART 發送字串的輔助函式
 */
void Send_UART_String(const char *str)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)str, (uint16_t)strlen(str), HAL_MAX_DELAY);
}

/**
 * @brief 錯誤處理函式
 */
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
        /* 發生致命錯誤時在此無限迴圈 */
    }
}
