# **ESP32-S3 RTOS 环境监测系统**

[English Version](https://github.com/ziyuan-404/WouoUI-esp32s3/blob/main/README-CH.md)

## **概览**

本项目是一个基于 ESP32-S3 微控制器的全面、实时的环境监测系统。它利用 FreeRTOS 高效地管理多个并发任务，确保稳定的传感器数据采集、Web 服务器运行以及云端 IoT 通信。

系统能够读取环境数据（温度、湿度、光照强度以及各种气体浓度），使用 WebSockets 和 Chart.js 在响应迅速的本地 Web 仪表板上显示数据，并支持通过 MQTT 将遥测数据上传至 OneNet 物联网平台。

## **功能特性**

* **实时操作系统 (RTOS)**：利用 FreeRTOS 实现强大的多任务处理（传感器读取、WiFi 管理、Web 服务器、MQTT 通信）。  
* **多传感器集成**：通过 I2C 总线读取温度、湿度、环境光以及多种气体（NO2、C2H5OH、VOC、CO）的数据。  
* **动态 Web 仪表板**：通过 ESP32 的 SPIFFS 文件系统提供本地 Web 界面。仪表板包含由 Chart.js 驱动的实时动态图表。  
* **WebSocket 通信**：确保 Web 客户端与 ESP32 之间保持低延迟的双向通信，以实现数据的实时更新。  
* **云端 IoT 集成**：支持通过 MQTT 连接到 OneNet 物联网平台，用于远程数据记录和监控。  
* **持久化存储**：使用 SPIFFS 存储 Web 静态资源（HTML、CSS、JS），并利用 ArduinoJson 解析非易失性系统配置。  
* **多语言 Web 界面**：通过基于 JSON 的语言包支持界面本地化。

## **硬件需求**

1. **微控制器**：ESP32-S3 开发板（例如 ESP32-S3-DevKitC-1 或 DevKitM-1）  
2. **传感器**：  
   * AHT20 或 AHT10（温湿度传感器）  
   * BH1750（数字环境光传感器）  
   * Seeed Studio Grove \- Multichannel Gas Sensor V2（基于 GMXXX 的多通道气体传感器）  
3. **连接**：使用 I2C 连线 (SDA, SCL, VCC, GND) 将所有传感器连接到 ESP32-S3。

## **软件依赖**

本项目使用 PlatformIO 和 Arduino 框架构建。需要以下主要库（由 PlatformIO 自动管理）：

* ESPAsyncWebServer（用于异步 Web 服务）  
* AsyncTCP（Web 服务器所需的 TCP 网络库）  
* ArduinoJson（用于解析配置和序列化 WebSocket 数据负载）  
* PubSubClient（用于 OneNet MQTT 通信）  
* WebSockets（用于实时的客户端-服务器通信）  
* Adafruit Unified Sensor & Adafruit AHTX0（用于 AHT 温湿度传感器）  
* BH1750（用于光照传感器）  
* Seeed\_Arduino\_MultiGas（用于多通道气体传感器）

## **项目结构**

代码库采用了模块化结构，以提升可维护性并实现关注点分离：

├── data/                  \# Web 服务器文件 (HTML, CSS, JS, JSON)  
│   ├── index.html         \# 主仪表板  
│   ├── settings.html      \# 配置页面  
│   ├── script.js          \# 前端逻辑与 WebSocket 处理  
│   ├── chart.min.js       \# 图表库  
│   └── lang.json          \# 多语言支持配置  
├── src/  
│   ├── main.cpp           \# 程序入口和 RTOS 任务初始化  
│   ├── config.h           \# 全局配置参数和引脚定义  
│   ├── app\_globals.cpp/h  \# 全局变量和任务间通信队列  
│   ├── storage/           \# 非易失性存储管理  
│   └── tasks/             \# FreeRTOS 任务具体实现  
│       ├── task\_sensor    \# I2C 传感器数据采集  
│       ├── task\_wifi      \# WiFi 连接与重连逻辑  
│       ├── task\_web\_server \# 异步 Web 服务器和 WebSockets  
│       ├── task\_onenet    \# OneNet 平台的 MQTT 连接  
│       └── task\_system\_control \# 状态机和核心逻辑  
└── platformio.ini         \# PlatformIO 构建配置文件

## **安装与设置**

1. **克隆仓库**：将本项目下载或克隆到您的本地计算机。  
2. **在 PlatformIO 中打开**：使用安装了 PlatformIO 扩展的 Visual Studio Code 打开项目文件夹。  
3. **硬件接线**：将您的传感器连接到 src/config.h 中指定的 I2C 引脚。  
4. **上传文件系统镜像**：  
   * 您必须将 Web 文件上传到 ESP32 的闪存中。  
   * 在 PlatformIO 中，点击左侧边栏的 "PlatformIO" 图标。  
   * 在您的项目环境（例如 esp32-s3-devkitm-1）下，展开 Platform 菜单。  
   * 点击 Build Filesystem Image（构建文件系统），然后点击 Upload Filesystem Image（上传文件系统）。  
5. **编译和上传固件**：  
   * 点击 PlatformIO 底部工具栏中的 Upload 按钮（向右的箭头图标），编译固件并将其烧录到您的 ESP32-S3 中。

## **使用说明**

1. **首次启动**：在首次启动时，如果设备找不到已知的 WiFi 网络，它可能会回退到接入点 (AP) 模式（具体取决于您的 task\_wifi 配置）。  
2. **Web 界面**：  
   * 将您的电脑或智能手机连接到与 ESP32 相同的网络。  
   * 打开 Web 浏览器并访问 ESP32 的 IP 地址。  
   * 您将看到主仪表板，显示温度、湿度、光照和多种气体的实时图表。  
3. **设置与配置**：  
   * 导航到设置页面（通过 UI 导航栏或直接访问 /settings.html）。  
   * 在这里，您可以配置您的 WiFi 凭据、OneNet MQTT 参数（产品 ID、设备 ID、鉴权信息）以及数据上报时间间隔。  
   * 保存设置。ESP32 会将这些信息写入 SPIFFS 中的 config.json，可能需要重启设备才能生效。

## **系统架构详情**

该软件深度依赖 FreeRTOS 的特性：

* **任务 (Tasks)**：专用任务可确保阻塞操作（如连接 WiFi 或等待 MQTT 确认）不会使时间敏感的传感器读取或 WebSocket 广播陷入停滞。  
* **互斥锁/信号量 (Mutexes/Semaphores)**：使用 FreeRTOS 互斥锁保护共享资源（例如全局传感器数据结构和配置变量），以防止竞争条件和数据损坏。  
* **队列 (Queues)**：用于任务间通信，使传感器任务能够高效地将离散数据包传递给 Web 或 MQTT 任务。

## **许可证**

本项目采用 MIT 许可证。有关更多详细信息，请参阅存储库中的 LICENSE 文件。

MIT 许可证是一种简短且简单的宽容型许可证，其条件仅要求保留版权声明和许可声明。经过许可的作品、修改版以及更大型的作品可以在不同的条款下分发，且无需公开源代码。