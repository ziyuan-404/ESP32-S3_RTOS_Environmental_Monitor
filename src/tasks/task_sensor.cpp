#include "task_sensor.h"
#include <DHT.h>
#include <Wire.h>
#include "Multichannel_Gas_GMXXX.h"
#include "storage/storage_manager.h"

// ==========================================================================
// == 任务内部使用的静态变量和对象 ==
// ==========================================================================
static DHT dht(DHT_PIN, DHT_TYPE);
static GAS_GMXXX<TwoWire> gas_sensor;
static bool gasSensorConnected = false;
static SemaphoreHandle_t calibrationTrigger = NULL;

// 传感器负载电阻 (RL)，单位 kOhm
const float RL_VALUE_KOHM = 10.0;
const float SENSOR_VCC = 3.3f;
const float ADC_RESOLUTION = 4095.0f;

// ==========================================================================
// == 内部函数声明 ==
// ==========================================================================
void sensor_task_code(void *pvParameters);
void init_sensors();
void read_dht_sensor(DeviceState& state);
void read_gas_sensor(DeviceState& state);
void calculate_ppm(DeviceState& state, const GasResistData& r0);
void check_alarms(DeviceState& state, const AlarmThresholds& thresholds);
void run_calibration_sequence();
float adcToRs(int adc_val);
void addHistoricalDataPoint(CircularBuffer& histBuffer, const DeviceState& state);


// ==========================================================================
// == 任务创建和控制函数 ==
// ==========================================================================
void create_sensor_task() {
    calibrationTrigger = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(
        sensor_task_code,
        "SensorTask",
        TASK_SENSOR_STACK_SIZE,
        NULL,
        TASK_SENSOR_PRIO,
        NULL,
        0 // 核心0
    );
}

// 从ISR（中断服务程序）或高优先级任务中安全地触发校准
void start_calibration_from_isr() {
    if (calibrationTrigger != NULL) {
        xSemaphoreGiveFromISR(calibrationTrigger, NULL);
    }
}

// ==========================================================================
// == 传感器任务主代码 ==
// ==========================================================================
void sensor_task_code(void *pvParameters) {
    P_PRINTLN("[TASK_SENSOR] 任务启动。");
    init_sensors();
    g_gasSensorWarmupEndTime = millis() + GAS_SENSOR_WARMUP_PERIOD_MS;

    TickType_t lastWakeTime = xTaskGetTickCount();

    for (;;) {
        // 1. 检查是否收到校准指令
        if (xSemaphoreTake(calibrationTrigger, 0) == pdTRUE) {
            run_calibration_sequence();
            // 校准后，确保下一次循环是常规读取
        }

        // 2. 执行常规的传感器读取和处理
        // 锁定共享状态，准备更新
        if (xSemaphoreTake(g_stateMutex, portMAX_DELAY) == pdTRUE) {
            if (xSemaphoreTake(g_configMutex, portMAX_DELAY) == pdTRUE) {
                read_dht_sensor(g_currentState);
                read_gas_sensor(g_currentState);
                if (gasSensorConnected && millis() > g_gasSensorWarmupEndTime) {
                    calculate_ppm(g_currentState, g_currentConfig.r0Values);
                }
                check_alarms(g_currentState, g_currentConfig.thresholds);

                // 添加到历史数据
                addHistoricalDataPoint(g_historicalData, g_currentState);
                
                // 设置事件组标志，通知Web任务数据已更新
                xEventGroupSetBits(g_networkStatusEventGroup, SENSOR_DATA_UPDATED_BIT);

                xSemaphoreGive(g_configMutex);
            }
            xSemaphoreGive(g_stateMutex);
        }

        // 3. 周期性保存历史数据
        static unsigned long lastSaveTime = 0;
        if (millis() - lastSaveTime > HISTORICAL_DATA_SAVE_INTERVAL_MS) {
            lastSaveTime = millis();
            save_historical_data();
        }

        // 4. 精确延时，保持固定的读取周期
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(SENSOR_READ_INTERVAL_MS));
    }
}

// ==========================================================================
// == 任务内部实现函数 ==
// ==========================================================================
void init_sensors() {
    dht.begin();
    P_PRINTLN("[TASK_SENSOR] DHT传感器已初始化。");

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    P_PRINTF("[TASK_SENSOR] I2C总线已在 SDA=%d, SCL=%d 初始化.\n", I2C_SDA_PIN, I2C_SCL_PIN);

    Wire.beginTransmission(GAS_SENSOR_I2C_ADDRESS);
    if (Wire.endTransmission() == 0) {
        gasSensorConnected = true;
        gas_sensor.begin(Wire, GAS_SENSOR_I2C_ADDRESS);
        P_PRINTLN("[TASK_SENSOR] Grove多通道气体传感器V2已连接。");
        P_PRINTLN("[TASK_SENSOR] 等待传感器预热...");
    } else {
        gasSensorConnected = false;
        P_PRINTLN("[TASK_SENSOR] ***错误*** 未检测到气体传感器!");
    }
}

float adcToRs(int adc_val) {
    if (adc_val <= 0) return -1.0;
    float v_out = (float)adc_val * SENSOR_VCC / ADC_RESOLUTION;
    if (v_out >= SENSOR_VCC) return -1.0;
    return (SENSOR_VCC * RL_VALUE_KOHM / v_out) - RL_VALUE_KOHM;
}

void read_dht_sensor(DeviceState& state) {
    float newTemp = dht.readTemperature();
    float newHum = dht.readHumidity();
    if (isnan(newTemp) || isnan(newHum)) {
        state.tempStatus = SS_DISCONNECTED;
        state.humStatus = SS_DISCONNECTED;
        state.temperature = 0;
        state.humidity = 0;
    } else {
        state.temperature = round(newTemp);
        state.humidity = newHum;
        if (state.tempStatus == SS_INIT || state.tempStatus == SS_DISCONNECTED) {
            state.tempStatus = SS_NORMAL;
        }
        if (state.humStatus == SS_INIT || state.humStatus == SS_DISCONNECTED) {
            state.humStatus = SS_NORMAL;
        }
    }
}

void read_gas_sensor(DeviceState& state) {
    if (!gasSensorConnected) {
        state.gasCoStatus = state.gasNo2Status = state.gasC2h5ohStatus = state.gasVocStatus = SS_DISCONNECTED;
        state.gasRsValues = {NAN, NAN, NAN, NAN};
        return;
    }

    if (millis() < g_gasSensorWarmupEndTime) {
        state.gasCoStatus = state.gasNo2Status = state.gasC2h5ohStatus = state.gasVocStatus = SS_INIT;
        state.gasPpmValues = {NAN, NAN, NAN, NAN};
        state.gasRsValues = {NAN, NAN, NAN, NAN};
        return;
    }
    
    // 正常读取
    state.gasRsValues.co = adcToRs(gas_sensor.getGM702B());
    state.gasRsValues.no2 = adcToRs(gas_sensor.getGM102B());
    state.gasRsValues.c2h5oh = adcToRs(gas_sensor.getGM302B());
    state.gasRsValues.voc = adcToRs(gas_sensor.getGM502B());

    // 检查传感器是否断开
    state.gasCoStatus = (state.gasRsValues.co < 0) ? SS_DISCONNECTED : (state.gasCoStatus == SS_INIT ? SS_NORMAL : state.gasCoStatus);
    state.gasNo2Status = (state.gasRsValues.no2 < 0) ? SS_DISCONNECTED : (state.gasNo2Status == SS_INIT ? SS_NORMAL : state.gasNo2Status);
    state.gasC2h5ohStatus = (state.gasRsValues.c2h5oh < 0) ? SS_DISCONNECTED : (state.gasC2h5ohStatus == SS_INIT ? SS_NORMAL : state.gasC2h5ohStatus);
    state.gasVocStatus = (state.gasRsValues.voc < 0) ? SS_DISCONNECTED : (state.gasVocStatus == SS_INIT ? SS_NORMAL : state.gasVocStatus);
}

void calculate_ppm(DeviceState& state, const GasResistData& r0) {
    auto calculate = [](float rs, float r0_val, double factor, double offset) -> double {
        if (rs > 0 && r0_val > 0) {
            float ratio = rs / r0_val;
            return pow(10, (log10(ratio) * factor) + offset);
        }
        return NAN;
    };
    state.gasPpmValues.co = calculate(state.gasRsValues.co, r0.co, -2.82, -0.12);
    state.gasPpmValues.no2 = calculate(state.gasRsValues.no2, r0.no2, 1.9, -0.2);
    state.gasPpmValues.c2h5oh = calculate(state.gasRsValues.c2h5oh, r0.c2h5oh, -2.0, -0.5);
    state.gasPpmValues.voc = calculate(state.gasRsValues.voc, r0.voc, -2.5, -0.6);
}

void check_alarms(DeviceState& state, const AlarmThresholds& thresholds) {
    bool previousAlarmState = state.isAnyAlarmActive;

    auto check = [&](SensorStatusVal& status, float value, float min_val, float max_val) {
        if (status == SS_NORMAL && (value < min_val || value > max_val)) status = SS_WARNING;
        else if (status == SS_WARNING && (value >= min_val && value <= max_val)) status = SS_NORMAL;
    };

    auto check_max = [&](SensorStatusVal& status, float value, float max_val) {
        if (status == SS_NORMAL && !isnan(value) && value > max_val) status = SS_WARNING;
        else if (status == SS_WARNING && (isnan(value) || value <= max_val)) status = SS_NORMAL;
    };
    
    if (state.tempStatus != SS_DISCONNECTED) check(state.tempStatus, state.temperature, thresholds.tempMin, thresholds.tempMax);
    if (state.humStatus != SS_DISCONNECTED) check(state.humStatus, state.humidity, thresholds.humMin, thresholds.humMax);
    if (state.gasCoStatus != SS_DISCONNECTED) check_max(state.gasCoStatus, state.gasPpmValues.co, thresholds.coPpmMax);
    if (state.gasNo2Status != SS_DISCONNECTED) check_max(state.gasNo2Status, state.gasPpmValues.no2, thresholds.no2PpmMax);
    if (state.gasC2h5ohStatus != SS_DISCONNECTED) check_max(state.gasC2h5ohStatus, state.gasPpmValues.c2h5oh, thresholds.c2h5ohPpmMax);
    if (state.gasVocStatus != SS_DISCONNECTED) check_max(state.gasVocStatus, state.gasPpmValues.voc, thresholds.vocPpmMax);

    state.isAnyAlarmActive = (state.tempStatus == SS_WARNING || state.humStatus == SS_WARNING || 
                             state.gasCoStatus == SS_WARNING || state.gasNo2Status == SS_WARNING ||
                             state.gasC2h5ohStatus == SS_WARNING || state.gasVocStatus == SS_WARNING);
    
    if (state.isAnyAlarmActive != previousAlarmState) {
        state.alarmStateChanged = true;
        SystemControlMessage msg;
        msg.command = state.isAnyAlarmActive ? CMD_ALARM_ON : CMD_ALARM_OFF;
        xQueueSend(g_systemControlQueue, &msg, 0);
    } else {
        state.alarmStateChanged = false;
    }
}

void addHistoricalDataPoint(CircularBuffer& histBuffer, const DeviceState& state) {
    if (state.temperature == 0 && state.humidity == 0 && isnan(state.gasPpmValues.co)) return;
    SensorDataPoint dp;
    
    EventBits_t bits = xEventGroupGetBits(g_networkStatusEventGroup);
    bool ntpSynced = (bits & NTP_SYNCED_BIT) != 0;

    dp.isTimeRelative = !ntpSynced;
    if (dp.isTimeRelative) {
        dp.timestamp = millis();
    } else {
        time_t now;
        time(&now);
        dp.timestamp = now;
    }
    dp.temp = state.temperature; 
    dp.hum = (int)state.humidity; 
    dp.gas = state.gasPpmValues;
    generateTimeStr(dp.timestamp, dp.isTimeRelative, dp.timeStr);
    histBuffer.add(dp);
}


void run_calibration_sequence() {
    P_PRINTLN("[TASK_SENSOR] 开始校准流程...");
    
    // 更新状态为校准中
    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    g_currentState.calibrationState = CAL_IN_PROGRESS;
    g_currentState.calibrationProgress = 0;
    g_currentState.measuredR0 = {NAN, NAN, NAN, NAN};
    xEventGroupSetBits(g_networkStatusEventGroup, SENSOR_DATA_UPDATED_BIT); // 通知Web更新UI
    xSemaphoreGive(g_stateMutex);

    // 如果传感器仍在物理预热，则等待
    if (millis() < g_gasSensorWarmupEndTime) {
        P_PRINTLN("[TASK_SENSOR] 校准：等待传感器物理预热完成...");
        while (millis() < g_gasSensorWarmupEndTime) {
            xSemaphoreTake(g_stateMutex, portMAX_DELAY);
            g_currentState.calibrationProgress = (int)((float)millis() / g_gasSensorWarmupEndTime * 20.0f);
            xEventGroupSetBits(g_networkStatusEventGroup, SENSOR_DATA_UPDATED_BIT);
            xSemaphoreGive(g_stateMutex);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    
    GasResistData r0_sum = {0.0, 0.0, 0.0, 0.0};
    int valid_samples[4] = {0, 0, 0, 0};

    P_PRINTLN("[TASK_SENSOR] 校准：开始采集数据...");
    for (int i = 0; i < CALIBRATION_SAMPLE_COUNT; i++) {
        float rs_co = adcToRs(gas_sensor.getGM702B());
        float rs_no2 = adcToRs(gas_sensor.getGM102B());
        float rs_c2h5oh = adcToRs(gas_sensor.getGM302B());
        float rs_voc = adcToRs(gas_sensor.getGM502B());

        if (rs_co > 0) { r0_sum.co += rs_co; valid_samples[0]++; }
        if (rs_no2 > 0) { r0_sum.no2 += rs_no2; valid_samples[1]++; }
        if (rs_c2h5oh > 0) { r0_sum.c2h5oh += rs_c2h5oh; valid_samples[2]++; }
        if (rs_voc > 0) { r0_sum.voc += rs_voc; valid_samples[3]++; }

        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        g_currentState.calibrationProgress = 20 + (int)((float)(i + 1) / CALIBRATION_SAMPLE_COUNT * 80.0f);
        g_currentState.measuredR0.co = (valid_samples[0] > 0) ? (r0_sum.co / valid_samples[0]) : NAN;
        g_currentState.measuredR0.no2 = (valid_samples[1] > 0) ? (r0_sum.no2 / valid_samples[1]) : NAN;
        g_currentState.measuredR0.c2h5oh = (valid_samples[2] > 0) ? (r0_sum.c2h5oh / valid_samples[2]) : NAN;
        g_currentState.measuredR0.voc = (valid_samples[3] > 0) ? (r0_sum.voc / valid_samples[3]) : NAN;
        xEventGroupSetBits(g_networkStatusEventGroup, SENSOR_DATA_UPDATED_BIT);
        xSemaphoreGive(g_stateMutex);

        vTaskDelay(pdMS_TO_TICKS(CALIBRATION_SAMPLE_INTERVAL_MS));
    }

    P_PRINTLN("[TASK_SENSOR] 校准：数据采集完成，正在计算并保存...");
    bool success = false;
    xSemaphoreTake(g_configMutex, portMAX_DELAY);
    if (valid_samples[0] > 0) { g_currentConfig.r0Values.co = r0_sum.co / valid_samples[0]; success = true; }
    if (valid_samples[1] > 0) { g_currentConfig.r0Values.no2 = r0_sum.no2 / valid_samples[1]; success = true; }
    if (valid_samples[2] > 0) { g_currentConfig.r0Values.c2h5oh = r0_sum.c2h5oh / valid_samples[2]; success = true; }
    if (valid_samples[3] > 0) { g_currentConfig.r0Values.voc = r0_sum.voc / valid_samples[3]; success = true; }
    xSemaphoreGive(g_configMutex);

    xSemaphoreTake(g_stateMutex, portMAX_DELAY);
    if (success) {
        g_currentState.calibrationState = CAL_COMPLETED;
        P_PRINTLN("[TASK_SENSOR] 校准成功。");
    } else {
        g_currentState.calibrationState = CAL_FAILED;
        P_PRINTLN("[TASK_SENSOR] 校准失败，没有有效的采样数据。");
    }
    xEventGroupSetBits(g_networkStatusEventGroup, SENSOR_DATA_UPDATED_BIT);
    xSemaphoreGive(g_stateMutex);

    if (success) {
        save_config(); // 保存新的R0值
        P_PRINTLN("[TASK_SENSOR] 新校准值已保存，3秒后设备将重启...");
        vTaskDelay(pdMS_TO_TICKS(3000));
        ESP.restart();
    } else {
        vTaskDelay(pdMS_TO_TICKS(3000)); // 失败后等待3秒，然后恢复
        xSemaphoreTake(g_stateMutex, portMAX_DELAY);
        g_currentState.calibrationState = CAL_IDLE;
        xEventGroupSetBits(g_networkStatusEventGroup, SENSOR_DATA_UPDATED_BIT);
        xSemaphoreGive(g_stateMutex);
    }
}

