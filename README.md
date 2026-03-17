# **ESP32-S3 RTOS Environmental Monitor**

[Chinese Version](https://github.com/ziyuan-404/ESP32-S3_RTOS_Environmental_Monitor/blob/main/README-CH.md)

## **Overview**

This project is a comprehensive, real-time environmental monitoring system based on the ESP32-S3 microcontroller. It leverages FreeRTOS to manage multiple concurrent tasks efficiently, ensuring stable sensor data acquisition, web server operation, and cloud IoT communication.

The system reads environmental data (temperature, humidity, light intensity, and various gas concentrations), displays the data on a highly responsive local web dashboard using WebSockets and Chart.js, and optionally uploads telemetry data to the OneNet IoT platform via MQTT.

## **Features**

* Real-Time Operating System: Utilizes FreeRTOS for robust multitasking (sensor reading, WiFi management, Web server, MQTT communication).  
* Multi-Sensor Integration: Reads temperature, humidity, ambient light, and multiple gases (NO2, C2H5OH, VOC, CO) via the I2C bus.  
* Dynamic Web Dashboard: Serves a local web interface from the ESP32 SPIFFS filesystem. The dashboard features real-time dynamic charts powered by Chart.js.  
* WebSocket Communication: Ensures low-latency, bi-directional communication between the web client and the ESP32 for live data updates.  
* Cloud IoT Integration: Supports MQTT connection to the OneNet IoT platform for remote data logging and monitoring.  
* Persistent Storage: Uses SPIFFS to store web assets (HTML, CSS, JS) and non-volatile system configurations parsed with ArduinoJson.  
* Multilingual Web Interface: Supports localization via a JSON-based language dictionary.

## **Hardware Requirements**

1. Microcontroller: ESP32-S3 Development Board (e.g., ESP32-S3-DevKitC-1 or DevKitM-1)  
2. Sensors:  
   * AHT20 or AHT10 (Temperature and Humidity Sensor)  
   * BH1750 (Digital Light Ambient Sensor)  
   * Seeed Studio Grove \- Multichannel Gas Sensor V2 (GMXXX based)  
3. Connectivity: I2C wiring (SDA, SCL, VCC, GND) connecting all sensors to the ESP32-S3.

## **Software Dependencies**

This project is built using PlatformIO and the Arduino framework. The following primary libraries are required (automatically managed via PlatformIO):

* ESPAsyncWebServer (for asynchronous web serving)  
* AsyncTCP (TCP networking required by the web server)  
* ArduinoJson (for configuration parsing and WebSocket payload serialization)  
* PubSubClient (for OneNet MQTT communication)  
* WebSockets (for real-time client-server communication)  
* Adafruit Unified Sensor & Adafruit AHTX0 (for AHT temp/humidity sensors)  
* BH1750 (for light sensing)  
* Seeed\_Arduino\_MultiGas (for the multichannel gas sensor)

## **Project Structure**

The codebase is structured to promote maintainability and separation of concerns:

├── data/                  \# Web server files (HTML, CSS, JS, JSON)  
│   ├── index.html         \# Main dashboard  
│   ├── settings.html      \# Configuration page  
│   ├── script.js          \# Frontend logic and WebSocket handling  
│   ├── chart.min.js       \# Charting library  
│   └── lang.json          \# Multi-language support configuration  
├── src/  
│   ├── main.cpp           \# Entry point and RTOS task initialization  
│   ├── config.h           \# Global configuration parameters and pin definitions  
│   ├── app\_globals.cpp/h  \# Global variables and inter-task communication queues  
│   ├── storage/           \# Non-volatile storage management  
│   └── tasks/             \# FreeRTOS task implementations  
│       ├── task\_sensor    \# I2C sensor data acquisition  
│       ├── task\_wifi      \# WiFi connection and reconnection logic  
│       ├── task\_web\_server\# Asynchronous web server and WebSockets  
│       ├── task\_onenet    \# MQTT connection to OneNet platform  
│       └── task\_system\_control \# State machine and core logic  
└── platformio.ini         \# PlatformIO build configuration

## **Installation and Setup**

1. Clone the Repository: Download or clone this project to your local machine.  
2. Open in PlatformIO: Open the project folder using Visual Studio Code with the PlatformIO extension installed.  
3. Hardware Wiring: Connect your sensors to the designated I2C pins defined in src/config.h.  
4. Upload Filesystem Image:  
   * You must upload the web files to the ESP32 flash memory.  
   * In PlatformIO, click on the "PlatformIO" icon in the left sidebar.  
   * Under your project environment (e.g., esp32-s3-devkitm-1), expand Platform.  
   * Click Build Filesystem Image and then Upload Filesystem Image.  
5. Build and Upload Firmware:  
   * Click the Upload button (right arrow icon) in the bottom PlatformIO toolbar to compile and flash the firmware to your ESP32-S3.

## **Usage**

1. Initial Boot: Upon first boot, if the device cannot find a known WiFi network, it may fallback to Access Point (AP) mode (depending on your specific task\_wifi configuration).  
2. Web Interface:  
   * Connect your computer or smartphone to the same network as the ESP32.  
   * Open a web browser and navigate to the ESP32's IP address.  
   * You will see the main dashboard displaying real-time charts for Temperature, Humidity, Light, and Gases.  
3. Settings Configuration:  
   * Navigate to the settings page (via the UI navigation or by accessing /settings.html).  
   * Here you can configure your WiFi credentials, OneNet MQTT parameters (Product ID, Device ID, Auth Info), and data reporting intervals.  
   * Save the settings. The ESP32 will write these to config.json in SPIFFS and may require a reboot to apply.

## **System Architecture Details**

The software relies heavily on FreeRTOS features:

* Tasks: Dedicated tasks ensure that blocking operations (like connecting to WiFi or waiting for an MQTT acknowledgement) do not halt the time-sensitive sensor readings or WebSocket broadcasting.  
* Mutexes/Semaphores: Shared resources, such as the global sensor data structures and configuration variables, are protected using FreeRTOS Mutexes to prevent race conditions and data corruption.  
* Queues: Used for inter-task communication, allowing the sensor task to pass discrete data packets to the Web or MQTT tasks efficiently.

## **License**

Please refer to the LICENSE file in the repository for details regarding the distribution and modification of this code.