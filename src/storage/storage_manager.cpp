#include "storage_manager.h"
#include "app_globals.h"
#include <SPIFFS.h>
#include <WiFi.h>

/**
 * @brief 初始化并挂载 SPIFFS 文件系统
 */
void init_spiffs() {
    if (!SPIFFS.begin(true)) {
        P_PRINTLN("[SPIFFS] 初始化失败!");
    } else {
        P_PRINTLN("[SPIFFS] 文件系统已挂载。");
    }
}

/**
 * @brief 从 SPIFFS 加载配置到全局 g_currentConfig (线程安全)
 */
void load_config() {
    if (xSemaphoreTake(g_spiffsMutex, portMAX_DELAY) == pdTRUE) {
        if (xSemaphoreTake(g_configMutex, portMAX_DELAY) == pdTRUE) {
            P_PRINTLN("[STORAGE] 正在加载配置...");
            if (SPIFFS.exists(SETTINGS_FILE)) {
                File file = SPIFFS.open(SETTINGS_FILE, "r");
                if (file && file.size() > 0) {
                    JsonDocument doc; // 使用自动管理的JsonDocument
                    DeserializationError error = deserializeJson(doc, file);
                    if (error) {
                        P_PRINTF("[STORAGE] 配置JSON解析失败: %s. 将重置为默认值。\n", error.c_str());
                        g_currentConfig = DeviceConfig(); // 使用默认构造函数重置
                    } else {
                        JsonObject thresholdsObj = doc["thresholds"];
                        g_currentConfig.thresholds.tempMin = thresholdsObj["tempMin"] | DEFAULT_TEMP_MIN;
                        g_currentConfig.thresholds.tempMax = thresholdsObj["tempMax"] | DEFAULT_TEMP_MAX;
                        g_currentConfig.thresholds.humMin  = thresholdsObj["humMin"]  | DEFAULT_HUM_MIN;
                        g_currentConfig.thresholds.humMax  = thresholdsObj["humMax"]  | DEFAULT_HUM_MAX;
                        g_currentConfig.thresholds.coPpmMax = thresholdsObj["coPpmMax"] | DEFAULT_CO_PPM_MAX;
                        g_currentConfig.thresholds.no2PpmMax = thresholdsObj["no2PpmMax"] | DEFAULT_NO2_PPM_MAX;
                        g_currentConfig.thresholds.c2h5ohPpmMax = thresholdsObj["c2h5ohPpmMax"] | DEFAULT_C2H5OH_PPM_MAX;
                        g_currentConfig.thresholds.vocPpmMax = thresholdsObj["vocPpmMax"] | DEFAULT_VOC_PPM_MAX;

                        JsonObject r0Obj = doc["r0Values"];
                        g_currentConfig.r0Values.co = r0Obj["co"] | DEFAULT_R0_CO;
                        g_currentConfig.r0Values.no2 = r0Obj["no2"] | DEFAULT_R0_NO2;
                        g_currentConfig.r0Values.c2h5oh = r0Obj["c2h5oh"] | DEFAULT_R0_C2H5OH;
                        g_currentConfig.r0Values.voc = r0Obj["voc"] | DEFAULT_R0_VOC;
                        
                        g_currentConfig.savedSsid = doc["wifi"]["ssid"].as<String>();
                        g_currentConfig.savedPassword = doc["wifi"]["password"].as<String>();
                        g_currentConfig.ledBrightness = doc["led"]["brightness"] | DEFAULT_LED_BRIGHTNESS;
                        P_PRINTLN("[STORAGE] 配置加载成功。");
                    }
                } else {
                     P_PRINTLN(file ? "[STORAGE] 配置文件为空, 将重置。" : "[STORAGE] 打开配置文件失败, 将重置。");
                     g_currentConfig = DeviceConfig();
                }
                if(file) file.close();
            } else {
                P_PRINTLN("[STORAGE] 配置文件不存在, 使用默认值。");
                g_currentConfig = DeviceConfig();
                // 首次启动时保存一次默认配置
                xSemaphoreGive(g_configMutex);
                xSemaphoreGive(g_spiffsMutex);
                save_config(); 
                return; // 提前返回，因为锁已经释放
            }
            xSemaphoreGive(g_configMutex);
        }
        xSemaphoreGive(g_spiffsMutex);
    }
}

/**
 * @brief 保存全局 g_currentConfig 到 SPIFFS (线程安全)
 */
void save_config() {
    if (xSemaphoreTake(g_spiffsMutex, portMAX_DELAY) == pdTRUE) {
        if (xSemaphoreTake(g_configMutex, portMAX_DELAY) == pdTRUE) {
            P_PRINTLN("[STORAGE] 正在保存配置...");
            File file = SPIFFS.open(SETTINGS_FILE, "w");
            if (file) {
                JsonDocument doc;
                JsonObject thresholdsObj = doc["thresholds"].to<JsonObject>();
                thresholdsObj["tempMin"] = g_currentConfig.thresholds.tempMin;
                thresholdsObj["tempMax"] = g_currentConfig.thresholds.tempMax;
                thresholdsObj["humMin"] = g_currentConfig.thresholds.humMin;
                thresholdsObj["humMax"] = g_currentConfig.thresholds.humMax;
                thresholdsObj["coPpmMax"] = g_currentConfig.thresholds.coPpmMax;
                thresholdsObj["no2PpmMax"] = g_currentConfig.thresholds.no2PpmMax;
                thresholdsObj["c2h5ohPpmMax"] = g_currentConfig.thresholds.c2h5ohPpmMax;
                thresholdsObj["vocPpmMax"] = g_currentConfig.thresholds.vocPpmMax;
                
                JsonObject r0Obj = doc["r0Values"].to<JsonObject>();
                r0Obj["co"] = g_currentConfig.r0Values.co;
                r0Obj["no2"] = g_currentConfig.r0Values.no2;
                r0Obj["c2h5oh"] = g_currentConfig.r0Values.c2h5oh;
                r0Obj["voc"] = g_currentConfig.r0Values.voc;

                JsonObject wifiObj = doc["wifi"].to<JsonObject>();
                wifiObj["ssid"] = g_currentConfig.savedSsid;
                wifiObj["password"] = g_currentConfig.savedPassword;

                JsonObject ledObj = doc["led"].to<JsonObject>();
                ledObj["brightness"] = g_currentConfig.ledBrightness;

                if (serializeJson(doc, file) == 0) {
                    P_PRINTLN("[STORAGE] 写入配置文件失败。");
                } else {
                    P_PRINTLN("[STORAGE] 配置保存成功。");
                }
                file.close();
            } else {
                P_PRINTLN("[STORAGE] 创建/打开配置文件失败。");
            }
            xSemaphoreGive(g_configMutex);
        }
        xSemaphoreGive(g_spiffsMutex);
    }
}

/**
 * @brief 重置所有设置并保存 (线程安全)
 */
void reset_all_settings() {
     if (xSemaphoreTake(g_configMutex, portMAX_DELAY) == pdTRUE) {
        P_PRINTLN("[STORAGE] 重置所有设置为默认值。");
        g_currentConfig = DeviceConfig();
        xSemaphoreGive(g_configMutex);
     }
     save_config(); // 保存重置后的配置
     
     // 同时清空历史数据
     if (xSemaphoreTake(g_spiffsMutex, portMAX_DELAY) == pdTRUE) {
         g_historicalData.clear();
         if (SPIFFS.exists(HISTORICAL_DATA_FILE)) {
             SPIFFS.remove(HISTORICAL_DATA_FILE);
         }
         xSemaphoreGive(g_spiffsMutex);
     }
}

/**
 * @brief 从 SPIFFS 加载历史数据 (线程安全)
 */
void load_historical_data() {
    if (xSemaphoreTake(g_spiffsMutex, portMAX_DELAY) == pdTRUE) {
        P_PRINTLN("[STORAGE] 正在加载历史数据...");
        g_historicalData.clear();
        if (SPIFFS.exists(HISTORICAL_DATA_FILE)) {
            File file = SPIFFS.open(HISTORICAL_DATA_FILE, "r");
            if (file && file.size() > 0) {
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, file);
                if (error) {
                    P_PRINTF("[STORAGE] 历史数据JSON解析失败: %s\n", error.c_str());
                } else {
                    JsonArray arr = doc.as<JsonArray>();
                    for (JsonObject obj : arr) {
                        SensorDataPoint dp;
                        dp.timestamp = obj["ts"];
                        dp.isTimeRelative = obj["rel"];
                        dp.temp = obj["t"];
                        dp.hum = obj["h"];
                        dp.gas.co = obj["co"];
                        dp.gas.no2 = obj["no2"];
                        dp.gas.c2h5oh = obj["c2h5oh"];
                        dp.gas.voc = obj["voc"];
                        generateTimeStr(dp.timestamp, dp.isTimeRelative, dp.timeStr);
                        g_historicalData.add(dp);
                    }
                    P_PRINTF("[STORAGE] 加载了 %u 条历史数据。\n", g_historicalData.count());
                }
            }
            if(file) file.close();
        }
        xSemaphoreGive(g_spiffsMutex);
    }
}

/**
 * @brief 保存历史数据到 SPIFFS (线程安全)
 */
void save_historical_data() {
    if (xSemaphoreTake(g_spiffsMutex, portMAX_DELAY) == pdTRUE) {
        P_PRINTLN("[STORAGE] 正在保存历史数据...");
        File file = SPIFFS.open(HISTORICAL_DATA_FILE, "w");
        if (file) {
            const std::vector<SensorDataPoint>& dataToSave = g_historicalData.getData();
            JsonDocument doc;
            JsonArray arr = doc.to<JsonArray>();
            for (const auto& dp : dataToSave) {
                JsonObject obj = arr.add<JsonObject>();
                obj["ts"] = dp.timestamp;
                obj["rel"] = dp.isTimeRelative;
                obj["t"] = dp.temp;
                obj["h"] = dp.hum;
                obj["co"] = dp.gas.co;
                obj["no2"] = dp.gas.no2;
                obj["c2h5oh"] = dp.gas.c2h5oh;
                obj["voc"] = dp.gas.voc;
            }
            if (serializeJson(doc, file) == 0) {
                P_PRINTLN("[STORAGE] 写入历史数据失败。");
            } else {
                 P_PRINTF("[STORAGE] %u 条历史数据已保存。\n", dataToSave.size());
            }
            file.close();
        } else {
             P_PRINTLN("[STORAGE] 创建历史数据文件失败。");
        }
        xSemaphoreGive(g_spiffsMutex);
    }
}

