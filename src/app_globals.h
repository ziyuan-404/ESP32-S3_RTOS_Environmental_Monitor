#ifndef APP_GLOBALS_H
#define APP_GLOBALS_H

#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>
#include "config.h"

// ==========================================================================
// == 数据结构定义 (与原版基本一致) ==
// ==========================================================================

enum SensorStatusVal { SS_NORMAL, SS_WARNING, SS_DISCONNECTED, SS_INIT };
enum CalibrationState { CAL_IDLE, CAL_IN_PROGRESS, CAL_COMPLETED, CAL_FAILED };
enum WifiConnectProgress { WIFI_CP_IDLE, WIFI_CP_DISCONNECTING, WIFI_CP_CONNECTING, WIFI_CP_FAILED };

struct GasPpmData { float co, no2, c2h5oh, voc; };
struct GasResistData { float co, no2, c2h5oh, voc; };

struct DeviceState {
    int temperature;
    float humidity;
    GasPpmData gasPpmValues;
    GasResistData gasRsValues;
    SensorStatusVal tempStatus, humStatus, gasCoStatus, gasNo2Status, gasC2h5ohStatus, gasVocStatus;
    bool alarmStateChanged;
    bool isAnyAlarmActive;
    CalibrationState calibrationState;
    int calibrationProgress;
    GasResistData measuredR0;
    DeviceState();
};

struct AlarmThresholds {
    int tempMin, tempMax;
    int humMin, humMax;
    float coPpmMax, no2PpmMax, c2h5ohPpmMax, vocPpmMax;
};

struct DeviceConfig {
    AlarmThresholds thresholds;
    GasResistData r0Values;
    String savedSsid;
    String savedPassword;
    uint8_t ledBrightness;
    DeviceConfig();
};

struct WifiStatus {
    WifiConnectProgress connectProgress;
    String ssidToTry;
    String passwordToTry;
    unsigned long connectAttemptStartTime;
    uint8_t connectInitiatorClientNum;
    bool isScanning;
    uint8_t scanRequesterClientNum;
    unsigned long scanStartTime;
    WifiStatus();
};

struct SensorDataPoint {
    unsigned long timestamp;
    bool isTimeRelative;
    int temp;
    int hum;
    GasPpmData gas;
    char timeStr[12];
};

class CircularBuffer {
public:
    CircularBuffer(size_t size);
    void add(const SensorDataPoint& item);
    const std::vector<SensorDataPoint>& getData() const;
    size_t count() const;
    void clear();
private:
    std::vector<SensorDataPoint> buffer;
    mutable std::vector<SensorDataPoint> orderedData;
    size_t maxSize, head, tail;
    bool full;
};

// ==========================================================================
// == 系统控制任务的命令队列结构体定义 ==
// ==========================================================================
enum SystemCommand {
    CMD_UPDATE_STATUS,      // 更新LED状态 (基于整体设备状态)
    CMD_SET_BRIGHTNESS,     // 设置LED亮度
    CMD_ALARM_ON,           // 触发报警 (蜂鸣器+LED)
    CMD_ALARM_OFF           // 关闭报警
};

struct SystemControlMessage {
    SystemCommand command;
    int value; // 用于传递亮度值等
};

// ==========================================================================
// == 全局共享变量声明 ==
// ==========================================================================
extern DeviceState g_currentState;
extern DeviceConfig g_currentConfig;
extern WifiStatus g_wifiStatus;
extern CircularBuffer g_historicalData;
extern unsigned long g_gasSensorWarmupEndTime;

// ==========================================================================
// == FreeRTOS 同步对象声明 ==
// ==========================================================================
// -- 互斥锁 (Mutexes) --
extern SemaphoreHandle_t g_stateMutex;       // 保护 g_currentState
extern SemaphoreHandle_t g_configMutex;      // 保护 g_currentConfig
extern SemaphoreHandle_t g_spiffsMutex;      // 保护 SPIFFS 文件操作

// -- 事件组 (Event Group) --
extern EventGroupHandle_t g_networkStatusEventGroup; // 同步网络状态 (WiFi, NTP)

// -- 队列 (Queue) --
extern QueueHandle_t g_systemControlQueue; // 用于向 system_control_task 发送命令

// ==========================================================================
// == 全局函数声明 ==
// ==========================================================================
void init_globals();
String getSensorStatusString(SensorStatusVal status);
void generateTimeStr(unsigned long ts, bool isRelative, char* buffer);

#endif // APP_GLOBALS_H
