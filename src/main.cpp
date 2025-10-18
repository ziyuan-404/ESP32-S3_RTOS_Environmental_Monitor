/**
 * =================================================================================
 * == ESP32 环境监测器 V6 (FreeRTOS 重构版) - 主文件 (main.cpp) ==
 *
 * 职责:
 * - 程序入口。
 * - 初始化所有硬件、服务和全局资源 (互斥锁, 事件组, 队列)。
 * - 创建并启动所有独立的 FreeRTOS 任务。
 * - 启动 FreeRTOS 调度器 (通过返回空的 loop() 实现)。
 *
 * =================================================================================
 */
#include <Arduino.h>
#include "config.h"
#include "app_globals.h"
#include "storage/storage_manager.h"
#include "tasks/task_sensor.h"
#include "tasks/task_wifi.h"
#include "tasks/task_web_server.h"
#include "tasks/task_onenet.h"
#include "tasks/task_system_control.h"

void setup() {
    // 启动串行通信用于调试
    Serial.begin(115200);
    Serial.println("\n[SETUP] 系统启动 (FreeRTOS 重构版)...");

    // 1. 初始化全局变量和 FreeRTOS 同步对象 (必须在所有任务创建前完成)
    init_globals();
    Serial.println("[SETUP] 全局变量和同步对象初始化完毕。");

    // 2. 初始化硬件 (LED, 蜂鸣器等)
    // 注意: 传感器相关的硬件初始化已移至 sensor_task 内部
    init_system_hardware();
    Serial.println("[SETUP] 系统控制硬件 (LED, 蜂鸣器) 初始化完毕。");

    // 3. 初始化并挂载文件系统
    init_spiffs();
    Serial.println("[SETUP] SPIFFS 文件系统初始化完毕。");

    // 4. 从 SPIFFS 加载配置和历史数据
    // 加载操作现在受互斥锁保护
    load_config();
    load_historical_data();
    Serial.println("[SETUP] 配置和历史数据加载完毕。");

    // 5. 根据加载的配置应用初始状态
    // (例如，设置LED亮度)
    SystemControlMessage led_msg;
    led_msg.command = CMD_SET_BRIGHTNESS;
    
    // 安全地读取配置
    xSemaphoreTake(g_configMutex, portMAX_DELAY);
    led_msg.value = g_currentConfig.ledBrightness;
    xSemaphoreGive(g_configMutex);

    xQueueSend(g_systemControlQueue, &led_msg, 0);
    Serial.printf("[SETUP] 应用初始LED亮度: %d%%\n", led_msg.value);


    // 6. 创建所有应用程序任务
    Serial.println("[SETUP] 开始创建所有应用任务...");
    create_wifi_task();
    create_web_server_task();
    create_sensor_task();
    create_system_control_task();
    create_onenet_task();
    Serial.println("[SETUP] 所有应用任务创建成功。");

    // 7. setup() 结束, FreeRTOS 调度器将接管 CPU 控制
    Serial.println("[SETUP] 初始化完成，FreeRTOS 调度器已启动。");
}

void loop() {
    // Arduino loop() 在 FreeRTOS 架构下保持为空。
    // 所有功能都在各自的任务中执行。
    // 主动让出CPU，防止 loopTask 占用过多资源。
    vTaskDelay(portMAX_DELAY);
}

