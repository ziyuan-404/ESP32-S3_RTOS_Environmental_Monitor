#include "task_system_control.h"
#include "app_globals.h"
#include <Adafruit_NeoPixel.h>

// ==========================================================================
// == 任务内部使用的静态变量和对象 ==
// ==========================================================================
static Adafruit_NeoPixel pixels(NEOPIXEL_NUM, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
static uint32_t COLOR_GREEN, COLOR_RED, COLOR_BLUE, COLOR_YELLOW, COLOR_ORANGE, COLOR_CYAN, COLOR_OFF;
static bool ledBlinkState = false;
static unsigned long lastBlinkTime = 0;

// ==========================================================================
// == 内部函数声明 ==
// ==========================================================================
void system_control_task_code(void *pvParameters);
void update_led_brightness(uint8_t brightness_percent);
void update_led_status();
void control_buzzer(bool alarm_active);

// ==========================================================================
// == 任务创建和初始化函数 ==
// ==========================================================================
void init_system_hardware() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    
    pixels.begin();
    pixels.setBrightness(map(DEFAULT_LED_BRIGHTNESS, 0, 100, 0, 255));
    pixels.clear();
    pixels.show();

    // 初始化颜色常量
    COLOR_GREEN  = pixels.Color(0, 120, 0);
    COLOR_RED    = pixels.Color(120, 0, 0);
    COLOR_BLUE   = pixels.Color(0, 0, 120);
    COLOR_YELLOW = pixels.Color(120, 120, 0);
    COLOR_ORANGE = pixels.Color(255, 100, 0);
    COLOR_CYAN   = pixels.Color(0, 255, 255);
    COLOR_OFF    = pixels.Color(0, 0, 0);

    // 初始状态灯为蓝色，表示正在启动
    pixels.setPixelColor(0, COLOR_BLUE);
    pixels.show();
}

void create_system_control_task() {
    xTaskCreatePinnedToCore(
        system_control_task_code,
        "SystemCtrlTask",
        TASK_SYSTEM_CONTROL_STACK_SIZE,
        NULL,
        TASK_SYSTEM_CONTROL_PRIO,
        NULL,
        1 // 核心1
    );
}


// ==========================================================================
// == 系统控制任务主代码 ==
// ==========================================================================
void system_control_task_code(void *pvParameters) {
    P_PRINTLN("[TASK_SYS_CTRL] 任务启动。");
    SystemControlMessage msg;
    bool alarmActive = false;
    
    for (;;) {
        // 1. 等待来自其他任务的命令，或超时后主动更新状态
        if (xQueueReceive(g_systemControlQueue, &msg, pdMS_TO_TICKS(250))) {
            switch (msg.command) {
                case CMD_UPDATE_STATUS:
                    // 由超时触发，这里不需要做什么
                    break;
                case CMD_SET_BRIGHTNESS:
                    update_led_brightness(msg.value);
                    break;
                case CMD_ALARM_ON:
                    alarmActive = true;
                    break;
                case CMD_ALARM_OFF:
                    alarmActive = false;
                    break;
            }
        }

        // 2. 无论是否有命令，都周期性地更新LED和蜂鸣器
        update_led_status();
        control_buzzer(alarmActive);
    }
}

// ==========================================================================
// == 任务内部实现函数 ==
// ==========================================================================
void update_led_brightness(uint8_t brightness_percent) {
    uint8_t brightness_val = map(brightness_percent, 0, 100, 0, 255);
    pixels.setBrightness(brightness_val);
    // 如果亮度大于0但灯是灭的，则弱弱地点亮一下以显示效果
    if (brightness_val > 0 && pixels.getPixelColor(0) == 0) {
        pixels.setPixelColor(0, pixels.Color(1, 1, 1));
    }
    pixels.show();
}

void update_led_status() {
    unsigned long currentTime = millis();
    uint32_t colorToSet = COLOR_OFF;

    // 获取网络状态和传感器状态的快照
    EventBits_t networkBits = xEventGroupGetBits(g_networkStatusEventGroup);
    bool wifiConnected = (networkBits & WIFI_CONNECTED_BIT) != 0;
    
    DeviceState stateSnapshot;
    if (xSemaphoreTake(g_stateMutex, 0) == pdTRUE) {
        stateSnapshot = g_currentState;
        xSemaphoreGive(g_stateMutex);
    } else {
        // 如果无法立即获取锁，则跳过本次更新
        return;
    }
    WifiStatus wifiSnapshot;
     if (xSemaphoreTake(g_configMutex, 0) == pdTRUE) { // g_wifiStatus is small, using configMutex for simplicity
        wifiSnapshot = g_wifiStatus;
        xSemaphoreGive(g_configMutex);
     } else {
        return;
     }

    bool isAnySensorWarning = stateSnapshot.isAnyAlarmActive;
    bool isAnySensorDisconnected = (stateSnapshot.tempStatus == SS_DISCONNECTED || stateSnapshot.humStatus == SS_DISCONNECTED ||
                                  stateSnapshot.gasCoStatus == SS_DISCONNECTED || stateSnapshot.gasNo2Status == SS_DISCONNECTED ||
                                  stateSnapshot.gasC2h5ohStatus == SS_DISCONNECTED || stateSnapshot.gasVocStatus == SS_DISCONNECTED);
    bool isAnySensorInitializing = (stateSnapshot.gasCoStatus == SS_INIT);

    const unsigned long BLINK_INTERVAL = 500;
    if (currentTime - lastBlinkTime >= BLINK_INTERVAL) {
        lastBlinkTime = currentTime;
        ledBlinkState = !ledBlinkState;
    }

    if (stateSnapshot.calibrationState == CAL_IN_PROGRESS) {
        colorToSet = ledBlinkState ? COLOR_CYAN : pixels.Color(0, 50, 50);
    } else if (isAnySensorWarning) {
        colorToSet = ledBlinkState ? COLOR_RED : COLOR_OFF;
    } else if (isAnySensorInitializing) {
        colorToSet = ledBlinkState ? COLOR_ORANGE : pixels.Color(100, 60, 0);
    } else if (isAnySensorDisconnected) {
        colorToSet = ledBlinkState ? COLOR_BLUE : COLOR_OFF;
    } else if (wifiSnapshot.isScanning || wifiSnapshot.connectProgress == WIFI_CP_CONNECTING) {
        colorToSet = ledBlinkState ? COLOR_BLUE : pixels.Color(0, 0, 50);
    } else if (!wifiConnected) {
        colorToSet = COLOR_YELLOW;
    } else {
        colorToSet = COLOR_GREEN;
    }

    if (pixels.getPixelColor(0) != colorToSet) {
        pixels.setPixelColor(0, colorToSet);
        pixels.show();
    }
}

void control_buzzer(bool alarm_active) {
    #define BUZZER_ALARM_DURATION 150
    #define BUZZER_ALARM_INTERVAL 200
    #define BUZZER_ALARM_CYCLE (BUZZER_ALARM_DURATION + BUZZER_ALARM_INTERVAL)
    #define BUZZER_TOTAL_CYCLE_TIME (3000) // 每3秒响一次

    static unsigned long lastCycleStartTime = 0;
    unsigned long currentTime = millis();

    if (alarm_active) {
        if (currentTime - lastCycleStartTime > BUZZER_TOTAL_CYCLE_TIME) {
            lastCycleStartTime = currentTime;
        }
        unsigned long timeInCycle = currentTime - lastCycleStartTime;
        if (timeInCycle < BUZZER_ALARM_DURATION) {
             digitalWrite(BUZZER_PIN, HIGH);
        } else {
             digitalWrite(BUZZER_PIN, LOW);
        }
    } else {
        digitalWrite(BUZZER_PIN, LOW);
        lastCycleStartTime = 0; // 重置周期
    }
}
