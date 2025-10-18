#ifndef TASK_SENSOR_H
#define TASK_SENSOR_H

#include "app_globals.h"

// 传感器读取和处理的周期 (毫秒)
#define SENSOR_READ_INTERVAL_MS 2000

void create_sensor_task();
void start_calibration_from_isr(); // 一个安全的从其他任务或回调中启动校准的方法

#endif // TASK_SENSOR_H
