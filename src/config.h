#ifndef CONFIG_H
#define CONFIG_H

// ==========================================================================
// == WiFi 和网络配置 ==
// ==========================================================================
#define WIFI_AP_SSID "ESP32_Sensor_Hub"
#define WIFI_AP_PASSWORD ""
#define WIFI_AP_CHANNEL 1
#define WIFI_AP_MAX_CONNECTIONS 4
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.nist.gov"
#define GMT_OFFSET_SEC 3600 * 8
#define DAYLIGHT_OFFSET_SEC 0
#define NTP_SYNC_INTERVAL_MS (60 * 60 * 1000UL) // 1小时同步一次
#define WIFI_RECONNECT_DELAY_MS 10000 // WiFi断线后重连间隔

// ==========================================================================
// == OneNET MQTT 配置 ==
// ==========================================================================
#define ONENET_MQTT_SERVER "www.onenet.hk.chinamobile.com"
#define ONENET_MQTT_PORT 1883
#define ONENET_PRODUCT_ID "IHL2T99b8k"
#define ONENET_DEVICE_ID "xiaomi"
#define ONENET_TOKEN "version=2018-10-31&res=products%2FIHL2T99b8k%2Fdevices%2Fxiaomi&et=2538749875&method=md5&sign=11FeOhHkd%2FH6sq9FuCMNlA%3D%3D"
#define ONENET_POST_INTERVAL_MS 60000 // 60秒上报一次
#define ONENET_TOPIC_PROPERTY_POST "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_ID "/thing/property/post"
#define ONENET_TOPIC_PROPERTY_SET "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_ID "/thing/property/set"
#define ONENET_TOPIC_PROPERTY_POST_REPLY "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_ID "/thing/property/post/reply"


// ==========================================================================
// == 硬件引脚定义 ==
// ==========================================================================
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9
#define DHT_PIN 4
#define BUZZER_PIN 10
#define NEOPIXEL_PIN 48
#define NEOPIXEL_NUM 1

// ==========================================================================
// == 传感器配置 ==
// ==========================================================================
#define DHT_TYPE DHT11
#define GAS_SENSOR_I2C_ADDRESS 0x08
#define GAS_SENSOR_WARMUP_PERIOD_MS 60000 // 60秒物理预热
#define CALIBRATION_SAMPLE_COUNT 100
#define CALIBRATION_SAMPLE_INTERVAL_MS 200

// ==========================================================================
// == 默认配置值 ==
// ==========================================================================
#define DEFAULT_LED_BRIGHTNESS 20
#define DEFAULT_R0_CO 20.0f
#define DEFAULT_R0_NO2 10.0f
#define DEFAULT_R0_C2H5OH 2.0f
#define DEFAULT_R0_VOC 50.0f
#define DEFAULT_TEMP_MIN 10
#define DEFAULT_TEMP_MAX 30
#define DEFAULT_HUM_MIN 30
#define DEFAULT_HUM_MAX 70
#define DEFAULT_CO_PPM_MAX 50.0f
#define DEFAULT_NO2_PPM_MAX 5.0f
#define DEFAULT_C2H5OH_PPM_MAX 200.0f
#define DEFAULT_VOC_PPM_MAX 10.0f

// ==========================================================================
// == 数据和文件系统配置 ==
// ==========================================================================
#define SETTINGS_FILE "/settings_v5_rtos.json"
#define HISTORICAL_DATA_FILE "/history_v5_rtos.json"
#define HISTORICAL_DATA_POINTS 90
#define HISTORICAL_DATA_SAVE_INTERVAL_MS (5 * 60 * 1000UL) // 5分钟

// ==========================================================================
// == FreeRTOS 任务配置 ==
// ==========================================================================
// -- 任务优先级 (数字越大，优先级越高) --
#define TASK_WIFI_PRIO 3
#define TASK_WEB_SERVER_PRIO 2
#define TASK_SENSOR_PRIO 2
#define TASK_ONENET_PRIO 1
#define TASK_SYSTEM_CONTROL_PRIO 4 // 最高，确保对LED/蜂鸣器的响应及时

// -- 任务堆栈大小 (Bytes) --
#define TASK_WIFI_STACK_SIZE 4096
#define TASK_WEB_SERVER_STACK_SIZE 8192
#define TASK_SENSOR_STACK_SIZE 8192 // 校准和计算需要较大堆栈
#define TASK_ONENET_STACK_SIZE 8192
#define TASK_SYSTEM_CONTROL_STACK_SIZE 2048

// -- 事件组标志位 --
#define WIFI_CONNECTED_BIT (1 << 0)
#define NTP_SYNCED_BIT (1 << 1)
#define SENSOR_DATA_UPDATED_BIT (1 << 2) // 新增: 用于通知Web任务数据已更新

// ==========================================================================
// == 调试信息输出 ==
// ==========================================================================
#define PROJECT_SERIAL_DEBUG true
#if PROJECT_SERIAL_DEBUG
  #define P_PRINT(x) Serial.print(x)
  #define P_PRINTLN(x) Serial.println(x)
  #define P_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define P_PRINT(x)
  #define P_PRINTLN(x)
  #define P_PRINTF(fmt, ...)
#endif

#endif // CONFIG_H

