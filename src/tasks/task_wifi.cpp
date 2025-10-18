#include "task_wifi.h"
#include "app_globals.h"
#include "storage/storage_manager.h" // 引入storage_manager头文件
#include <WiFi.h>
#include <DNSServer.h>
#include <time.h>

// ==========================================================================
// == 任务内部使用的静态变量和对象 ==
// ==========================================================================
static DNSServer dnsServer;

// ==========================================================================
// == 内部函数声明 ==
// ==========================================================================
void wifi_task_code(void *pvParameters);
void attempt_ntp_sync();
void process_wifi_connection();

// ==========================================================================
// == 任务创建函数 ==
// ==========================================================================
void create_wifi_task() {
    xTaskCreatePinnedToCore(
        wifi_task_code,
        "WifiTask",
        TASK_WIFI_STACK_SIZE,
        NULL,
        TASK_WIFI_PRIO,
        NULL,
        1 // 核心1
    );
}

// ==========================================================================
// == WiFi任务主代码 ==
// ==========================================================================
void wifi_task_code(void *pvParameters) {
    P_PRINTLN("[TASK_WIFI] 任务启动。");

    WiFi.mode(WIFI_AP_STA);
    P_PRINTLN("[TASK_WIFI] 设置为AP+STA模式。");
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONNECTIONS);
    IPAddress apIP = WiFi.softAPIP();
    P_PRINTF("[TASK_WIFI] AP模式已启动. SSID: %s, IP: %s\n", WIFI_AP_SSID, apIP.toString().c_str());

    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", apIP);
    P_PRINTLN("[TASK_WIFI] Captive Portal DNS服务器已启动。");

    // 尝试自动连接
    xSemaphoreTake(g_configMutex, portMAX_DELAY);
    String ssid = g_currentConfig.savedSsid;
    String pass = g_currentConfig.savedPassword;
    xSemaphoreGive(g_configMutex);
    
    if (ssid.length() > 0) {
        xSemaphoreTake(g_configMutex, portMAX_DELAY); // 使用configMutex暂时保护g_wifiStatus
        g_wifiStatus.ssidToTry = ssid;
        g_wifiStatus.passwordToTry = pass;
        g_wifiStatus.connectProgress = WIFI_CP_CONNECTING;
        g_wifiStatus.connectAttemptStartTime = millis();
        xSemaphoreGive(g_configMutex);
        WiFi.begin(ssid.c_str(), pass.c_str());
        P_PRINTF("[TASK_WIFI] 尝试自动连接到: %s\n", ssid.c_str());
    }

    unsigned long lastNtpSyncTime = 0;

    for (;;) {
        dnsServer.processNextRequest();
        process_wifi_connection();
        
        bool isConnected = (WiFi.status() == WL_CONNECTED);
        EventBits_t currentBits = xEventGroupGetBits(g_networkStatusEventGroup);

        if (isConnected && !(currentBits & WIFI_CONNECTED_BIT)) {
            xEventGroupSetBits(g_networkStatusEventGroup, WIFI_CONNECTED_BIT);
            P_PRINTLN("[TASK_WIFI] WiFi已连接，设置事件标志位。");
        } else if (!isConnected && (currentBits & WIFI_CONNECTED_BIT)) {
            xEventGroupClearBits(g_networkStatusEventGroup, WIFI_CONNECTED_BIT | NTP_SYNCED_BIT);
            P_PRINTLN("[TASK_WIFI] WiFi已断开，清除事件标志位。");
        }

        // 处理NTP同步
        if (isConnected && !(currentBits & NTP_SYNCED_BIT)) {
            attempt_ntp_sync();
        } else if (isConnected && (millis() - lastNtpSyncTime > NTP_SYNC_INTERVAL_MS)) {
            attempt_ntp_sync();
            lastNtpSyncTime = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // 任务循环延时
    }
}

// ==========================================================================
// == 任务内部实现函数 ==
// ==========================================================================
void attempt_ntp_sync() {
    static int ntp_attempts = 0;
    const int MAX_NTP_ATTEMPTS = 5;

    if (ntp_attempts >= MAX_NTP_ATTEMPTS) {
        return; // 放弃尝试
    }

    P_PRINTF("[TASK_WIFI] 尝试NTP同步 (次数 %d/%d)...\n", ntp_attempts + 1, MAX_NTP_ATTEMPTS);
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5000)) {
        P_PRINTLN("[TASK_WIFI] NTP同步失败。");
        ntp_attempts++;
    } else {
        P_PRINTLN("[TASK_WIFI] NTP同步成功!");
        xEventGroupSetBits(g_networkStatusEventGroup, NTP_SYNCED_BIT);
        ntp_attempts = MAX_NTP_ATTEMPTS; // 成功后不再尝试
    }
}

void process_wifi_connection() {
    WifiStatus status_snapshot;
    xSemaphoreTake(g_configMutex, portMAX_DELAY);
    status_snapshot = g_wifiStatus;
    xSemaphoreGive(g_configMutex);

    if (status_snapshot.connectProgress == WIFI_CP_IDLE || status_snapshot.connectProgress == WIFI_CP_FAILED) {
        // 如果WiFi断开，则启动重连机制
        if (WiFi.status() != WL_CONNECTED) {
            static unsigned long lastReconnectAttempt = 0;
            if (millis() - lastReconnectAttempt > WIFI_RECONNECT_DELAY_MS) {
                lastReconnectAttempt = millis();
                xSemaphoreTake(g_configMutex, portMAX_DELAY);
                if (g_currentConfig.savedSsid.length() > 0) {
                     P_PRINTF("[TASK_WIFI] WiFi已断开, 尝试重新连接到 %s...\n", g_currentConfig.savedSsid.c_str());
                     WiFi.begin(g_currentConfig.savedSsid.c_str(), g_currentConfig.savedPassword.c_str());
                }
                xSemaphoreGive(g_configMutex);
            }
        }
        return;
    }
    
    // 处理手动连接流程
    if (status_snapshot.connectProgress == WIFI_CP_CONNECTING) {
        wl_status_t status = WiFi.status();
        if (status == WL_CONNECTED) {
            P_PRINTF("[TASK_WIFI] 手动连接成功: %s\n", status_snapshot.ssidToTry.c_str());
            xSemaphoreTake(g_configMutex, portMAX_DELAY);
            g_currentConfig.savedSsid = status_snapshot.ssidToTry;
            g_currentConfig.savedPassword = status_snapshot.passwordToTry;
            g_wifiStatus.connectProgress = WIFI_CP_IDLE;
            xSemaphoreGive(g_configMutex);
            save_config();
        } else if (millis() - status_snapshot.connectAttemptStartTime > 20000) { // 20秒超时
            P_PRINTF("[TASK_WIFI] 手动连接超时: %s\n", status_snapshot.ssidToTry.c_str());
            WiFi.disconnect(true);
            xSemaphoreTake(g_configMutex, portMAX_DELAY);
            g_wifiStatus.connectProgress = WIFI_CP_FAILED;
            xSemaphoreGive(g_configMutex);
        }
    }
}

