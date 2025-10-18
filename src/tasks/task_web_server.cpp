#include "task_web_server.h"
#include "app_globals.h"
#include "tasks/task_sensor.h" // For start_calibration_from_isr
#include "storage/storage_manager.h"
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <map>
#include <functional>

// ==========================================================================
// == 任务内部使用的静态变量和对象 ==
// ==========================================================================
static AsyncWebServer server(80);
static WebSocketsServer webSocket(81);
static std::map<String, std::function<void(uint8_t, const JsonDocument&, JsonDocument&)>> wsActionHandlers;

// ==========================================================================
// == 内部函数声明 ==
// ==========================================================================
void web_server_task_code(void *pvParameters);
void configure_web_server_routes();
void setup_websocket_actions();
void on_websocket_event(uint8_t clientNum, WStype_t type, uint8_t * payload, size_t length);
void handle_websocket_message(uint8_t clientNum, const JsonDocument& doc, JsonDocument& responseDoc);
void handle_captive_portal(AsyncWebServerRequest* request);

// -- 数据发送函数 --
void send_sensor_data(uint8_t clientNum = 255);
void send_wifi_status(uint8_t clientNum = 255);
void send_historical_data(uint8_t clientNum);
void send_current_settings(uint8_t clientNum);
void send_calibration_status(uint8_t clientNum = 255);

// -- WebSocket Action Handlers --
void handle_get_current_settings(uint8_t, const JsonDocument&, JsonDocument&);
void handle_get_historical_data(uint8_t, const JsonDocument&, JsonDocument&);
void handle_save_thresholds(uint8_t, const JsonDocument&, JsonDocument&);
void handle_save_led_brightness(uint8_t, const JsonDocument&, JsonDocument&);
void handle_scan_wifi(uint8_t, const JsonDocument&, JsonDocument&);
void handle_connect_wifi(uint8_t, const JsonDocument&, JsonDocument&);
void handle_reset_settings(uint8_t, const JsonDocument&, JsonDocument&);
void handle_start_calibration(uint8_t, const JsonDocument&, JsonDocument&);

// ==========================================================================
// == 任务创建函数 ==
// ==========================================================================
void create_web_server_task() {
    xTaskCreatePinnedToCore(
        web_server_task_code,
        "WebServerTask",
        TASK_WEB_SERVER_STACK_SIZE,
        NULL,
        TASK_WEB_SERVER_PRIO,
        NULL,
        1 // 核心1
    );
}

// ==========================================================================
// == Web服务器任务主代码 ==
// ==========================================================================
void web_server_task_code(void *pvParameters) {
    P_PRINTLN("[TASK_WEB] 任务启动。");

    configure_web_server_routes();
    server.begin();
    P_PRINTLN("[TASK_WEB] HTTP服务器已启动。");

    setup_websocket_actions();
    webSocket.begin();
    webSocket.onEvent(on_websocket_event);
    P_PRINTLN("[TASK_WEB] WebSocket服务器已启动。");
    
    unsigned long lastBroadcastTime = 0;

    for (;;) {
        webSocket.loop();
        
        // 等待传感器数据更新的事件
        EventBits_t bits = xEventGroupWaitBits(g_networkStatusEventGroup, SENSOR_DATA_UPDATED_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));

        // 如果数据更新了，或者到了广播周期，就发送数据
        if ((bits & SENSOR_DATA_UPDATED_BIT) || (millis() - lastBroadcastTime > WEBSOCKET_UPDATE_INTERVAL_MS)) {
            lastBroadcastTime = millis();
            send_sensor_data();
            send_wifi_status();
            send_calibration_status();
        }
        
        // 短暂延时，避免空转
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ==========================================================================
// == 任务内部实现函数 ==
// ==========================================================================
void configure_web_server_routes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(SPIFFS, "/index.html", "text/html"); });
    server.on("/settings.html", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(SPIFFS, "/settings.html", "text/html"); });
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(SPIFFS, "/style.css", "text/css"); });
    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(SPIFFS, "/script.js", "application/javascript"); });
    server.on("/lang.json", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(SPIFFS, "/lang.json", "application/json"); });
    server.on("/chart.min.js", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(SPIFFS, "/chart.min.js", "application/javascript"); });
    
    server.on("/generate_204", HTTP_GET, handle_captive_portal);
    server.onNotFound(handle_captive_portal);
}

void handle_captive_portal(AsyncWebServerRequest *request) {
    request->redirect("/");
}

void on_websocket_event(uint8_t clientNum, WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
        case WStype_DISCONNECTED:
            P_PRINTF("[%u] WS已断开。\n", clientNum);
            break;
        case WStype_CONNECTED: {
            P_PRINTF("[%u] WS已连接。\n", clientNum);
            send_wifi_status(clientNum);
            send_sensor_data(clientNum);
            send_historical_data(clientNum);
            send_current_settings(clientNum);
            send_calibration_status(clientNum);
            break;
        }
        case WStype_TEXT: {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload, length);
            JsonDocument responseDoc;
            if (error) {
                responseDoc["type"] = "error";
                responseDoc["message"] = "Invalid JSON.";
            } else {
                handle_websocket_message(clientNum, doc, responseDoc);
            }
            if (!responseDoc.isNull()) {
                String responseStr;
                serializeJson(responseDoc, responseStr);
                webSocket.sendTXT(clientNum, responseStr);
            }
            break;
        }
        default: break;
    }
}

void setup_websocket_actions() {
    wsActionHandlers["getCurrentSettings"] = handle_get_current_settings;
    wsActionHandlers["getHistoricalData"] = handle_get_historical_data;
    wsActionHandlers["saveThresholds"] = handle_save_thresholds;
    wsActionHandlers["saveLedBrightness"] = handle_save_led_brightness;
    wsActionHandlers["scanWifi"] = handle_scan_wifi;
    wsActionHandlers["connectWifi"] = handle_connect_wifi;
    wsActionHandlers["resetSettings"] = handle_reset_settings;
    wsActionHandlers["startCalibration"] = handle_start_calibration;
}

void handle_websocket_message(uint8_t clientNum, const JsonDocument& doc, JsonDocument& responseDoc) {
    String action = doc["action"];
    if (action.isEmpty()) {
        responseDoc["type"] = "error";
        responseDoc["message"] = "Missing 'action'.";
        return;
    }
    auto it = wsActionHandlers.find(action);
    if (it != wsActionHandlers.end()) {
        it->second(clientNum, doc, responseDoc);
    } else {
        responseDoc["type"] = "error";
        responseDoc["message"] = "Unknown action: " + action;
    }
}

// ... WebSocket Action Handlers ...
void handle_get_current_settings(uint8_t clientNum, const JsonDocument& req, JsonDocument& res) {
    send_current_settings(clientNum);
    res.clear(); // No immediate response needed, handled by send function
}
void handle_get_historical_data(uint8_t clientNum, const JsonDocument& req, JsonDocument& res) {
    send_historical_data(clientNum);
    res.clear();
}
void handle_save_thresholds(uint8_t cnum, const JsonDocument& req, JsonDocument& res) {
    xSemaphoreTake(g_configMutex, portMAX_DELAY);
    g_currentConfig.thresholds.tempMin = req["tempMin"] | g_currentConfig.thresholds.tempMin;
    g_currentConfig.thresholds.tempMax = req["tempMax"] | g_currentConfig.thresholds.tempMax;
    g_currentConfig.thresholds.humMin  = req["humMin"]  | g_currentConfig.thresholds.humMin;
    g_currentConfig.thresholds.humMax  = req["humMax"]  | g_currentConfig.thresholds.humMax;
    g_currentConfig.thresholds.coPpmMax   = req["coPpmMax"]   | g_currentConfig.thresholds.coPpmMax;
    g_currentConfig.thresholds.no2PpmMax  = req["no2PpmMax"]  | g_currentConfig.thresholds.no2PpmMax;
    g_currentConfig.thresholds.c2h5ohPpmMax = req["c2h5ohPpmMax"] | g_currentConfig.thresholds.c2h5ohPpmMax;
    g_currentConfig.thresholds.vocPpmMax = req["vocPpmMax"] | g_currentConfig.thresholds.vocPpmMax;
    xSemaphoreGive(g_configMutex);
    save_config();
    res["type"] = "saveSettingsStatus";
    res["success"] = true;
    res["message"] = "Thresholds saved.";
}

void handle_save_led_brightness(uint8_t cnum, const JsonDocument& req, JsonDocument& res) {
    int brightness = req["brightness"];
    xSemaphoreTake(g_configMutex, portMAX_DELAY);
    g_currentConfig.ledBrightness = brightness;
    xSemaphoreGive(g_configMutex);
    save_config();

    SystemControlMessage msg;
    msg.command = CMD_SET_BRIGHTNESS;
    msg.value = brightness;
    xQueueSend(g_systemControlQueue, &msg, 0);

    res["type"] = "saveBrightnessStatus";
    res["success"] = true;
    res["message"] = "Brightness saved.";
}

void handle_scan_wifi(uint8_t cnum, const JsonDocument& req, JsonDocument& res){
    P_PRINTLN("[TASK_WEB] WiFi扫描请求...");
    int n = WiFi.scanNetworks();
    P_PRINTF("[TASK_WEB] 扫描完成, 发现 %d 个网络。\n", n);
    res["type"] = "wifiScanResults";
    JsonArray networks = res["networks"].to<JsonArray>();
    for (int i = 0; i < n; ++i) {
        JsonObject net = networks.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
    }
}

void handle_connect_wifi(uint8_t cnum, const JsonDocument& req, JsonDocument& res) {
    String ssid = req["ssid"];
    String pass = req["password"];
    
    xSemaphoreTake(g_configMutex, portMAX_DELAY);
    g_wifiStatus.ssidToTry = ssid;
    g_wifiStatus.passwordToTry = pass;
    g_wifiStatus.connectProgress = WIFI_CP_CONNECTING; // Change to connecting directly
    g_wifiStatus.connectAttemptStartTime = millis();
    g_wifiStatus.connectInitiatorClientNum = cnum;
    xSemaphoreGive(g_configMutex);
    
    WiFi.disconnect(true); 
    vTaskDelay(pdMS_TO_TICKS(100)); // Give some time for disconnect to happen
    WiFi.begin(ssid.c_str(), pass.c_str());

    res["type"] = "connectWifiStatus";
    res["message"] = "Initiating connection...";
}

void handle_reset_settings(uint8_t cnum, const JsonDocument& req, JsonDocument& res) {
    reset_all_settings();
    res["type"] = "resetStatus";
    res["success"] = true;
    res["message"] = "Settings reset. Device will restart.";
    String respStr;
    serializeJson(res, respStr);
    webSocket.sendTXT(cnum, respStr);
    vTaskDelay(1000);
    ESP.restart();
}

void handle_start_calibration(uint8_t cnum, const JsonDocument& req, JsonDocument& res) {
    P_PRINTLN("[TASK_WEB] 收到启动校准请求。");
    // This is safe because it just gives a semaphore, which is an ISR-safe operation
    start_calibration_from_isr(); 
    res["type"] = "calibrationStatus";
    res["success"] = true;
    res["message"] = "Calibration initiated.";
}

// ... Data Sending Functions ...
void send_sensor_data(uint8_t clientNum) {
    JsonDocument doc;
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        doc["type"] = "sensorData";
        doc["temperature"] = g_currentState.temperature;
        doc["humidity"] = g_currentState.humidity;
        JsonObject gas = doc["gasPpm"].to<JsonObject>();
        gas["co"] = g_currentState.gasPpmValues.co;
        gas["no2"] = g_currentState.gasPpmValues.no2;
        gas["c2h5oh"] = g_currentState.gasPpmValues.c2h5oh;
        gas["voc"] = g_currentState.gasPpmValues.voc;
        doc["tempStatus"] = getSensorStatusString(g_currentState.tempStatus);
        doc["humStatus"] = getSensorStatusString(g_currentState.humStatus);
        doc["gasCoStatus"] = getSensorStatusString(g_currentState.gasCoStatus);
        doc["gasNo2Status"] = getSensorStatusString(g_currentState.gasNo2Status);
        doc["gasC2h5ohStatus"] = getSensorStatusString(g_currentState.gasC2h5ohStatus);
        doc["gasVocStatus"] = getSensorStatusString(g_currentState.gasVocStatus);

        char timeStr[12];
        EventBits_t bits = xEventGroupGetBits(g_networkStatusEventGroup);
        bool ntpSynced = (bits & NTP_SYNCED_BIT) != 0;
        unsigned long ts = ntpSynced ? time(NULL) : millis();
        generateTimeStr(ts, !ntpSynced, timeStr);
        doc["timeStr"] = timeStr;
        xSemaphoreGive(g_stateMutex);

        String jsonString;
        serializeJson(doc, jsonString);
        if (clientNum == 255) webSocket.broadcastTXT(jsonString);
        else webSocket.sendTXT(clientNum, jsonString);
    }
}

void send_wifi_status(uint8_t clientNum){
    JsonDocument doc;
    doc["type"] = "wifiStatus";
    EventBits_t bits = xEventGroupGetBits(g_networkStatusEventGroup);
    bool isConnected = (bits & WIFI_CONNECTED_BIT) != 0;
    doc["connected"] = isConnected;
    doc["ssid"] = isConnected ? WiFi.SSID() : "N/A";
    doc["ip"] = isConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    doc["ntp_synced"] = (bits & NTP_SYNCED_BIT) != 0;
    
    xSemaphoreTake(g_configMutex, portMAX_DELAY);
    doc["connecting_attempt_ssid"] = (g_wifiStatus.connectProgress == WIFI_CP_CONNECTING || g_wifiStatus.connectProgress == WIFI_CP_DISCONNECTING) ? g_wifiStatus.ssidToTry : "";
    doc["connection_failed"] = (g_wifiStatus.connectProgress == WIFI_CP_FAILED);
    xSemaphoreGive(g_configMutex);

    String jsonString;
    serializeJson(doc, jsonString);
    if (clientNum == 255) webSocket.broadcastTXT(jsonString);
    else webSocket.sendTXT(clientNum, jsonString);
}

void send_historical_data(uint8_t clientNum){
    if (xSemaphoreTake(g_spiffsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        JsonDocument doc;
        doc["type"] = "historicalData";
        JsonArray historyArr = doc["history"].to<JsonArray>();
        const auto& data = g_historicalData.getData();
        for (const auto& dp : data) {
            JsonObject dataPoint = historyArr.add<JsonObject>();
            dataPoint["time"] = dp.timeStr;
            dataPoint["temp"] = dp.temp;
            dataPoint["hum"] = dp.hum;
            dataPoint["co"] = dp.gas.co;
            dataPoint["no2"] = dp.gas.no2;
            dataPoint["c2h5oh"] = dp.gas.c2h5oh;
            dataPoint["voc"] = dp.gas.voc;
        }
        xSemaphoreGive(g_spiffsMutex);

        String jsonString;
        serializeJson(doc, jsonString);
        webSocket.sendTXT(clientNum, jsonString);
    }
}
void send_current_settings(uint8_t clientNum){
    JsonDocument doc;
    if (xSemaphoreTake(g_configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        doc["type"] = "settingsData";
        JsonObject settingsObj = doc["settings"].to<JsonObject>();
        JsonObject thresholdsObj = settingsObj["thresholds"].to<JsonObject>();
        thresholdsObj["tempMin"] = g_currentConfig.thresholds.tempMin;
        thresholdsObj["tempMax"] = g_currentConfig.thresholds.tempMax;
        thresholdsObj["humMin"] = g_currentConfig.thresholds.humMin;
        thresholdsObj["humMax"] = g_currentConfig.thresholds.humMax;
        thresholdsObj["coPpmMax"] = g_currentConfig.thresholds.coPpmMax;
        thresholdsObj["no2PpmMax"] = g_currentConfig.thresholds.no2PpmMax;
        thresholdsObj["c2h5ohPpmMax"] = g_currentConfig.thresholds.c2h5ohPpmMax;
        thresholdsObj["vocPpmMax"] = g_currentConfig.thresholds.vocPpmMax;
        
        JsonObject r0Obj = settingsObj["r0Values"].to<JsonObject>();
        r0Obj["co"] = g_currentConfig.r0Values.co;
        r0Obj["no2"] = g_currentConfig.r0Values.no2;
        r0Obj["c2h5oh"] = g_currentConfig.r0Values.c2h5oh;
        r0Obj["voc"] = g_currentConfig.r0Values.voc;

        settingsObj["currentSSID"] = g_currentConfig.savedSsid;
        settingsObj["ledBrightness"] = g_currentConfig.ledBrightness;
        xSemaphoreGive(g_configMutex);

        String jsonString;
        serializeJson(doc, jsonString);
        webSocket.sendTXT(clientNum, jsonString);
    }
}

void send_calibration_status(uint8_t clientNum) {
    JsonDocument doc;
    if (xSemaphoreTake(g_stateMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        doc["type"] = "calibrationStatusUpdate";
        JsonObject calStatus = doc["calibration"].to<JsonObject>();
        calStatus["state"] = g_currentState.calibrationState;
        calStatus["progress"] = g_currentState.calibrationProgress;
        
        JsonObject measuredR0 = calStatus["measuredR0"].to<JsonObject>();
        measuredR0["co"] = g_currentState.measuredR0.co;
        measuredR0["no2"] = g_currentState.measuredR0.no2;
        measuredR0["c2h5oh"] = g_currentState.measuredR0.c2h5oh;
        measuredR0["voc"] = g_currentState.measuredR0.voc;
        
        xSemaphoreGive(g_stateMutex);
        
        String jsonString;
        serializeJson(doc, jsonString);
        if (clientNum == 255) webSocket.broadcastTXT(jsonString);
        else webSocket.sendTXT(clientNum, jsonString);
    }
}

