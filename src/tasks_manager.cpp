#include "tasks_manager.h"
#include "wifi_manager.h"
#include "radar_manager.h"
#include "data_processor.h"
#include "emotion_analyzer_simple.h"
#include <BLEDevice.h>
#include <esp_task_wdt.h>

NetworkStatus currentNetworkStatus = NET_INITIAL;//当前网络状态，初始为初始网络状态
unsigned long lastBlinkTime = 0;//上次闪烁时间
bool ledState = false;//LED状态
int breatheValue = 0;//呼吸值
bool breatheIncreasing = true;//呼吸值是否递增
uint8_t WiFi_Connect_First_bit = 1;//WiFi连接状态位
uint64_t device_sn = 0;//设备SN，初始为0，后续从Flash中加载

PhysioDataProcessor* physioProcessor;//生理数据处理器
SimpleEmotionAnalyzer* emotionAnalyzer;//情感分析器

bool clearConfigRequested = false;//是否请求清除配置
bool forceLedOff = false;//是否强制关闭LED

/**
 * @brief 加载设备SN
 * 从Flash中读取保存的设备SN（支持64位雪花算法ID）
 */
void loadDeviceSN() {
    device_sn = preferences.getULong64("deviceSn", 0);
    Serial.printf("从Flash加载设备SN: %llu\n", device_sn);
}

/**
 * @brief 保存设备SN
 * 将设备SN保存到Flash中（支持64位雪花算法ID）
 */
void saveDeviceId() {
    preferences.putULong64("deviceSn", device_sn);//将设备SN保存到Flash中
    Serial.printf("设备SN已保存到Flash: %llu\n", device_sn);
}

/**
 * @brief 计算CRC32哈希值
 * @param data 输入数据指针
 * @param length 数据长度
 * @return CRC32哈希值
 */
uint32_t calculateCRC32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

/**
 * @brief 生成设备唯一标识哈希
 * 将 device_sn + MAC 地址拼接后计算 CRC32 哈希
 * @return 4字节哈希值
 */
uint32_t generateDeviceHash() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char macHex[13];
    snprintf(macHex, sizeof(macHex), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char snStr[21];
    snprintf(snStr, sizeof(snStr), "%llu", device_sn);

    String hashInput = String("SN") + String(snStr) + String("|") + String(macHex);

    uint32_t hash = calculateCRC32((const uint8_t*)hashInput.c_str(), hashInput.length());

    Serial.printf("🔐 [HASH] 输入: %s, 哈希: 0x%08X\n", hashInput.c_str(), hash);

    return hash;
}

/**
 * @brief 构建BLE厂商数据
 * 构造BLE广播厂商数据，包含FF FF标识和SN哈希值
 * @return 9字节厂商数据字符串
 */
std::string buildBLEManufacturerData() {
    std::string manufacturerData;
    manufacturerData.reserve(9);

    manufacturerData.push_back(static_cast<char>(0xFF));
    manufacturerData.push_back(static_cast<char>(0xFF));
    manufacturerData.push_back('R');
    manufacturerData.push_back(0x01);
    manufacturerData.push_back(0x00);

    uint32_t snHash = generateDeviceHash();
    manufacturerData.push_back(static_cast<char>((snHash >> 24) & 0xFF));
    manufacturerData.push_back(static_cast<char>((snHash >> 16) & 0xFF));
    manufacturerData.push_back(static_cast<char>((snHash >> 8) & 0xFF));
    manufacturerData.push_back(static_cast<char>(snHash & 0xFF));

    return manufacturerData;
}

/**
 * @brief 刷新BLE广播数据
 * 更新BLE广播的厂商数据和设备名称，使用SN码作为设备名
 */
void refreshBLEAdvertisingData() {
    BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    if (pAdvertising == nullptr) {
        Serial.println("⚠️ [BLE] 广播对象为空，无法刷新广播数据");
        return;
    }

    char snName[32];
    if (device_sn > 0) {
        snprintf(snName, sizeof(snName), "Radar_%llu", device_sn);
    } else {
        String macAddr = getDeviceMacAddress();
        macAddr.replace(":", "");
        snprintf(snName, sizeof(snName), "Radar_%s", macAddr.c_str());
    }

    BLEAdvertisementData advertisementData;
    advertisementData.setFlags(0x06);
    advertisementData.setCompleteServices(BLEUUID(SERVICE_UUID));
    advertisementData.setManufacturerData(buildBLEManufacturerData());

    BLEAdvertisementData scanResponseData;
    scanResponseData.setName(snName);

    pAdvertising->setAdvertisementData(advertisementData);
    pAdvertising->setScanResponseData(scanResponseData);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);

    Serial.printf("📡 [BLE] 已刷新广播 ManufacturerData, device_sn=%llu\n", device_sn);
}

/**
 * @brief 获取设备MAC地址
 * 读取WiFi STA接口的MAC地址并格式化为字符串
 * @return MAC地址字符串，格式为 XX:XX:XX:XX:XX:XX
 */
String getDeviceMacAddress() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

/**
 * @brief 设置网络状态
 * 更新当前网络状态，并重置呼吸灯参数
 * @param status 网络状态
 */
void setNetworkStatus(NetworkStatus status) {
    currentNetworkStatus = status;

    if (status == NET_CONNECTED) {
        breatheValue = BREATHE_MIN;
        breatheIncreasing = true;
    }
}

/**
 * @brief 清除存储的配置
 * 清除Flash中保存的WiFi配置和设备ID，重置WiFi连接状态
 */
void clearStoredConfig() {
    Serial.println("🧹 开始清除存储的配置...");

    uint16_t oldDeviceId = preferences.getUShort("deviceId", 0);

    preferences.remove("deviceId");
    preferences.remove("wifi_first");

    wifiManager.clearAllConfigs();

    Serial.println("✅ 配置已清除完成");
    Serial.printf("🗑️ 被清除的设备ID: %u\n", oldDeviceId);

    WiFi_Connect_First_bit = 1;

    WiFi.disconnect(true);
    setNetworkStatus(NET_DISCONNECTED);

    Serial.println("🔄 已清除Flash与内存中的配置，请重新配置WiFi和设备ID");

    if (deviceConnected) {
        sendStatusToBLE();
    }
}

/**
 * @brief BOOT按钮监控任务
 * 监控BOOT按钮按下事件，长按3秒清除存储的配置
 * @param parameter 任务参数（未使用）
 */
void bootButtonMonitorTask(void *parameter) {
    Serial.println("🔍 启动BOOT按钮监控任务...");

    pinMode(CONFIG_CLEAR_PIN, OUTPUT);
    digitalWrite(CONFIG_CLEAR_PIN, LOW);

    unsigned long buttonPressStartTime = 0;
    bool buttonPressed = false;

    while (1) {
        int buttonState = digitalRead(BOOT_BUTTON_PIN);

        if (buttonState == LOW && !buttonPressed) {
            buttonPressed = true;
            buttonPressStartTime = millis();
            Serial.println("⚠️ 检测到BOOT按钮按下，长按3秒将清除配置");

            digitalWrite(CONFIG_CLEAR_PIN, HIGH);
        }
        else if (buttonState == HIGH && buttonPressed) {
            if (!clearConfigRequested) {
                digitalWrite(CONFIG_CLEAR_PIN, LOW);
                Serial.println("❌ 按钮释放，取消清除操作");
            }
            buttonPressed = false;
        }

        if (buttonPressed && (millis() - buttonPressStartTime >= CLEAR_CONFIG_DURATION)) {
            if (!clearConfigRequested) {
                clearConfigRequested = true;
                forceLedOff = true;

                clearStoredConfig();

                Serial.println("🔄 系统即将重启...");

                vTaskDelay(1000 / portTICK_PERIOD_MS);
                digitalWrite(CONFIG_CLEAR_PIN, LOW);
                ledcWrite(0, 0);
                ESP.restart();
            }
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }
}

/**
 * @brief 睡眠分析任务
 * 每秒更新生理数据、运行睡眠状态机、输出睡眠状态到串口
 * @param parameter 任务参数（未使用）
 */
void sleepAnalysisTask(void *parameter) {
    SleepAnalyzer* sleepAnalyzer = new SleepAnalyzer();

    static unsigned long lastSleepAnalysisTime = 0;
    const unsigned long SLEEP_ANALYSIS_INTERVAL = 1000;
    static unsigned long lastStatsPrintTime = 0;
    const unsigned long STATS_PRINT_INTERVAL = 30000;

    while (1) {
        unsigned long currentTime = millis();

        if (currentTime - lastSleepAnalysisTime >= SLEEP_ANALYSIS_INTERVAL) {
            lastSleepAnalysisTime = currentTime;

            if (sensorData.heart_valid || sensorData.breath_valid) {
                float hr = sensorData.heart_valid ? sensorData.heart_rate : 0;
                float rr = sensorData.breath_valid ? sensorData.breath_rate : 0;

                if (hr > 0 || rr > 0) {
                    physioProcessor->update(hr, rr,
                        sensorData.heart_valid ? 80 : 0,
                        sensorData.breath_valid ? 80 : 0);

                    HeartRateData hrData = physioProcessor->getHeartRateData();
                    RespirationData rrData = physioProcessor->getRespirationData();
                    HRVEstimate hrvData = physioProcessor->getHRVEstimate();

                    BodyMovementData movementData;
                    memset(&movementData, 0, sizeof(BodyMovementData));
                    movementData.movement = sensorData.body_movement;
                    movementData.movementSmoothed = sensorData.body_movement;
                    movementData.movementMean = sensorData.body_movement;
                    movementData.activityLevel = sensorData.body_movement / 100.0f;
                    movementData.isValid = (sensorData.body_movement >= 0 && sensorData.body_movement <= 100);
                    movementData.timestamp = currentTime;

                    sleepAnalyzer->update(hrData, rrData, hrvData, movementData);

                    sleepAnalyzer->printState();

                    if (currentTime - lastStatsPrintTime >= STATS_PRINT_INTERVAL) {
                        lastStatsPrintTime = currentTime;
                        sleepAnalyzer->printStatistics();
                    }
                }
            }
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }
}

/**
 * @brief LED控制任务
 * 根据网络状态控制LED显示：断开时慢闪、连接中快闪、已连接时呼吸灯效果
 * @param parameter 任务参数（未使用）
 */
void ledControlTask(void *parameter) {
    Serial.println("💡 启动LED控制任务...");

    pinMode(NETWORK_LED_PIN, OUTPUT);
    digitalWrite(NETWORK_LED_PIN, LOW);
    ledcSetup(0, 5000, 8);
    ledcAttachPin(NETWORK_LED_PIN, 0);

    while (1) {
        if (forceLedOff) {
            ledcWrite(0, 0);
            vTaskDelay(10 / portTICK_PERIOD_MS);
            continue;
        }

        switch (currentNetworkStatus) {
            case NET_INITIAL:
            case NET_DISCONNECTED:
                if (millis() - lastBlinkTime >= SLOW_BLINK_INTERVAL) {
                    ledState = !ledState;
                    if(ledState) {
                        ledcWrite(0, 255);
                    } else {
                        ledcWrite(0, 0);
                    }
                    lastBlinkTime = millis();
                }
                break;

            case NET_CONNECTING:
                if (millis() - lastBlinkTime >= FAST_BLINK_INTERVAL) {
                    ledState = !ledState;
                    if(ledState) {
                        ledcWrite(0, 255);
                    } else {
                        ledcWrite(0, 0);
                    }
                    lastBlinkTime = millis();
                }
                break;

            case NET_CONNECTED:
                if (millis() - lastBlinkTime >= BREATHE_INTERVAL) {
                    ledcWrite(0, breatheValue);

                    if (breatheIncreasing) {
                        breatheValue += BREATHE_STEP;
                        if (breatheValue >= BREATHE_MAX) {
                            breatheValue = BREATHE_MAX;
                            breatheIncreasing = false;
                        }
                    } else {
                        breatheValue -= BREATHE_STEP;
                        if (breatheValue <= BREATHE_MIN) {
                            breatheValue = BREATHE_MIN;
                            breatheIncreasing = true;
                        }
                    }
                    lastBlinkTime = millis();
                }
                break;
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_START:
            setNetworkStatus(NET_INITIAL);
            break;

        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            setNetworkStatus(NET_CONNECTING);
            break;

        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            setNetworkStatus(NET_CONNECTED);
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            setNetworkStatus(NET_DISCONNECTED);
            break;

        case ARDUINO_EVENT_WIFI_STA_STOP:
            setNetworkStatus(NET_DISCONNECTED);
            break;
    }
}

/**
 * @brief WiFi监控任务
 * 初始化WiFi并定期打印连接状态和信号强度
 * @param parameter 任务参数（未使用）
 */
void wifiMonitorTask(void *parameter) {
    Serial.println("📡 WiFi监控任务启动");

    wifiManager.begin();

    if (wifiManager.getSavedNetworkCount() > 0) {
        Serial.printf("💾 检测到 %d 个已保存的WiFi配置，尝试连接...\n", wifiManager.getSavedNetworkCount());
        if (wifiManager.initializeWiFi()) {
            Serial.println("✅ WiFi连接成功！");
        } else {
            Serial.println("❌ WiFi连接失败，请通过BLE重新配置");
        }
    } else {
        Serial.println("⚠️ 未检测到WiFi配置，请通过BLE进行网络配置");
    }

    size_t wifi_first_len = preferences.getBytes("wifi_first", &WiFi_Connect_First_bit, sizeof(WiFi_Connect_First_bit));
    if (wifi_first_len == sizeof(WiFi_Connect_First_bit)) {
        Serial.printf("从Flash读取 WiFi_Connect_First_bit: %u\n", WiFi_Connect_First_bit);
    } else {
        Serial.println("Flash中无 wifi_first 条目，保留内存中原始值");
    }

    if(WiFi_Connect_First_bit == 0)
    {
        unsigned long wifiWaitStart = millis();
        unsigned long lastWifiWaitPrint = 0;
        const unsigned long WIFI_WAIT_TIMEOUT = 15000;
        while (WiFi.status() != WL_CONNECTED && (millis() - wifiWaitStart) < WIFI_WAIT_TIMEOUT) {
            if (millis() - lastWifiWaitPrint >= 1000) {
                lastWifiWaitPrint = millis();
            }
            yield();
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    Serial.println("📡 WiFi初始化完成，开始监控...");

    while(1) {
        static unsigned long lastPrint = 0;
        if (millis() - lastPrint > 30000) {
            Serial.printf("📡 WiFi状态: %d, RSSI: %d dBm\n",
                WiFi.status(), WiFi.RSSI());
            lastPrint = millis();
        }
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief BLE配置处理任务
 * 处理BLE配置命令，监控设备连接状态并管理广播
 * @param parameter 任务参数（未使用）
 */
void bleConfigTask(void *parameter) {
    Serial.println("📡 BLE配置处理任务启动");

    char snName[32];
    if (device_sn > 0) {
        snprintf(snName, sizeof(snName), "Radar_%llu", device_sn);
    } else {
        String macAddr = getDeviceMacAddress();
        macAddr.replace(":", "");
        snprintf(snName, sizeof(snName), "Radar_%s", macAddr.c_str());
    }
    BLEDevice::init(snName);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID,
                        BLECharacteristic::PROPERTY_READ |
                        BLECharacteristic::PROPERTY_WRITE |
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
    pCharacteristic->setCallbacks(new MyCallbacks());
    pCharacteristic->addDescriptor(new BLE2902());

    pService->start();
    refreshBLEAdvertisingData();
    BLEDevice::startAdvertising();

    Serial.println(String("✅ BLE已启动，设备名称: ") + snName);

    while(1) {
        processBLEConfig();

        if (!deviceConnected && oldDeviceConnected) {
            vTaskDelay(500 / portTICK_PERIOD_MS);
            pServer->startAdvertising();
            Serial.println("开始BLE广播");
            oldDeviceConnected = deviceConnected;
        }
        if (deviceConnected && !oldDeviceConnected) {
            oldDeviceConnected = deviceConnected;
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief 雷达命令发送任务
 * 每2秒轮流向雷达模组发送11条不同命令，查询心率、呼吸率、睡眠状态等数据
 * @param parameter 任务参数（未使用）
 */
void radarCmdTask(void *parameter) {
    Serial.println("📡 雷达命令发送任务启动");
    initR60ABD1();

    static const uint8_t radar_cmds[][3] = {
        {0x84, 0x81, 0x0F},  // 0x81: 查询心率/呼吸率
        {0x84, 0x8D, 0x0F},  // 0x8D: 查询睡眠状态
        {0x84, 0x8F, 0x0F},  // 0x8F: 查询体动数据
        {0x84, 0x8E, 0x0F},  // 0x8E: 查询人员存在
        {0x84, 0x91, 0x0F},  // 0x91: 查询呼吸波形
        {0x84, 0x92, 0x0F},  // 0x92: 查询呼吸波形(备用)
        {0x84, 0x83, 0x0F},  // 0x83: 查询心跳波形
        {0x84, 0x84, 0x0F},  // 0x84: 查询心跳波形(备用)
        {0x84, 0x85, 0x0F},  // 0x85: 查询心跳波形(扩展)
        {0x84, 0x86, 0x0F},  // 0x86: 查询心跳波形(扩展)
        {0x84, 0x90, 0x0F}   // 0x90: 查询综合状态
    };

    static size_t cmdIndex = 0;
    static unsigned long lastCmdMillis = 0;
    const unsigned long CMD_INTERVAL = 2000UL;

    while (1) {
        unsigned long now = millis();

        if (now - lastCmdMillis >= CMD_INTERVAL) {
            sendRadarCommand(
                radar_cmds[cmdIndex][0],
                radar_cmds[cmdIndex][1],
                radar_cmds[cmdIndex][2]
            );

            lastCmdMillis = now;
            cmdIndex++;
            if (cmdIndex >= sizeof(radar_cmds) / sizeof(radar_cmds[0])) {
                cmdIndex = 0;
            }
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }
}

/**
 * @brief 情绪分析任务
 * 每秒更新生理数据、校准基线、分析情绪并输出结果
 * @param parameter 任务参数（未使用）
 */
void emotionAnalysisTask(void *parameter) {
    physioProcessor = new PhysioDataProcessor();
    emotionAnalyzer = new SimpleEmotionAnalyzer(60);

    static unsigned long lastEmotionAnalysisTime = 0;
    const unsigned long EMOTION_ANALYSIS_INTERVAL = 1000;

    while (1) {
        unsigned long currentTime = millis();

        if (currentTime - lastEmotionAnalysisTime >= EMOTION_ANALYSIS_INTERVAL) {
            lastEmotionAnalysisTime = currentTime;

            if (sensorData.heart_valid || sensorData.breath_valid) {
                float hr = sensorData.heart_valid ? sensorData.heart_rate : 0;
                float rr = sensorData.breath_valid ? sensorData.breath_rate : 0;

                if (hr > 0 || rr > 0) {
                    physioProcessor->update(hr, rr,
                        sensorData.heart_valid ? 80 : 0,
                        sensorData.breath_valid ? 80 : 0);

                    HeartRateData hrData = physioProcessor->getHeartRateData();
                    RespirationData rrData = physioProcessor->getRespirationData();
                    HRVEstimate hrvData = physioProcessor->getHRVEstimate();

                    BodyMovementData movementData;
                    memset(&movementData, 0, sizeof(BodyMovementData));
                    movementData.movement = sensorData.body_movement;
                    movementData.movementSmoothed = sensorData.body_movement;
                    movementData.movementMean = sensorData.body_movement;
                    movementData.activityLevel = sensorData.body_movement / 100.0f;
                    movementData.isValid = (sensorData.body_movement >= 0 && sensorData.body_movement <= 100);
                    movementData.timestamp = currentTime;

                    if (hrData.isValid && rrData.isValid) {
                        emotionAnalyzer->calibrateBaseline(hrData, rrData, movementData);
                    }

                    EmotionResult emotionResult = emotionAnalyzer->analyze(hrData, rrData, hrvData, movementData);

                    if (emotionResult.isValid) {
                        Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
                        Serial.printf("主要情绪:%s (置信度: %.1f%%);",
                            EMOTION_NAMES[emotionResult.primaryEmotion],
                            emotionResult.confidence * 100);
                        Serial.printf("次要情绪: %s倾向\n",
                            EMOTION_NAMES[emotionResult.secondaryEmotion]);
                        Serial.printf("情绪强度:%.1f%% ", emotionResult.intensity * 100);
                        Serial.printf("效价:%.2f(负面到正面) ", emotionResult.valence);
                        Serial.printf("唤醒度:%.2f(平静到激动)\n", emotionResult.arousal);
                        Serial.printf("压力水平:%.1f ", emotionResult.stressLevel);
                        Serial.printf("焦虑水平:%.1f ", emotionResult.anxietyLevel);
                        Serial.printf("放松水平:%.1f ", emotionResult.relaxationLevel);
                        Serial.printf("交感神经活动:%.2f ", emotionResult.sympatheticActivity);
                        Serial.printf("副交感神经活动:%.2f\n", emotionResult.parasympatheticActivity);
                    }
                }
            }
        }

        vTaskDelay(50 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }
}

/**
 * @brief 初始化所有FreeRTOS任务
 * 创建并启动所有后台任务：BOOT按钮监控、LED控制、WiFi监控、MQTT、BLE配置、雷达命令发送、情绪分析、睡眠分析
 */
void initAllTasks() {
    loadDeviceSN();
    xTaskCreate(bootButtonMonitorTask, "Boot Button Monitor Task", 2048, NULL, 1, NULL);
    xTaskCreate(ledControlTask, "LED Control Task", 2048, NULL, 1, NULL);
    xTaskCreate(wifiMonitorTask, "WiFi Monitor Task", 4096, NULL, 2, NULL);
    xTaskCreatePinnedToCore(mqttTask, "MQTT Task", 8192, NULL, 2, &mqttTaskHandle, 1);
    xTaskCreate(bleConfigTask, "BLE Config Task", 4096, NULL, 1, NULL);
    xTaskCreate(radarCmdTask, "Radar Cmd Task", 2048, NULL, 2, NULL);
    xTaskCreate(emotionAnalysisTask, "Emotion Analysis Task", 4096, NULL, 1, NULL);
    xTaskCreate(sleepAnalysisTask, "Sleep Analysis Task", 4096, NULL, 1, NULL);
}
