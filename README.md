# STM32 HAL 綜合巡航與天氣控制系統 (STM32 Navigation & Weather Control FSM)

基於 **STM32 HAL 庫** 開發的嵌入式綜合控制系統。本專案結合了 **有限狀態機 (FSM)**、**PWM 呼吸燈**、**非阻塞式時間管理 (`HAL_GetTick`)**、**硬體按鈕去彈跳** 以及 **UART 環形緩衝區 (Ring Buffer) 指令解析**，適用於智慧載具、情境面板或嵌入式教學示範。

---

## 📌 系統功能亮點

* **雙層狀態機 (FSM)**：劃分為主系統狀態（正常巡航/戰術交戰）與子天氣模式。
* **PWM 呼吸燈與定時閃爍**：運用 TIM3 產生 1kHz PWM，搭配 `HAL_GetTick()` 實現完全非阻塞的呼吸燈與閃爍效果，拒絕使用耗時的 `HAL_Delay()`。
* **高效 UART 環形緩衝區 (Ring Buffer)**：中斷服務常式 (ISR) 僅負責接收並寫入緩衝區，主迴圈非阻塞輪詢與解析，確保系統即時性。
* **強健的按鈕防護**：所有按鈕皆配置內部上拉 (`GPIO_PULLUP`)、20ms 軟體消抖與放開檢測機制。

---

## 🔌 硬體腳位與周邊配置

| 周邊類型 | 腳位 (Pin) | 硬體元件 | 功能描述 |
| :--- | :--- | :--- | :--- |
| **TIM3 CH1** | - | 白色 LED | 呼吸燈 (預設 Pulse = 160) |
| **TIM3 CH2** | - | 藍色 LED | 呼吸燈 (預設 Pulse = 400) |
| **GPIO Output** | `PC2` | 綠色 LED | 正常巡航狀態指示燈 (恆亮) |
| **GPIO Output** | `PC3` | 紅色 LED | 警報爆閃燈 (戰術交戰時 100ms 閃爍) |
| **GPIO Output** | `PA5` | 黃色 LED | 天氣狀態燈 (依天氣模式不同頻率閃爍) |
| **GPIO Output** | `PC4` | 蜂鳴器 (Buzzer) | 戰術交戰警報鳴叫 (與紅燈同步) |
| **GPIO Input** | `PC0` | Button 1 | 循環切換 4 種天氣模式 (含內部上拉) |
| **GPIO Input** | `PC1` | Button 2 | 切換至戰術交戰 (COMBAT) 狀態 (含內部上拉) |
| **GPIO Input** | `PC13`| Button 3 | 重置系統回正常巡航 (NORMAL) 狀態 (含內部上拉) |
| **USART2** | `PA2` (TX)<br>`PA3` (RX) | 序列埠通訊 | 115200 波特率, 8 資料位元, 無奇偶校驗, 1 停止位 (8N1) |

---

## 🧬 系統架構與狀態機 (FSM)

### 一、 主系統狀態 (`SystemState_t`)
1. **`SYS_STATE_NORMAL` (正常巡航)**：
   * 綠燈 (`PC2`) 恆亮，紅燈 (`PC3`) 與蜂鳴器 (`PC4`) 保持熄滅與靜音。
   * 運行內部的「天氣系統」邏輯。
2. **`SYS_STATE_COMBAT` (戰術交戰)**：
   * 綠燈、黃燈、白/藍 PWM 燈光全部強制熄滅。
   * 紅燈 (`PC3`) 與蜂鳴器 (`PC4`) 同步以 **100ms** 為間隔高速爆閃與鳴叫。

### 二、 天氣子模式 (`WeatherState_t`)
* **晴空萬里 (`WEATHER_SUNNY`)**：白燈 2 秒週期呼吸燈；藍燈與黃燈熄滅。
* **藍天白雲 (`WEATHER_CLOUDY`)**：白燈 2 秒週期呼吸燈；藍燈 1 秒週期呼吸燈；黃燈熄滅。
* **下雨 (`WEATHER_RAINY`)**：白燈 2 秒週期呼吸燈；藍燈熄滅；黃燈每 1000ms 閃爍一次。
* **狂風暴雨 (`WEATHER_STORMY`)**：白燈 2 秒週期呼吸燈；藍燈 1 秒週期呼吸燈；黃燈每 500ms 高速閃爍一次。

---

## 💻 UART 指令控制台

系統開機後會透過 UART 發送 `Commander login\r\n`。您可以使用序列埠終端機（如 Tera Term、PuTTY、VS Code Serial Monitor）輸入以下指令來控制系統（支援英文字串或數字代號，大小寫不拘，需含換行符號）：

| 指令 (文字) | 指令 (數字) | 動作說明 |
| :--- | :--- | :--- |
| `sunny` | `1` | 切換至「晴空萬里」天氣模式 |
| `cloudy` | `2` | 切換至「藍天白雲」天氣模式 |
| `rainy` | `3` | 切換至「下雨」天氣模式 |
| `stormy` | `4` | 切換至「狂風暴雨」天氣模式 |
| `combat` | `5` | 切換至「戰術交戰 (COMBAT)」模式 |
| `reset` | `6` | 重置系統回「正常巡航 (NORMAL)」狀態 |

---

## 🛠️ 軟體架構亮點說明

### 1. 零阻塞時間管理
程式捨棄了會卡死 CPU 的 `HAL_Delay()`，改用內建的 `HAL_GetTick()` 抓取系統毫秒級的時間戳記：
```c
uint32_t currentTick = HAL_GetTick();
if (currentTick - lastWhiteBreathTick >= 10) {
    lastWhiteBreathTick = currentTick;
    // 更新呼吸燈 PWM 數值...
}# micro-computer-exam-project
