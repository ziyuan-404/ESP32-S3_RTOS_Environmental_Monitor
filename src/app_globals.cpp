#include "app_globals.h"

// ==========================================================================
// == 全局共享变量定义 ==
// ==========================================================================
DeviceState g_currentState;
DeviceConfig g_currentConfig;
WifiStatus g_wifiStatus;
CircularBuffer g_historicalData(HISTORICAL_DATA_POINTS);
unsigned long g_gasSensorWarmupEndTime = 0;

// ==========================================================================
// == FreeRTOS 同步对象定义 ==
// ==========================================================================
SemaphoreHandle_t g_stateMutex = NULL;
SemaphoreHandle_t g_configMutex = NULL;
SemaphoreHandle_t g_spiffsMutex = NULL;
EventGroupHandle_t g_networkStatusEventGroup = NULL;
QueueHandle_t g_systemControlQueue = NULL;


// ==========================================================================
// == 构造函数实现 ==
// ==========================================================================
DeviceState::DeviceState() : 
    temperature(0), humidity(0),
    tempStatus(SS_INIT), humStatus(SS_INIT),
    gasCoStatus(SS_INIT), gasNo2Status(SS_INIT),
    gasC2h5ohStatus(SS_INIT), gasVocStatus(SS_INIT),
    alarmStateChanged(false), isAnyAlarmActive(false),
    calibrationState(CAL_IDLE), calibrationProgress(0)
{
    gasPpmValues = {NAN, NAN, NAN, NAN};
    gasRsValues = {NAN, NAN, NAN, NAN};
    measuredR0 = {NAN, NAN, NAN, NAN};
}

DeviceConfig::DeviceConfig() : ledBrightness(DEFAULT_LED_BRIGHTNESS) {
    thresholds = {
        DEFAULT_TEMP_MIN, DEFAULT_TEMP_MAX, DEFAULT_HUM_MIN, DEFAULT_HUM_MAX,
        DEFAULT_CO_PPM_MAX, DEFAULT_NO2_PPM_MAX, DEFAULT_C2H5OH_PPM_MAX, DEFAULT_VOC_PPM_MAX
    };
    r0Values = {
        DEFAULT_R0_CO, DEFAULT_R0_NO2, DEFAULT_R0_C2H5OH, DEFAULT_R0_VOC
    };
}

WifiStatus::WifiStatus() : 
    connectProgress(WIFI_CP_IDLE), connectAttemptStartTime(0),
    connectInitiatorClientNum(255), isScanning(false),
    scanRequesterClientNum(255), scanStartTime(0) {}

CircularBuffer::CircularBuffer(size_t size) : maxSize(size), head(0), tail(0), full(false) {
    buffer.resize(size);
}

// ==========================================================================
// == 全局函数实现 ==
// ==========================================================================

/**
 * @brief 初始化所有全局 FreeRTOS 同步对象
 */
void init_globals() {
    g_stateMutex = xSemaphoreCreateMutex();
    g_configMutex = xSemaphoreCreateMutex();
    g_spiffsMutex = xSemaphoreCreateMutex();
    g_networkStatusEventGroup = xEventGroupCreate();
    g_systemControlQueue = xQueueCreate(10, sizeof(SystemControlMessage));

    if (!g_stateMutex || !g_configMutex || !g_spiffsMutex || !g_networkStatusEventGroup || !g_systemControlQueue) {
        P_PRINTLN("[FATAL] 无法创建 FreeRTOS 同步对象！系统将不稳定。");
        // 在实际产品中，这里可能需要重启或进入安全模式
    }
}

// -- CircularBuffer 方法实现 --
void CircularBuffer::add(const SensorDataPoint& item) {
    buffer[head] = item;
    if (full) {
        tail = (tail + 1) % maxSize;
    }
    head = (head + 1) % maxSize;
    full = (head == tail);
}

const std::vector<SensorDataPoint>& CircularBuffer::getData() const {
    orderedData.clear();
    if (!full && (head == tail)) return orderedData;
    if (full) {
        for (size_t i = 0; i < maxSize; ++i) {
            orderedData.push_back(buffer[(tail + i) % maxSize]);
        }
    } else {
        for (size_t i = tail; i != head; i = (i + 1) % maxSize) {
            orderedData.push_back(buffer[i]);
        }
    }
    return orderedData;
}

size_t CircularBuffer::count() const {
    if (full) return maxSize;
    if (head >= tail) return head - tail;
    return maxSize - (tail - head);
}

void CircularBuffer::clear() {
    head = 0;
    tail = 0;
    full = false;
    orderedData.clear();
}


String getSensorStatusString(SensorStatusVal status) {
    switch (status) {
        case SS_NORMAL: return "normal";
        case SS_WARNING: return "warning";
        case SS_DISCONNECTED: return "disconnected";
        case SS_INIT: return "initializing";
        default: return "unknown";
    }
}

void generateTimeStr(unsigned long current_ts, bool isRelative, char* buffer) {
    if (isRelative) {
        unsigned long seconds = current_ts / 1000;
        unsigned long hours = seconds / 3600;
        seconds %= 3600;
        unsigned long minutes = seconds / 60;
        seconds %= 60;
        sprintf(buffer, "%02lu:%02lu:%02lu", hours, minutes, seconds);
    } else {
        time_t now = current_ts;
        struct tm * p_tm = localtime(&now);
        if (p_tm) {
            sprintf(buffer, "%02d:%02d:%02d", p_tm->tm_hour, p_tm->tm_min, p_tm->tm_sec);
        } else {
            strcpy(buffer, "00:00:00");
        }
    }
}
