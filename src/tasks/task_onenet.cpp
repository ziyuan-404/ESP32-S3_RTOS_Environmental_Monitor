#include "task_onenet.h"
#include "app_globals.h"
#include <WiFi.h>
#include <PubSubClient.h>

// ==========================================================================
// == 任务内部使用的静态变量和对象 ==
// ==========================================================================
static WiFiClient espClient;
static PubSubClient mqttClient(espClient);
static unsigned long postMsgId = 0;

// ==========================================================================
// == 内部函数声明 ==
// ==========================================================================
void onenet_task_code(void *pvParameters);
void mqtt_callback(char* topic, byte* payload, unsigned int length);
bool connect_to_onenet();
void post_properties();

// ==========================================================================
// == 任务创建函数 ==
// ==========================================================================
void create_onenet_task() {
    xTaskCreatePinnedToCore(
        onenet_task_code,
        "OneNetTask",
        TASK_ONENET_STACK_SIZE,
        NULL,
        TASK_ONENET_PRIO,
        NULL,
        1 // 核心1
    );
}

// ==========================================================================
// == OneNET任务主代码 ==
// ==========================================================================
void onenet_task_code(void *pvParameters) {
    P_PRINTLN("[TASK_ONENET] 任务启动。");
    mqttClient.setServer(ONENET_MQTT_SERVER, ONENET_MQTT_PORT);
    mqttClient.setCallback(mqtt_callback);
    mqttClient.setBufferSize(2048);

    for (;;) {
        // 1. 等待 WiFi 连接成功
        P_PRINTLN("[TASK_ONENET] 等待WiFi连接...");
        xEventGroupWaitBits(g_networkStatusEventGroup, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        P_PRINTLN("[TASK_ONENET] WiFi已连接，开始处理MQTT。");

        // 2. WiFi连接后，管理MQTT连接和数据上报
        unsigned long lastPostTime = 0;
        while ((xEventGroupGetBits(g_networkStatusEventGroup) & WIFI_CONNECTED_BIT) != 0) {
            if (!mqttClient.connected()) {
                connect_to_onenet();
            }

            mqttClient.loop();

            if (millis() - lastPostTime >= ONENET_POST_INTERVAL_MS) {
                lastPostTime = millis();
                if (mqttClient.connected()) {
                    post_properties();
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100)); // 短暂延时
        }
        
        // WiFi断开，断开MQTT
        if (mqttClient.connected()) {
            mqttClient.disconnect();
            P_PRINTLN("[TASK_ONENET] WiFi断开，已断开MQTT连接。");
        }
    }
}

// ==========================================================================
// == 任务内部实现函数 ==
// ==========================================================================
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    P_PRINTF("[TASK_ONENET] 收到MQTT消息, 主题: %s\n", topic);
    // 在这里添加处理云端指令的逻辑
}

bool connect_to_onenet() {
    static unsigned long lastConnectAttempt = 0;
    if (millis() - lastConnectAttempt < 5000) {
        return false; // 5秒内只尝试一次
    }
    lastConnectAttempt = millis();

    P_PRINTLN("[TASK_ONENET] 正在连接到MQTT服务器...");
    if (mqttClient.connect(ONENET_DEVICE_ID, ONENET_PRODUCT_ID, ONENET_TOKEN)) {
        P_PRINTLN("[TASK_ONENET] MQTT连接成功!");
        mqttClient.subscribe(ONENET_TOPIC_PROPERTY_SET);
        mqttClient.subscribe(ONENET_TOPIC_PROPERTY_POST_REPLY);
        return true;
    } else {
        P_PRINTF("[TASK_ONENET] MQTT连接失败, rc=%d\n", mqttClient.state());
        return false;
    }
}

void post_properties() {
    JsonDocument postDoc;
    DeviceState currentStateSnapshot;

    // 从全局状态安全地读取数据
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        currentStateSnapshot = g_currentState;
        xSemaphoreGive(g_stateMutex);
    } else {
        P_PRINTLN("[TASK_ONENET] 获取状态锁超时，跳过上报。");
        return;
    }

    if (currentStateSnapshot.tempStatus == SS_INIT || currentStateSnapshot.tempStatus == SS_DISCONNECTED) {
        P_PRINTLN("[TASK_ONENET] 传感器数据未就绪，跳过上报。");
        return;
    }

    postDoc["id"] = String(postMsgId++);
    postDoc["version"] = "1.0";
    JsonObject params = postDoc["params"].to<JsonObject>();
    
    params["temp_value"]["value"] = currentStateSnapshot.temperature;
    params["humidity_value"]["value"] = (int)currentStateSnapshot.humidity;

    if (!isnan(currentStateSnapshot.gasPpmValues.co)) params["CO_ppm"]["value"] = round(currentStateSnapshot.gasPpmValues.co * 100) / 100.0;
    if (!isnan(currentStateSnapshot.gasPpmValues.no2)) params["NO2_ppm"]["value"] = round(currentStateSnapshot.gasPpmValues.no2 * 100) / 100.0;
    if (!isnan(currentStateSnapshot.gasPpmValues.c2h5oh)) params["C2H5OH_ppm"]["value"] = round(currentStateSnapshot.gasPpmValues.c2h5oh * 10) / 10.0;
    if (!isnan(currentStateSnapshot.gasPpmValues.voc)) params["VOC_ppm"]["value"] = round(currentStateSnapshot.gasPpmValues.voc * 100) / 100.0;

    String postData;
    serializeJson(postDoc, postData);
    
    P_PRINTLN("[TASK_ONENET] 准备上报数据到OneNET...");
    if (mqttClient.publish(ONENET_TOPIC_PROPERTY_POST, postData.c_str())) {
        P_PRINTLN("[TASK_ONENET] 属性上报成功.");
    } else {
        P_PRINTLN("[TASK_ONENET] ***错误*** 属性上报失败!");
    }
}

