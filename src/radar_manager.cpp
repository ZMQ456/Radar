#include "radar_manager.h"
#include "tasks_manager.h"
#include "wifi_manager.h"
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <string.h>

extern uint16_t currentDeviceId; // 当前设备ID
extern Preferences preferences; // Flash存储对象
extern WiFiManager wifiManager; // WiFi管理器对象

const int BAUD_RATE = 115200; // 串口波特率
const int UART1_RX = 11; // UART1接收新板引脚
const int UART1_TX = 10; // UART1发送新板引脚
//const int UART1_RX = 3; // UART1接收旧板引脚
//const int UART1_TX = 2; // UART1发送旧板引脚

const uint32_t PHASE_SEND_INTERVAL = 1; // 相位数据发送间隔（毫秒）
const uint32_t VITAL_SEND_INTERVAL = 10; // 生命体征数据发送间隔（毫秒）

const unsigned long SENSOR_TIMEOUT = 40000; // 传感器超时时间（毫秒）
static uint32_t packetCounter = 0; // 数据包计数器
static bool shouldSendOtherData = false; // 是否发送其他数据标志

const unsigned long SLEEP_DATA_INTERVAL = 5000; // 睡眠数据发送间隔（毫秒）

static int dnsFailCount = 0; // DNS解析失败计数器
const int DNS_FAIL_RESET_THRESHOLD = 3; // DNS失败重置网络阈值

void resetWiFiConnection(); // WiFi连接重置函数声明

//const char* influxDBHost = "8.134.11.76"; // InfluxDB服务器公网地址
const char* influxDBHost = "www.lmhrt.cn";  // InfluxDB服务器域名地址
const int influxDBPort = 8086; // InfluxDB服务器端口
const char* influxDBToken = "KuTa5ZsqoHIhi2IglOO06zExUYw1_mJ6K0mcA9X1y6O6CJDog3_Cgr8mUw1SwpuCCKRElqxa6wAhrrhsYPytkg=="; // InfluxDB访问令牌
const char* influxDBOrg = "gzlg"; // InfluxDB组织名称
const char* influxDBBucket = "gzlg"; // InfluxDB存储桶名称

uint8_t presence_Bit = 1; // 存在标志位

SensorData sensorData; // 传感器数据结构体
HardwareSerial mySerial1(1); // 硬件串口1对象

QueueHandle_t phaseDataQueue; // 相位数据队列句柄
QueueHandle_t vitalDataQueue; // 生命体征数据队列句柄
QueueHandle_t uartQueue; // UART数据队列句柄
TaskHandle_t bleSendTaskHandle = NULL; // BLE发送任务句柄
TaskHandle_t vitalSendTaskHandle = NULL; // 生命体征发送任务句柄
TaskHandle_t uartProcessTaskHandle = NULL; // UART处理任务句柄

BLEServer* pServer = NULL; // BLE服务器指针
BLECharacteristic* pCharacteristic = NULL; // BLE特征值指针

bool deviceConnected = false; // 设备连接状态
bool oldDeviceConnected = false; // 旧设备连接状态
QueueHandle_t bleCommandQueue = nullptr; // BLE命令队列

bool continuousSendEnabled = false; // 持续发送使能标志
unsigned long continuousSendInterval = 500; // 持续发送间隔（毫秒）
BLEFlowController bleFlow(500); // BLE流控制器对象
SemaphoreHandle_t bleSendMutex; // BLE发送互斥锁

BleProto::FrameParser bleFrameParser; // BLE帧解析器（TLV协议）
uint8_t bleSequenceCounter = 0;       // BLE出站帧序列号计数器

// BLE命令处理常量
static const size_t MAX_LEGACY_JSON_LEN = sizeof(BleCommandMessage::json) - 1; // legacy JSON兼容队列限制，不是TLV协议限制

unsigned long lastSensorUpdate = 0; // 上次传感器更新时间
LastSentData lastSentData = {0}; // 上次发送的数据，初始化为0
unsigned long lastCheckTime = 0; // 上次检测时间，初始化为0

/**
 * @brief BLE流控制器构造函数
 * 初始化BLE数据流控制参数，限制数据发送速率
 * @param maxBps 最大每秒发送字节数
 */
BLEFlowController::BLEFlowController(size_t maxBps) : maxBytesPerSecond(maxBps), bytesSent(0) {
    lastResetTime = millis();
    lastSendTime = 0;
}

/**
 * @brief 检查是否可以发送数据
 * 检查当前是否满足发送条件，包括速率限制和时间间隔
 * @param dataSize 要发送的数据大小
 * @return 是否可以发送数据
 */
bool BLEFlowController::canSend(size_t dataSize) {
    unsigned long currentTime = millis();
    
    if(currentTime - lastResetTime >= 1000) {
        bytesSent = 0;
        lastResetTime = currentTime;
    }
    
    if((bytesSent + dataSize) > maxBytesPerSecond) {
        return false;
    }
    
    if(currentTime - lastSendTime < 5) {
        return false;
    }
    
    return true;
}

/**
 * @brief 检查发送时间间隔
 * 检查距离上次发送是否满足最小时间间隔
 * @return 是否满足发送条件
 */
bool BLEFlowController::check() {
    unsigned long currentTime = millis();
    if(currentTime - lastSendTime < 5) {
        return false;
    }
    return true;
}

/**
 * @brief 记录数据发送
 * 更新已发送字节数和最后发送时间
 * @param dataSize 已发送的数据大小
 */
void BLEFlowController::recordSend(size_t dataSize) {
    bytesSent += dataSize;
    lastSendTime = millis();
}

/**
 * @brief 重置流控制器
 * 重置已发送字节数和时间戳
 */
void BLEFlowController::reset() {
    bytesSent = 0;
    lastResetTime = millis();
    lastSendTime = 0;
}

/**
 * @brief BLE服务器连接回调
 * 当客户端连接时触发
 * @param pServer BLE服务器指针
 */
void MyServerCallbacks::onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("✅ [BLE] 客户端已连接");
}

/**
 * @brief BLE服务器断开连接回调
 * 当客户端断开连接时触发
 * @param pServer BLE服务器指针
 */
void MyServerCallbacks::onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("🔴 [BLE] 客户端已断开");
    continuousSendEnabled = false;
}

/**
 * @brief BLE特征值写入回调
 * 当客户端写入数据时触发
 * @param pCharacteristic BLE特征值指针
 */
void MyCallbacks::onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.empty()) {
        return;
    }

    // 队列未初始化，直接丢弃
    if (bleCommandQueue == nullptr) {
        Serial.println("[BLE] 警告：命令队列为空，丢弃命令");
        return;
    }

    BleProto::Frame frame;
    bool parsed = bleFrameParser.input(
        reinterpret_cast<const uint8_t*>(value.data()),
        value.size(),
        frame
    );

    while (parsed) {
        String legacyJson;
        if (BleProto::decodeFrameToLegacyJson(frame, legacyJson)) {
            Serial.printf("[BLE] 收到TLV命令，转旧JSON: %s\n", legacyJson.c_str());
        } else {
            Serial.printf("[BLE] 未识别CMD: 0x%02X\n", frame.cmd);
            legacyJson = "{\"command\":\"unknown\"}";
        }

        // 检查消息长度，避免截断
        if (legacyJson.length() > MAX_LEGACY_JSON_LEN) {
            Serial.printf("[BLE] 命令过长(%d > %d)，静默丢弃（避免回调中同步响应）\n", 
                         legacyJson.length(), MAX_LEGACY_JSON_LEN);
            parsed = bleFrameParser.input(nullptr, 0, frame);
            continue;
        }

        BleCommandMessage msg = {};
        legacyJson.toCharArray(msg.json, sizeof(msg.json));
        
        // 检查队列是否已满
        if (xQueueSend(bleCommandQueue, &msg, 0) != pdTRUE) {
            Serial.println("[BLE] 命令队列已满，静默丢弃（避免回调中同步响应）");
        }

        parsed = bleFrameParser.input(nullptr, 0, frame);
    }
}

/**
 * @brief 初始化雷达管理器
 * 初始化队列和FreeRTOS任务
 */
void initRadarManager() {
    Serial.println("🔧 初始化雷达管理器...");

    phaseDataQueue = xQueueCreate(QUEUE_SIZE, sizeof(PhaseData));
    vitalDataQueue = xQueueCreate(QUEUE_SIZE, sizeof(VitalData));
    uartQueue = xQueueCreate(2048, sizeof(char));

    if (phaseDataQueue == NULL || vitalDataQueue == NULL || uartQueue == NULL) {
        Serial.println("❌ 队列创建失败");
    } else {
        Serial.println("✅ FreeRTOS队列创建成功");
    }

    bleSendMutex = xSemaphoreCreateMutex();
    if (bleSendMutex == NULL) {
        Serial.println("❌ BLE发送互斥锁创建失败");
    } else {
        Serial.println("✅ BLE发送互斥锁创建成功");
    }

    bleCommandQueue = xQueueCreate(10, sizeof(BleCommandMessage));
    if (bleCommandQueue == NULL) {
        Serial.println("❌ BLE命令队列创建失败");
    } else {
        Serial.println("✅ BLE命令队列创建成功");
    }

    xTaskCreatePinnedToCore(
        bleSendTask,
        "BleSendTask",
        TASK_STACK_SIZE,
        NULL,
        3,
        &bleSendTaskHandle,
        1
    );
    
    xTaskCreatePinnedToCore(
        vitalSendTask,
        "VitalSendTask",
        TASK_STACK_SIZE,
        NULL,
        2,
        &vitalSendTaskHandle,
        1
    );
    
    xTaskCreatePinnedToCore(
        radarDataTask,
        "RadarProcessTask",
        4096,
        NULL,
        4,
        NULL,
        1
    );
    
    xTaskCreatePinnedToCore(
        uartProcessTask,
        "UartProcessTask",
        4096,
        NULL,
        5,
        &uartProcessTaskHandle,
        1
    );
    
    Serial.println("✅ 雷达管理器初始化完成");
}

/**
 * @brief 初始化R60ABD1雷达模组
 * 发送初始化命令激活雷达数据上报功能
 */
void initR60ABD1() {
    Serial.println("🔧 初始化R60ABD1雷达模组...");
    
    Serial.println("📡 发送查询指令以激活数据上报...");
    uint8_t queryPresenceCmd[] = {0x53, 0x59, 0x80, 0x81, 0x00, 0x01, 0x00, 0x7D, 0x54, 0x43};
    mySerial1.write(queryPresenceCmd, sizeof(queryPresenceCmd));

    Serial.println("📡 开启核心监测功能...");
    
    sendRadarCommand(0x80, 0x00, 0x01);
    delay(50);
    sendRadarCommand(0x81, 0x00, 0x01);
    delay(50);
    sendRadarCommand(0x85, 0x00, 0x01);
    delay(50);
    sendRadarCommand(0x84, 0x00, 0x01);
    delay(50);
    
    Serial.println("📡 尝试开启波形数据...");
    sendRadarCommand(0x81, 0x0C, 0x01);
    delay(50);
    sendRadarCommand(0x85, 0x0A, 0x01);
    delay(50);

    sendRadarCommand(0x84, 0x13, 0x01);
    delay(50);
    sendRadarCommand(0x84, 0x14, 0x01);
    delay(50);
    
    Serial.println("🔍 查询当前状态...");
    sendRadarCommand(0x80, 0x80, 0x0F);
    delay(50);
    sendRadarCommand(0x81, 0x80, 0x0F);
    delay(50);
    sendRadarCommand(0x85, 0x80, 0x0F);
    delay(50);
    sendRadarCommand(0x84, 0x80, 0x0F);
    
    Serial.println("✅ R60ABD1雷达初始化完成");
}

/**
 * @brief 解析R60ABD1雷达数据帧
 * 解析雷达返回的数据帧并提取传感器数据
 * @param frame 数据帧指针
 * @param frameLen 数据帧长度
 * @return 是否解析成功
 */
bool parseR60ABD1Frame(uint8_t *frame, uint16_t frameLen) {
    if(frameLen < 8) return false;
    
    if(frame[0] != FRAME_HEADER1 || frame[1] != FRAME_HEADER2 ||
       frame[frameLen-2] != FRAME_TAIL1 || frame[frameLen-1] != FRAME_TAIL2) {
        return false;
    }
    
    uint8_t checksum = 0;
    for(int i = 0; i < frameLen-3; i++) {
        checksum += frame[i];
        if(i % 50 == 0) {
            esp_task_wdt_reset();
        }
    }
    if(checksum != frame[frameLen-3]) {
        Serial.println("❌ R60ABD1帧校验和错误");
        return false;
    }
    
    for(int i = 0; i < frameLen && i < 20; i++) {
    }
    if(frameLen > 20) Serial.print("... ");
    
    uint8_t ctrlByte = frame[2];
    uint8_t cmdByte = frame[3];
    uint16_t dataLen = (frame[4] << 8) | frame[5];

    switch(ctrlByte) {
        case CTRL_PRESENCE:
            switch(cmdByte) {
                // 开关人体存在监测
                case 0x00:
                case 0x80:
                    if(dataLen >= 1) {
                        if(frame[6] == 0x01) {
                            sendRadarCommand(0x84, 0x00, 0x01);  // 睡眠监测
                        }
                    }
                    break;

                case 0x01:
                    if(dataLen >= 1) {
                        sensorData.presence = frame[6];  // 0:无人, 1:有人
                        Serial.printf("👤 人体存在: %s\n", sensorData.presence ? "有人" : "无人");
                    }
                    break;
                
                case 0x02:
                    if(dataLen >= 1) {
                        sensorData.motion = frame[6];  // 0:无, 1:静止, 2:活跃
                        const char* states[] = {"无", "静止", "活跃"};
                        Serial.printf("🏃 运动状态: %s\n", states[sensorData.motion]);
                    }
                    break;
                
                case 0x03:
                    if(dataLen >= 1) {
                        sensorData.body_movement = frame[6];  // 0-100
                        Serial.printf("📊体动参数: %d\n", sensorData.body_movement);
                    }
                    break;
                
                case 0x04:
                    if(dataLen >= 2) {
                        sensorData.distance = ((uint16_t)frame[6] << 8) | frame[7];
                        Serial.printf("📏人体距离: %d cm\n", sensorData.distance);
                    }
                    break;
                
                case 0x05:
                    if(dataLen >= 6) {
                        uint16_t x_raw = ((uint16_t)frame[6] << 8) | frame[7];
                        sensorData.pos_x = parseSignedCoordinate(x_raw);
                        
                        uint16_t y_raw = ((uint16_t)frame[8] << 8) | frame[9];
                        sensorData.pos_y = parseSignedCoordinate(y_raw);
                        
                        uint16_t z_raw = ((uint16_t)frame[10] << 8) | frame[11];
                        sensorData.pos_z = parseSignedCoordinate(z_raw);
                    }
                    break;
                
                default:
                    Serial.printf("❓未知的0x80命令字: 0x%02X\n", cmdByte);
                    break;
            }
            break;
            
        case CTRL_BREATH:
            switch(cmdByte) {
                case 0x00:
                case 0x80:
                    if(dataLen >= 1) {
                    }
                    break;
                   
                case 0x01:
                    if(dataLen >= 1) {
                        sensorData.breath_status = frame[6];
                    }
                    break;
                
                case 0x02:
                    if(dataLen >= 1) {
                        sensorData.breath_rate = (float)frame[6];
                        sensorData.breath_valid = (sensorData.breath_rate >= 0.0f && 
                                                 sensorData.breath_rate <= 35.0f);
                    }
                    break;
                
                case 0x05:
                    if(dataLen >= 5) {
                        for(int i = 0; i < 5 && i < dataLen; i++) {
                            sensorData.breath_waveform[i] = (int8_t)(frame[6+i] - 128);
                        }
                    }
                    break;
                
                default:
                    break;
            }
            break;
            
        case CTRL_HEARTRATE:
            switch(cmdByte) {
                case 0x00:
                case 0x80:
                    if(dataLen >= 1) {
                    }
                    break;
                case 0x02:
                    if(dataLen >= 1) {
                        sensorData.heart_rate = (float)frame[6];
                        sensorData.heart_valid = (sensorData.heart_rate >= 60.0f && 
                                                sensorData.heart_rate <= 120.0f);
                    }
                    break;
                
                case 0x05:
                    if(dataLen >= 5) {
                        for(int i = 0; i < 5 && i < dataLen; i++) {
                            sensorData.heart_waveform[i] = (int8_t)(frame[6+i] - 128);
                        }
                    }
                    break;
                
                default:
                    break;
            }
            break;
            
        case CTRL_SLEEP:
            switch(cmdByte) {
                case 0x00:
                case 0x80:
                    if(dataLen >= 1) {
                    }
                    break;  
                
                case 0x01:
                case 0x81:
                    if(dataLen >= 1) {
                        sensorData.bed_status = frame[6];
                    }
                    break;

                case 0x03:
                case 0x83:
                    if(dataLen >= 2) {
                        sensorData.awake_time = ((uint16_t)frame[6] << 8) | (uint16_t)frame[7];
                    }
                    break;
                
                case 0x04:
                case 0x84:
                    if(dataLen >= 2) {
                        sensorData.light_sleep_time = ((uint16_t)frame[6] << 8) | (uint16_t)frame[7];
                    }
                    break;
                
                case 0x05:
                case 0x85:
                    if(dataLen >= 2) {
                        sensorData.deep_sleep_time = ((uint16_t)frame[6] << 8) | (uint16_t)frame[7];
                    }
                    break;
                
                case 0x06:
                    if(dataLen >= 1) {
                        sensorData.sleep_score = frame[6];
                    }
                    break;

                case 0x86:
                    if(dataLen >= 2) {
                        sensorData.sleep_score = frame[6];
                    }
                    break;

                case 0x0C:
                case 0x8D:
                    if(dataLen >= 8) {
                        sensorData.presence = frame[6];
                        sensorData.sleep_state = frame[7];
                        sensorData.avg_breath_rate = frame[8];
                        sensorData.avg_heart_rate = frame[9];
                        sensorData.turnover_count = frame[10];
                        sensorData.large_move_ratio = frame[11];
                        sensorData.small_move_ratio = frame[12];
                        sensorData.apnea_count = frame[13];
                    }
                    break;
                
                case 0x0D:
                case 0x8F:
                    if(dataLen >= 12) {
                        sensorData.sleep_score = frame[6];
                        sensorData.sleep_total_time = ((uint16_t)frame[7] << 8) | (uint16_t)frame[8];
                        
                        sensorData.awake_ratio = frame[9];
                        sensorData.light_sleep_ratio = frame[10];
                        sensorData.deep_sleep_ratio = frame[11];
                        
                        sensorData.bed_Out_Time = frame[12];
                        sensorData.turn_count = frame[13];
                        sensorData.turnover_count = frame[14];
                        sensorData.avg_breath_rate = frame[15];
                        sensorData.avg_heart_rate = frame[16];
                        sensorData.apnea_count = frame[17];
                    }
                    break;
                
                case 0x0E:
                case 0x8E:
                    if(dataLen >= 1) {
                        sensorData.abnormal_state = frame[6];
                    }
                    break;
                
                case 0x10:
                case 0x90:
                    if(dataLen >= 1) {
                        sensorData.sleep_grade = frame[6];
                    }
                    break;
                
                case 0x11:
                case 0x91:
                    if(dataLen >= 1) {
                        sensorData.struggle_alert = frame[6];
                    }
                    break;
                
                case 0x12:
                case 0x92:
                    if(dataLen >= 1) {
                        sensorData.no_one_alert = frame[6];
                    }
                    break;
                
                default:
                    break;
            }
            break;
            
        case 0x07:
            if(dataLen >= 1) {
            }
            break;
            
        default:
            Serial.printf("❓未知控制字: 0x%02X\n", ctrlByte);
            break;
    }

    lastSensorUpdate = millis();
    
    sensorData.heart_valid = (sensorData.heart_rate > 0 && sensorData.heart_rate < 200);
    sensorData.breath_valid = (sensorData.breath_rate >= 0.1f && sensorData.breath_rate <= 60.0f);

    if( sensorData.heart_valid ==1 && sensorData.heart_valid == 1 && presence_Bit == 1 )
    {
        sensorData.presence = 1;
        presence_Bit = 0;
    }
    
    return true;
}

int16_t parseSignedCoordinate(uint16_t raw_value) {
    bool is_negative = (raw_value & 0x8000) != 0;
    uint16_t magnitude = raw_value & 0x7FFF;
    
    int16_t result = (int16_t)magnitude;
    if (is_negative) {
        result = -result;
    }
    
    return result;
}

/**
 * @brief 发送雷达命令
 * 构造并发送控制命令到雷达模组
 * @param ctrl 控制字节
 * @param cmd 命令字节
 * @param value 值字节
 */
void sendRadarCommand(uint8_t ctrl, uint8_t cmd, uint8_t value) {
    uint8_t command[10];
    command[0] = 0x53;
    command[1] = 0x59;
    command[2] = ctrl;
    command[3] = cmd;
    command[4] = 0x00;
    command[5] = 0x01;
    command[6] = value;
    
    uint8_t checksum = 0;
    for(int i = 0; i < 7; i++) {
        checksum += command[i];
    }
    command[7] = checksum;
    
    command[8] = 0x54;
    command[9] = 0x43;
    
    mySerial1.write(command, 10);
}

void IRAM_ATTR serialRxCallback() {
    if (uartQueue != NULL) {
        while(mySerial1.available()) {
            char c = mySerial1.read();
            
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xQueueSendFromISR(uartQueue, &c, &xHigherPriorityTaskWoken);
            
            if(xHigherPriorityTaskWoken) {
                portYIELD_FROM_ISR();
            }
        }
    }
}

/**
 * @brief 检查数据是否发生变化
 * 比较当前传感器数据与上次发送的数据，判断是否需要发送更新
 * @return 是否发生变化
 */
bool isDataChanged() {
    // 心率变化阈值：0.5
    if (fabs(sensorData.heart_rate - lastSentData.heart_rate) > 0.5) {
        return true;
    }
    
    // 呼吸率变化阈值：0.5
    if (fabs(sensorData.breath_rate - lastSentData.breath_rate) > 0.5) {
        return true;
    }
    
    // 存在状态变化
    if (sensorData.presence != lastSentData.presence) {
        return true;
    }
    
    // 运动状态变化
    if (sensorData.motion != lastSentData.motion) {
        return true;
    }
    
    // 睡眠状态变化
    if (sensorData.sleep_state != lastSentData.sleep_state) {
        return true;
    }
    
    return false;
}

/**
 * @brief BLE数据发送任务
 * 实现基于数据变化和定时检测的发送机制
 * 1. 定时检测数据变化（基于continuousSendInterval）
 * 2. 检测到变化时立即发送数据
 * 3. 发送后更新lastSentData为当前数据
 * @param parameter 任务参数（未使用）
 */
void bleSendTask(void *parameter) {
    Serial.println("🔁 R60ABD1 BLE data send task started");
    
    while (1) {
        esp_task_wdt_reset();
        
        // 检查是否需要进行数据检测
        if (continuousSendEnabled && deviceConnected) {
            unsigned long currentTime = millis();
            
            // 按照设定的时间间隔进行检测
            if (currentTime - lastCheckTime >= continuousSendInterval) {
                lastCheckTime = currentTime;
                
                // 检查数据是否发生变化
                if (isDataChanged()) {
                    // 构建JSON数据
                    JsonDocument doc;
                    doc["method"] = "ble.event.data.post";
                    doc["deviceId"] = getDeviceMacAddress();
                    doc["reportType"] = "radar";
                    doc["success"] = true;
                    
                    JsonObject params = doc["params"].to<JsonObject>();
                    
                    if (sensorData.presence > 0) {
                        params["heartRate"] = sensorData.heart_rate;
                        params["breathingRate"] = sensorData.breath_rate;
                        params["heartbeatWaveform"] = (int)sensorData.heart_waveform[0];
                        params["breathingWaveform"] = (int)sensorData.breath_waveform[0];
                        params["personDetected"] = sensorData.presence;
                        params["humanActivity"] = sensorData.motion;
                        params["sleepState"] = sensorData.sleep_state;
                        params["humanDistance"] = sensorData.distance;
                        params["humanPositionX"] = sensorData.pos_x;
                        params["humanPositionY"] = sensorData.pos_y;
                        params["humanPositionZ"] = sensorData.pos_z;
                        params["timestamp"] = currentTime;
                    } else {
                        params["heartRate"] = 0.0;
                        params["breathingRate"] = 0.0;
                        params["heartbeatWaveform"] = 0;
                        params["breathingWaveform"] = 0;
                        params["personDetected"] = 0;
                        params["humanActivity"] = 0;
                        params["sleepState"] = 0;
                        params["humanDistance"] = 0;
                        params["timestamp"] = currentTime;
                    }
                    
                    // 序列化为JSON字符串
                    String jsonStr;
                    serializeJson(doc, jsonStr);

                    // 统一走 TLV 帧封装路径，不再裸切 JSON
                    sendJSONDataToBLE(jsonStr);

                    lastSentData.heart_rate = sensorData.heart_rate;
                    lastSentData.breath_rate = sensorData.breath_rate;
                    lastSentData.presence = sensorData.presence;
                    lastSentData.motion = sensorData.motion;
                    lastSentData.sleep_state = sensorData.sleep_state;
                }
            }
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }
}

bool isInRefusedCooldown();
unsigned long getRefusedCooldownRemaining();
void updateRefusedTime();

/**
 * @brief 生命体征数据发送任务
 * 从队列中获取生命体征数据并发送到InfluxDB数据库
 * @param parameter 任务参数（未使用）
 */
void vitalSendTask(void *parameter) {
    Serial.println("🔁🔁 生命体征数据发送任务启动（WiFi数据库传输）");
    
    unsigned long lastSleepDataTime = 0;
    const unsigned long SLEEP_DATA_INTERVAL = 5000;
    
    VitalData cachedData = {0};
    bool hasCachedData = false;
    unsigned long lastSendTime = 0;
    const unsigned long FORCE_SEND_INTERVAL = 60000; // 每分钟强制发送一次
    const unsigned long MIN_SEND_INTERVAL = 1500; // 最小发送间隔1秒
    VitalData pendingData = {0};
    bool hasPendingData = false;
    
    while (1) {
        VitalData vitalData;
        
        if (xQueueReceive(vitalDataQueue, &vitalData, portMAX_DELAY) == pdTRUE) {
            esp_task_wdt_reset();
            
            if (WiFi.status() == WL_CONNECTED) {
                if (vitalData.heart_rate == 0 || vitalData.breath_rate == 0) {
                    Serial.printf("⚠️ 心率=%.1f, 呼吸率=%.1f，跳过发送数据到数据库\n", 
                        vitalData.heart_rate, vitalData.breath_rate);
                    continue;
                }
                
                if (vitalData.heart_rate > 200 || vitalData.breath_rate > 50 || 
                    vitalData.heart_rate < 0 || vitalData.breath_rate < 0) {
                    Serial.printf("❌ 数据异常 - 心率:%.1f, 呼吸:%.1f，丢弃\n",
                        vitalData.heart_rate, vitalData.breath_rate);
                    continue;
                }
                
                bool dataChanged = false;
                bool shouldSend = false;
                
                if (!hasCachedData) {
                    dataChanged = true;
                    shouldSend = true;
                    Serial.printf("📊 首次数据 - 心率:%.1f, 呼吸:%.1f, 距离:%d\n",
                        vitalData.heart_rate, vitalData.breath_rate, vitalData.distance);
                } else {
                    if (memcmp(&vitalData, &cachedData, sizeof(VitalData)) != 0) {
                        dataChanged = true;
                        shouldSend = true;
                        
                        if (fabs(vitalData.heart_rate - cachedData.heart_rate) >= 1.0f ||
                            fabs(vitalData.breath_rate - cachedData.breath_rate) >= 1.0f) {
                            Serial.printf("📊 数据变化: 心率 %.1f->%.1f, 呼吸 %.1f->%.1f, 距离 %d->%d, 状态 %d->%d\n",
                                cachedData.heart_rate, vitalData.heart_rate,
                                cachedData.breath_rate, vitalData.breath_rate,
                                cachedData.distance, vitalData.distance,
                                cachedData.sleep_state, vitalData.sleep_state);
                        } else {
                            Serial.printf("📊 数据变化: 距离 %d->%d, 状态 %d->%d, 运动 %d->%d, 异常 %d->%d\n",
                                cachedData.distance, vitalData.distance,
                                cachedData.sleep_state, vitalData.sleep_state,
                                cachedData.motion, vitalData.motion,
                                cachedData.abnormal_state, vitalData.abnormal_state);
                        }
                    }
                }
                
                unsigned long currentTime = millis();
                if (!shouldSend && (currentTime - lastSendTime >= FORCE_SEND_INTERVAL)) {
                    shouldSend = true;
                    Serial.printf("⏰ 定时发送触发 (距上次 %d 秒)\n", (currentTime - lastSendTime) / 1000);
                }
                
                cachedData = vitalData;
                hasCachedData = true;
                
                if (shouldSend) {
                    unsigned long timeSinceLastSend = millis() - lastSendTime;
                    
                    if (isInRefusedCooldown()) {
                        Serial.printf("⏳ 连接被拒绝冷却中，等待 %d 秒\n", getRefusedCooldownRemaining() / 1000);
                        if (timeSinceLastSend < MIN_SEND_INTERVAL && lastSendTime > 0) {
                            pendingData = vitalData;
                            hasPendingData = true;
                        }
                    } else if (timeSinceLastSend < MIN_SEND_INTERVAL && lastSendTime > 0) {
                        pendingData = vitalData;
                        hasPendingData = true;
                        Serial.printf("⏳ 发送间隔不足 (%d ms)，缓存最新数据等待 %d ms\n", 
                            (int)timeSinceLastSend, MIN_SEND_INTERVAL - (int)timeSinceLastSend);
                    } else {
                        Serial.printf("📡 发送数据 - 心率:%.1f, 呼吸:%.1f, 距离:%d\n",
                            vitalData.heart_rate, vitalData.breath_rate, vitalData.distance);
                    
                    String dailyDataLine = "daily_data,deviceId=" + String(currentDeviceId) + ",dataType=daily ";
                    
                    bool firstField = true;
                    
                    if (vitalData.heart_rate > 0) {
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "heartRate=" + String(vitalData.heart_rate, 1);
                        firstField = false;
                    }
                    
                    if (vitalData.breath_rate > 0) {
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "breathingRate=" + String(vitalData.breath_rate, 1);
                        firstField = false;
                    }
                    
                    if (!firstField) dailyDataLine += ",";
                    dailyDataLine += "personDetected=" + String(vitalData.presence) + "i";
                    firstField = false;
                    
                    if (!firstField) dailyDataLine += ",";
                    dailyDataLine += "humanActivity=" + String(vitalData.motion) + "i";
                    firstField = false;
                    
                    if (vitalData.distance > 0) {
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "humanDistance=" + String(vitalData.distance) + "i";
                        firstField = false;
                    }
                    
                    if (vitalData.sleep_state >= 0) {
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "sleepState=" + String(vitalData.sleep_state) + "i";
                        firstField = false;
                    }
                    
                    if (!firstField) dailyDataLine += ",";
                    dailyDataLine += "humanPositionX=" + String(vitalData.pos_x) + "i";
                    firstField = false;
                    
                    if (!firstField) dailyDataLine += ",";
                    dailyDataLine += "humanPositionY=" + String(vitalData.pos_y) + "i";
                    firstField = false;
                    
                    if (!firstField) dailyDataLine += ",";
                    dailyDataLine += "humanPositionZ=" + String(vitalData.pos_z) + "i";
                    firstField = false;
                    
                    if (!firstField) dailyDataLine += ",";
                    dailyDataLine += "heartbeatWaveform=" + String((int)sensorData.heart_waveform[0]) + "i";
                    firstField = false;
                    
                    if (!firstField) dailyDataLine += ",";
                    dailyDataLine += "breathingWaveform=" + String((int)sensorData.breath_waveform[0]) + "i";
                    firstField = false;
                    
                    if (!firstField) dailyDataLine += ",";
                    dailyDataLine += "abnormalState=" + String(vitalData.abnormal_state) + "i";
                    firstField = false;
                    
                    if (!firstField) dailyDataLine += ",";
                    dailyDataLine += "bedStatus=" + String(vitalData.bed_status) + "i";
                    firstField = false;
                    
                    if (!firstField) dailyDataLine += ",";
                    dailyDataLine += "struggleAlert=" + String(vitalData.struggle_alert) + "i";
                    firstField = false;
                    
                    if (!firstField) dailyDataLine += ",";
                    dailyDataLine += "noOneAlert=" + String(vitalData.no_one_alert) + "i";
                    firstField = false;
                    
                    if (!dailyDataLine.endsWith(" ")) {
                        bool sendSuccess = sendDailyDataToInfluxDB(dailyDataLine);
                        esp_task_wdt_reset();
                        
                        if (sendSuccess) {
                            lastSendTime = millis();
                        } else {
                            Serial.println("❌ 发送失败");
                        }
                    }
                    
                    unsigned long currentTime = millis();
                    if (currentTime - lastSleepDataTime >= SLEEP_DATA_INTERVAL) {
                        sendSleepDataToInfluxDB();
                        lastSleepDataTime = currentTime;
                    }
                    
                    hasPendingData = false;
                    }
                } else {
                    Serial.printf("💾 缓存数据未变化 - 心率:%.1f, 呼吸:%.1f\n",
                        vitalData.heart_rate, vitalData.breath_rate);
                    
                    if (hasPendingData && (millis() - lastSendTime >= MIN_SEND_INTERVAL) && !isInRefusedCooldown()) {
                        Serial.printf("⏰ 发送缓存数据 - 心率:%.1f, 呼吸:%.1f\n",
                            pendingData.heart_rate, pendingData.breath_rate);
                        
                        String dailyDataLine = "daily_data,deviceId=" + String(currentDeviceId) + ",dataType=daily ";
                        
                        bool firstField = true;
                        
                        if (pendingData.heart_rate > 0) {
                            if (!firstField) dailyDataLine += ",";
                            dailyDataLine += "heartRate=" + String(pendingData.heart_rate, 1);
                            firstField = false;
                        }
                        
                        if (pendingData.breath_rate > 0) {
                            if (!firstField) dailyDataLine += ",";
                            dailyDataLine += "breathingRate=" + String(pendingData.breath_rate, 1);
                            firstField = false;
                        }
                        
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "personDetected=" + String(pendingData.presence) + "i";
                        firstField = false;
                        
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "humanActivity=" + String(pendingData.motion) + "i";
                        firstField = false;
                        
                        if (pendingData.distance > 0) {
                            if (!firstField) dailyDataLine += ",";
                            dailyDataLine += "humanDistance=" + String(pendingData.distance) + "i";
                            firstField = false;
                        }
                        
                        if (pendingData.sleep_state >= 0) {
                            if (!firstField) dailyDataLine += ",";
                            dailyDataLine += "sleepState=" + String(pendingData.sleep_state) + "i";
                            firstField = false;
                        }
                        
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "humanPositionX=" + String(pendingData.pos_x) + "i";
                        firstField = false;
                        
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "humanPositionY=" + String(pendingData.pos_y) + "i";
                        firstField = false;
                        
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "humanPositionZ=" + String(pendingData.pos_z) + "i";
                        firstField = false;
                        
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "heartbeatWaveform=" + String((int)pendingData.heartbeat_waveform) + "i";
                        firstField = false;
                        
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "breathingWaveform=" + String((int)pendingData.breathing_waveform) + "i";
                        firstField = false;
                        
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "abnormalState=" + String(pendingData.abnormal_state) + "i";
                        firstField = false;
                        
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "bedStatus=" + String(pendingData.bed_status) + "i";
                        firstField = false;
                        
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "struggleAlert=" + String(pendingData.struggle_alert) + "i";
                        firstField = false;
                        
                        if (!firstField) dailyDataLine += ",";
                        dailyDataLine += "noOneAlert=" + String(pendingData.no_one_alert) + "i";
                        firstField = false;
                        
                        if (!dailyDataLine.endsWith(" ")) {
                            bool sendSuccess = sendDailyDataToInfluxDB(dailyDataLine);
                            esp_task_wdt_reset();
                            
                            if (sendSuccess) {
                                lastSendTime = millis();
                            } else {
                                Serial.println("❌ 缓存数据发送失败");
                            }
                        }
                        
                        hasPendingData = false;
                    }
                }
            } else {
                UBaseType_t queueItems = uxQueueMessagesWaiting(vitalDataQueue);
                if (queueItems > 0) {
                    VitalData tempData;
                    while (xQueueReceive(vitalDataQueue, &tempData, 0) == pdTRUE) {
                    }
                }
            }
            
            esp_task_wdt_reset();
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief 发送日常数据到InfluxDB数据库
 * 通过HTTP协议将数据写入InfluxDB时序数据库
 * @param dailyDataLine 数据行字符串
 * @return 是否发送成功
 */
bool isInRefusedCooldown() {
    static unsigned long lastRefusedTime = 0;
    const unsigned long REFUSED_COOLDOWN = 10000;
    
    if (lastRefusedTime == 0) return false;
    
    unsigned long currentTime = millis();
    return (currentTime - lastRefusedTime < REFUSED_COOLDOWN);
}

unsigned long getRefusedCooldownRemaining() {
    static unsigned long lastRefusedTime = 0;
    const unsigned long REFUSED_COOLDOWN = 10000;
    
    if (lastRefusedTime == 0) return 0;
    
    unsigned long currentTime = millis();
    unsigned long elapsed = currentTime - lastRefusedTime;
    if (elapsed >= REFUSED_COOLDOWN) return 0;
    return REFUSED_COOLDOWN - elapsed;
}

void updateRefusedTime() {
    static unsigned long lastRefusedTime = 0;
    lastRefusedTime = millis();
}

void resetWiFiConnection() {
    Serial.println("🔄 开始重置WiFi连接...");
    WiFi.disconnect(true);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    WiFi.mode(WIFI_OFF);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    WiFi.mode(WIFI_STA);
    vTaskDelay(200 / portTICK_PERIOD_MS);
    Serial.println("✅ WiFi连接已重置，将触发重连");
    wifiManager.handleReconnect();
}

bool sendDailyDataToInfluxDB(String dailyDataLine) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("❌ WiFi未连接，跳过发送");
        return false;
    }
    
    int rssi = WiFi.RSSI();
    if (rssi < -85) {
        Serial.printf("⚠️ WiFi信号过弱 (%d dBm)，跳过发送\n", rssi);
        return false;
    }
    
    if (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
        Serial.println("❌ WiFi没有获取到IP地址，跳过发送");
        return false;
    }
    
    if (isInRefusedCooldown()) {
        Serial.printf("⚠️ 连接被拒绝冷却中，等待 %d 秒\n", getRefusedCooldownRemaining() / 1000);
        return false;
    }
    
    IPAddress resolvedIP;
    if (!WiFi.hostByName(influxDBHost, resolvedIP)) {
        Serial.printf("❌ DNS解析失败: %s\n", influxDBHost);
        dnsFailCount++;
        Serial.printf("⚠️ DNS解析失败计数: %d/%d\n", dnsFailCount, DNS_FAIL_RESET_THRESHOLD);
        
        if (dnsFailCount >= DNS_FAIL_RESET_THRESHOLD) {
            Serial.println("⚠️ DNS解析连续失败次数过多，重置WiFi连接...");
            dnsFailCount = 0;
            resetWiFiConnection();
        }
        return false;
    } else {
        dnsFailCount = 0;
    }

    String url = String("http://") + resolvedIP.toString() + ":" + String(influxDBPort) + "/api/v2/write?org=" + String(influxDBOrg) + "&bucket=" + String(influxDBBucket);

    HTTPClient http;
    http.setTimeout(15000);
    http.setConnectTimeout(8000);
    http.setReuse(false);

    http.begin(url);
    http.addHeader("Authorization", String("Token ") + String(influxDBToken));
    http.addHeader("Content-Type", "text/plain; charset=utf-8");
    http.addHeader("Connection", "close");
    http.addHeader("Host", String(influxDBHost));

    int httpResponseCode = http.POST(dailyDataLine);
    String errorMsg = http.errorToString(httpResponseCode);
    
    if (httpResponseCode == 204) {
        Serial.println("✅ 日常数据已保存到InfluxDB");
        http.end();
        return true;
    }
    
    Serial.printf("❌ HTTP响应: %d (%s)\n", httpResponseCode, errorMsg.c_str());
    http.end();
    
    if (errorMsg.indexOf("connection refused") >= 0 || httpResponseCode == -1) {
        updateRefusedTime();
        Serial.println("⚠️ 服务器拒绝连接，进入冷却期");
    }
    
    return false;
}

/**
 * @brief 发送睡眠数据到InfluxDB数据库
 * 将睡眠相关的统计数据发送到InfluxDB时序数据库
 */
void sendSleepDataToInfluxDB() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    
    if (sensorData.sleep_total_time == 0) {
        return;
    }
    
    HTTPClient http;
    http.setTimeout(5000);  // 增加超时到5秒
    http.setConnectTimeout(5000);  // 连接超时5秒
    
    String url = String("http://") + String(influxDBHost) + ":" + String(influxDBPort) + "/api/v2/write?org=" + String(influxDBOrg) + "&bucket=" + String(influxDBBucket);
    
    http.begin(url);
    http.addHeader("Authorization", String("Token ") + String(influxDBToken));
    http.addHeader("Content-Type", "text/plain; charset=utf-8");
    
    String lineProtocol = String("sleep_data,deviceId=") + String(currentDeviceId) + ",dataType=sleep ";
    
    String fields = "";
    fields += String("sleepQualityScore=") + String((int)sensorData.sleep_score) + "i";
    fields += ",sleepQualityGrade=" + String((int)sensorData.sleep_grade) + "i";
    fields += ",totalSleepDuration=" + String((int)sensorData.sleep_total_time) + "i";
    fields += ",awakeDurationRatio=" + String((int)sensorData.awake_ratio) + "i";
    fields += ",lightSleepRatio=" + String((int)sensorData.light_sleep_ratio) + "i";
    fields += ",deepSleepRatio=" + String((int)sensorData.deep_sleep_ratio) + "i";
    fields += ",outOfBedDuration=" + String((int)sensorData.bed_Out_Time) + "i";
    fields += ",outOfBedCount=" + String((int)sensorData.turn_count) + "i";
    fields += ",turnCount=" + String((int)sensorData.turnover_count) + "i";
    fields += ",avgBreathingRate=" + String((int)sensorData.avg_breath_rate) + "i";
    fields += ",avgHeartRate=" + String((int)sensorData.avg_heart_rate) + "i";
    fields += ",apneaCount=" + String((int)sensorData.apnea_count) + "i";
    fields += ",abnormalState=" + String((int)sensorData.abnormal_state) + "i";
    fields += ",bodyMovement=" + String((int)sensorData.body_movement) + "i";
    fields += ",breathStatus=" + String((int)sensorData.breath_status) + "i";
    fields += ",sleepState=" + String((int)sensorData.sleep_state) + "i";
    fields += ",largeMoveRatio=" + String((int)sensorData.large_move_ratio) + "i";
    fields += ",smallMoveRatio=" + String((int)sensorData.small_move_ratio) + "i";
    fields += ",struggleAlert=" + String((int)sensorData.struggle_alert) + "i";
    fields += ",noOneAlert=" + String((int)sensorData.no_one_alert) + "i";
    fields += ",awakeDuration=" + String((int)sensorData.awake_time) + "i";
    fields += ",lightSleepDuration=" + String((int)sensorData.light_sleep_time) + "i";
    fields += ",deepSleepDuration=" + String((int)sensorData.deep_sleep_time) + "i";
    
    lineProtocol += fields;
    
    Serial.println(String("🌙 发送睡眠数据到InfluxDB: ") + lineProtocol);

    // 重试机制
    int maxRetries = 3;
    int httpResponseCode = -1;
    
    for (int retry = 0; retry < maxRetries; retry++) {
        if (retry > 0) {
            Serial.printf("🔄 睡眠数据第%d次重试...\n", retry + 1);
            delay(500);
        }
        
        httpResponseCode = http.POST(lineProtocol);
        
        if (httpResponseCode == 204) {
            Serial.println(String("✅ 睡眠数据已保存到InfluxDB设备") + String(currentDeviceId) + "上");
            http.end();
            return;
        }
        
        // 如果是连接错误，继续重试
        if (httpResponseCode == -1 || httpResponseCode == 0) {
            Serial.printf("⚠️ 睡眠数据连接失败 (错误码:%d)，准备重试...\n", httpResponseCode);
            http.end();
            http.begin(url);
            http.addHeader("Authorization", String("Token ") + String(influxDBToken));
            http.addHeader("Content-Type", "text/plain; charset=utf-8");
            continue;
        } else {
            break;
        }
    }
    
    Serial.printf("❌ 保存睡眠数据到InfluxDB失败 (重试%d次): %d - %s\n", 
        maxRetries, httpResponseCode,
        httpResponseCode == -1 ? "连接超时" : http.getString().c_str());
    
    http.end();
}

/**
 * @brief 雷达数据处理任务
 * 处理雷达数据并分发到相应的队列
 * @param parameter 任务参数（未使用）
 */
void radarDataTask(void *parameter) {
    Serial.println("🔁 雷达数据处理任务启动（最高优先级）");
    
    while (1) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief UART数据处理任务
 * 从UART队列中读取数据并解析雷达数据帧
 * @param parameter 任务参数（未使用）
 */
void uartProcessTask(void *parameter) {
    Serial.println("🔧 初始化雷达UART串口...");
    mySerial1.setRxBufferSize(UART_RX_BUFFER_SIZE);
    mySerial1.begin(BAUD_RATE, SERIAL_8N1, UART1_RX, UART1_TX);
    mySerial1.onReceive(serialRxCallback);
    Serial.println("✅ UART1配置完成，缓冲区大小: 4096字节");

    uint8_t buffer[256];
    int bufferIndex = 0;
    bool inFrame = false;
    uint8_t prevByte = 0;

    Serial.println("✅ R60ABD1串口数据处理任务启动");
    
    while(1) {
        uint8_t c;
        if(xQueueReceive(uartQueue, &c, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            esp_task_wdt_reset();
            if(!inFrame) {
                if(prevByte == FRAME_HEADER1 && c == FRAME_HEADER2) {
                    buffer[0] = FRAME_HEADER1;
                    buffer[1] = FRAME_HEADER2;
                    bufferIndex = 2;
                    inFrame = true;
                }
            } else {
                if(bufferIndex < sizeof(buffer)) {
                    buffer[bufferIndex++] = c;
                    
                    if(bufferIndex >= 8 && 
                       buffer[bufferIndex-2] == FRAME_TAIL1 && 
                       buffer[bufferIndex-1] == FRAME_TAIL2) {
                        if(parseR60ABD1Frame(buffer, bufferIndex)) {
                            static uint32_t frameCounter = 0;
                            frameCounter++;
                            
                            lastSensorUpdate = millis();
                            
                            static uint32_t phasePacketCounter = 0;
                            static uint32_t vitalPacketCounter = 0;
                            
                            phasePacketCounter++;
                            if (phasePacketCounter >= PHASE_SEND_INTERVAL) {
                                PhaseData phaseData;
                                memset(&phaseData, 0, sizeof(PhaseData)); // 初始化为0
                                phaseData.heartbeat_waveform = sensorData.heartbeat_waveform;
                                phaseData.breathing_waveform = sensorData.breathing_waveform;
                                
                                if (xQueueSend(phaseDataQueue, &phaseData, 0) == pdTRUE) {
                                } else {
                                }
                                phasePacketCounter = 0;
                            }
                            
                            vitalPacketCounter++;
                            if (vitalPacketCounter >= VITAL_SEND_INTERVAL) {
                                VitalData vitalData;
                                memset(&vitalData, 0, sizeof(VitalData)); // 初始化为0
                                vitalData.heart_rate = sensorData.heart_rate;
                                vitalData.breath_rate = sensorData.breath_rate;
                                vitalData.presence = sensorData.presence;
                                vitalData.motion = sensorData.motion;
                                vitalData.distance = sensorData.distance;
                                vitalData.sleep_state = sensorData.sleep_state;
                                vitalData.sleep_score = sensorData.sleep_score;
                                vitalData.body_movement = sensorData.body_movement;
                                vitalData.breath_status = sensorData.breath_status;
                                vitalData.sleep_time = sensorData.sleep_time;
                                vitalData.bed_status = sensorData.bed_status;
                                vitalData.abnormal_state = sensorData.abnormal_state;
                                vitalData.avg_heart_rate = sensorData.avg_heart_rate;
                                vitalData.avg_breath_rate = sensorData.avg_breath_rate;
                                vitalData.turn_count = sensorData.turn_count;
                                vitalData.large_move_ratio = sensorData.large_move_ratio;
                                vitalData.small_move_ratio = sensorData.small_move_ratio;
                                vitalData.pos_x = sensorData.pos_x;
                                vitalData.pos_y = sensorData.pos_y;
                                vitalData.pos_z = sensorData.pos_z;
                                vitalData.deep_sleep_time = sensorData.deep_sleep_time;
                                vitalData.light_sleep_time = sensorData.light_sleep_time;
                                vitalData.awake_time = sensorData.awake_time;
                                vitalData.sleep_total_time = sensorData.sleep_total_time;
                                vitalData.deep_sleep_ratio = sensorData.deep_sleep_ratio;
                                vitalData.light_sleep_ratio = sensorData.light_sleep_ratio;
                                vitalData.awake_ratio = sensorData.awake_ratio;
                                vitalData.turnover_count = sensorData.turnover_count;
                                vitalData.struggle_alert = sensorData.struggle_alert;
                                vitalData.no_one_alert = sensorData.no_one_alert;
                                vitalData.apnea_count = sensorData.apnea_count;
                                vitalData.heartbeat_waveform = sensorData.heartbeat_waveform;
                                vitalData.breathing_waveform = sensorData.breathing_waveform;
                                
                                if (xQueueSend(vitalDataQueue, &vitalData, 0) == pdTRUE) {
                                } else {
                                }
                                vitalPacketCounter = 0;
                            }
                            
                            if(frameCounter % 100 == 0) {
                            }
                        }
                        
                        inFrame = false;
                    }
                } else {
                    Serial.println("⚠️ R60ABD1帧缓冲区溢出，重置接收状态");
                    inFrame = false;
                }
            }
            
            prevByte = c;
        }
        
        vTaskDelay(1 / portTICK_PERIOD_MS);
        esp_task_wdt_reset();
    }
}

/**
 * @brief 分块发送数据
 * 将大数据分块发送，避免BLE MTU限制
 * @param data 要发送的数据字符串
 */
void sendDataInChunks(const String& data) {
    if (xSemaphoreTake(bleSendMutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    const int MAX_PACKET_SIZE = 20;
    const int HEADER_SIZE = 6;
    const int CHUNK_SIZE = MAX_PACKET_SIZE - HEADER_SIZE;

    int totalLength = data.length();
    int numChunks = (totalLength + CHUNK_SIZE - 1) / CHUNK_SIZE;

    Serial.printf("[BLE发送] 分包数据 %d 字节, %d 包\n", totalLength, numChunks);

    for(int i = 0; i < numChunks; i++) {
        int start = i * CHUNK_SIZE;
        int chunkLength = min(CHUNK_SIZE, totalLength - start);
        String chunk = data.substring(start, start + chunkLength);

        String packetHeader = String("[") + String(i+1) + String("/") + String(numChunks) + String("]");

        int maxDataLength = MAX_PACKET_SIZE - packetHeader.length();
        if (chunk.length() > maxDataLength) {
            chunk = chunk.substring(0, maxDataLength);
        }

        String packet = packetHeader + chunk;

        if (!deviceConnected) {
            xSemaphoreGive(bleSendMutex);
            return;
        }

        pCharacteristic->setValue(packet.c_str());
        pCharacteristic->notify();

        if (i < numChunks - 1) {
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
    }

    xSemaphoreGive(bleSendMutex);
}

/**
 * @brief 发送JSON数据到BLE（TLV帧封装）
 * 将JSON字符串通过 BleProto::encodeLegacyJsonToFrame 转换为TLV帧后分包发送。
 * 每包最大20字节，包间延迟10ms，使用互斥锁保证线程安全。
 * @param jsonData JSON格式数据字符串
 */
void sendJSONDataToBLE(const String& jsonData) {
    if (xSemaphoreTake(bleSendMutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    if (!deviceConnected) {
        xSemaphoreGive(bleSendMutex);
        return;
    }

    BleProto::Frame frame;
    if (!BleProto::encodeLegacyJsonToFrame(jsonData, bleSequenceCounter++, frame)) {
        Serial.printf("[BLE] JSON转TLV失败，原始数据: %s\n", jsonData.c_str());
        xSemaphoreGive(bleSendMutex);
        return;
    }

    std::vector<uint8_t> raw = BleProto::encodeFrame(frame);

    const size_t MAX_PACKET_SIZE = 20;
    size_t offset = 0;

    while (offset < raw.size()) {
        size_t chunkLen = min(MAX_PACKET_SIZE, raw.size() - offset);
        pCharacteristic->setValue(raw.data() + offset, chunkLen);
        pCharacteristic->notify();

        offset += chunkLen;
        if (offset < raw.size()) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
    }

    Serial.printf("[BLE] 已发送TLV帧 CMD=0x%02X, 总长=%u\n",
                  frame.cmd, static_cast<unsigned>(raw.size()));

    xSemaphoreGive(bleSendMutex);
}

/**
 * @brief 发送自定义JSON数据到BLE
 * 构造自定义JSON格式数据并通过BLE发送
 * @param jsonType JSON类型
 * @param jsonString JSON内容字符串
 * @return 是否发送成功
 */
bool sendCustomJSONData(const String& jsonType, const String& jsonString) {
    if (!deviceConnected) {
        return false;
    }

    String fullJSON = String("{\"type\":\"") + jsonType + String("\",") + jsonString + String("}");

    sendJSONDataToBLE(fullJSON);

    return true;
}

/**
 * @brief 发送雷达数据到BLE
 * 发送雷达传感器数据（已移至FreeRTOS任务处理）
 */
void sendRadarDataToBLE() {
    Serial.println("ℹ️ 雷达数据发送已移至FreeRTOS任务处理");
}

/**
 * @brief 处理查询雷达数据命令
 * 处理来自BLE的雷达数据查询请求
 * @param doc JSON文档对象
 * @return 是否处理成功
 */
bool processQueryRadarData(JsonDocument& doc) {
    const char* command = doc["command"];
    if (command != nullptr && strcmp(command, "queryRadarData") == 0) {
        Serial.println("收到查询雷达数据命令");
        
        if (deviceConnected) {
            String radarDataMsg = String("{\"type\":\"radarData\",\"success\":true") +
                               String(",\"deviceId\":") + String(currentDeviceId) +
                               String(",\"timestamp\":") + String(millis()) +
                               String(",\"presence\":") + String(sensorData.presence) +
                               String(",\"heartRate\":") + String(sensorData.heart_rate, 1) +
                               String(",\"breathRate\":") + String(sensorData.breath_rate, 1) +
                               String(",\"motion\":") + String(sensorData.motion) +
                               String(",\"heartbeatWaveform\":") + String((int)sensorData.breath_waveform[0]) +
                               String(",\"breathingWaveform\":") + String((int)sensorData.heart_waveform[0]) +
                               String(",\"distance\":") + String(sensorData.distance) +
                               String(",\"bodyMovement\":") + String(sensorData.body_movement) +
                               String(",\"breathStatus\":") + String(sensorData.breath_status) +
                               String(",\"sleepState\":") + String(sensorData.sleep_state) +
                               String(",\"sleepTime\":") + String(sensorData.sleep_time) +
                               String(",\"sleepScore\":") + String(sensorData.sleep_score) +
                               String(",\"avgHeartRate\":") + String(sensorData.avg_heart_rate) +
                               String(",\"avgBreathRate\":") + String(sensorData.avg_breath_rate) +
                               String(",\"turnCount\":") + String(sensorData.turn_count) +
                               String(",\"largeMoveRatio\":") + String(sensorData.large_move_ratio) +
                               String(",\"smallMoveRatio\":") + String(sensorData.small_move_ratio) +
                               String("}");
      
            sendJSONDataToBLE(radarDataMsg);
            Serial.println("已发送雷达数据");
            Serial.printf("发送的数据: %s\n", radarDataMsg.c_str());
        } else {
            Serial.println("BLE未连接，无法发送雷达数据");
        }
        return true;
    }
    return false;
}

/**
 * @brief 处理启动持续发送命令
 * 处理来自BLE的启动持续发送请求
 * @param doc JSON文档对象
 * @return 是否处理成功
 */
bool processStartContinuousSend(JsonDocument& doc) {
    const char* command = doc["command"];
    if (command != nullptr && strcmp(command, "startContinuousSend") == 0) {
        if (doc["interval"].is<uint32_t>()) {
            continuousSendInterval = doc["interval"].as<uint32_t>();
            if (continuousSendInterval < 100) continuousSendInterval = 100;
            if (continuousSendInterval > 10000) continuousSendInterval = 10000;
        }
        
        continuousSendEnabled = true;
        bleFlow.reset();
        
        Serial.printf("⚙️ 启动持续发送模式，间隔: %lu ms\n", continuousSendInterval);
        
        if (deviceConnected) {
            String confirmMsg = String("{\"type\":\"startContinuousSendResult\",\"success\":true,\"message\":\"已启动持续发送模式\",\"interval\":") +
                               String(continuousSendInterval) + "}";

            sendJSONDataToBLE(confirmMsg);
        }
        return true;
    }
    return false;
}

/**
 * @brief 处理停止持续发送命令
 * 处理来自BLE的停止持续发送请求
 * @param doc JSON文档对象
 * @return 是否处理成功
 */
bool processStopContinuousSend(JsonDocument& doc) {
    const char* command = doc["command"];
    if (command != nullptr && strcmp(command, "stopContinuousSend") == 0) {
        continuousSendEnabled = false;
        
        Serial.println("🛑 停止持续发送模式");
        
        if (deviceConnected) {
            String confirmMsg = String("{\"type\":\"stopContinuousSendResult\",\"success\":true,\"message\":\"已停止持续发送模式\"}");

            sendJSONDataToBLE(confirmMsg);
        }
        return true;
    }
    return false;
}

/**
 * @brief 处理BLE配置数据
 * 处理从BLE接收到的配置数据，解析JSON命令并执行相应操作
 */
void processBLEConfig() {
    // 队列未初始化时直接返回，避免空指针访问
    if (bleCommandQueue == nullptr) {
        return;
    }
    
    BleCommandMessage msg;
    while (xQueueReceive(bleCommandQueue, &msg, 0) == pdTRUE) {
        String bleData = String(msg.json);
        bleData.trim();

        Serial.printf("[BLE] 解析: %s\n", bleData.c_str());

        if (bleData.startsWith("{") && bleData.endsWith("}")) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, bleData);

            if (error) {
                String errorMsg = String("{\"type\":\"error\",\"message\":\"JSON解析失败: ") +
                                  String(error.c_str()) + String("\"}");
                sendJSONDataToBLE(errorMsg);
                continue;
            }

            bool processed = false;
            if (!processed) processed = processSetDeviceId(doc);
            if (!processed) processed = processWiFiConfigCommand(doc);
            if (!processed) processed = processQueryStatus(doc);
            if (!processed) processed = processQueryRadarData(doc);
            if (!processed) processed = processStartContinuousSend(doc);
            if (!processed) processed = processStopContinuousSend(doc);
            if (!processed) processed = processScanWiFi(doc);
            if (!processed) processed = processGetSavedNetworks(doc);
            if (!processed) processed = processEchoRequest(doc);

            if (!processed && deviceConnected) {
                JsonDocument errorDoc;
                errorDoc["type"] = "error";
                errorDoc["message"] = "未知命令";

                String responseMsg;
                serializeJson(errorDoc, responseMsg);
                sendJSONDataToBLE(responseMsg);
            }
        }
    }
}

/**
 * @brief 处理设置设备ID命令
 * 处理来自BLE的设置设备ID请求
 * @param doc JSON文档对象
 * @return 是否处理成功
 */
bool processSetDeviceId(JsonDocument& doc) {
  const char* command = doc["command"];
  if (command != nullptr && strcmp(command, "setDeviceId") == 0) {
    uint16_t newDeviceId = doc["newDeviceId"];
    
   if (newDeviceId < 1000 || newDeviceId > 1999){
      Serial.printf("[错误] 设备ID超出范围，有效范围: 1000-1999\n");
      if (deviceConnected) {
        String errorMsg = String("{\"type\":\"error\",\"message\":\"设备ID超出范围，有效范围: 1000-1999\"}");
        
        sendJSONDataToBLE(errorMsg);
      }
      return true;
    }
    
    currentDeviceId = newDeviceId;
    
    Serial.printf("[设备ID] 已设置新的设备ID: %u\n", currentDeviceId);
    
    preferences.putUShort("deviceId", currentDeviceId);
    Serial.printf("设备ID已保存到Flash: %u\n", currentDeviceId);
    
    if (deviceConnected) {
      String confirmMsg = String("{\"type\":\"setDeviceIdResult\",\"success\":true,\"message\":\"设备ID设置成功\",\"newDeviceId\":") + 
                         String(newDeviceId) + "}";
      
      sendJSONDataToBLE(confirmMsg);
      
      sendStatusToBLE();
    }
    return true;
  }
  return false;
}

/**
 * @brief 发送设备状态到BLE
 * 将当前设备状态信息通过BLE发送给客户端
 */
void sendStatusToBLE() {
  if (deviceConnected) {
    String statusMsg = String("{\"type\":\"status\",\"wifiConfigured\":") + 
                      String(wifiManager.getSavedNetworkCount() > 0 ? "true" : "false") +
                      String(",\"wifiConnected\":") + 
                      String(WiFi.status() == WL_CONNECTED ? "true" : "false") +
                      String(",\"ipAddress\":\"") + 
                      (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "") + "\"" +
                      String(",\"deviceId\":") + 
                      String(currentDeviceId) + "}";
    
    sendJSONDataToBLE(statusMsg);
    Serial.println("已发送连接状态信息");
  }
}

/**
 * @brief 处理查询状态命令
 * 处理来自BLE的查询设备状态请求
 * @param doc JSON文档对象
 * @return 是否处理成功
 */
bool processQueryStatus(JsonDocument& doc) {
  const char* command = doc["command"];
  if (command != nullptr && strcmp(command, "queryStatus") == 0) {
    if (deviceConnected) {
      String statusMsg = String("{\"type\":\"deviceStatus\",\"success\":true,\"deviceId\":") + 
                        String(currentDeviceId) + 
                        String(",\"wifiConfigured\":") + 
                        String(wifiManager.getSavedNetworkCount() > 0 ? "true" : "false") +
                        String(",\"wifiConnected\":") + 
                        String(WiFi.status() == WL_CONNECTED ? "true" : "false") +
                        String(",\"ipAddress\":\"") + 
                        (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "") + 
                        String("\"}");
      
      sendJSONDataToBLE(statusMsg);
      Serial.println("已发送设备状态信息");
    }
    return true;
  }
  return false;
}

/**
 * @brief 处理WiFi配置命令
 * 处理来自BLE的WiFi配置请求
 * @param doc JSON文档对象
 * @return 是否处理成功
 */
bool processWiFiConfigCommand(JsonDocument& doc) {
  const char* command = doc["command"];
  if (command != nullptr && strcmp(command, "setWiFiConfig") == 0) {
    Serial.println("📱 [BLE-WiFi] 收到WiFi配置命令");
    const char* newSSID = doc["ssid"];
    const char* newPassword = doc["password"];
    
    if (newSSID != nullptr && newPassword != nullptr) {

      return wifiManager.handleConfigurationData(newSSID, newPassword);
    } else {
      Serial.println("❌ [BLE-WiFi] WiFi配置参数不完整");
      if (deviceConnected) {
        String errorMsg = String("{\"type\":\"error\",\"message\":\"WiFi配置参数不完整，需要ssid和password字段\"}");
        sendJSONDataToBLE(errorMsg);
      }
      return true;
    }
  }
  return false;
}

/**
 * @brief 处理扫描WiFi命令
 * 处理来自BLE的WiFi扫描请求
 * @param doc JSON文档对象
 * @return 是否处理成功
 */
bool processScanWiFi(JsonDocument& doc) {
  const char* command = doc["command"];
  if (command != nullptr && strcmp(command, "scanWiFi") == 0) {
    Serial.println("📱 [BLE-WiFi] 收到WiFi扫描命令");
    
    // 调用新的startScan接口（会暂停重连任务）
    if (wifiManager.startScan(30000)) {
      Serial.println("✅ 扫描已启动");
      return true;
    } else {
      String errorMsg = String("{\"type\":\"scanWiFiResult\",\"success\":false,\"message\":\"无法启动扫描\",\"networks\":[],\"count\":0}");
      sendJSONDataToBLE(errorMsg);
      return false;
    }
  }
  return false;
}

bool processGetSavedNetworks(JsonDocument& doc) {
  const char* command = doc["command"];
  if (command != nullptr && strcmp(command, "getSavedNetworks") == 0) {
    Serial.println("📱 [BLE-WiFi] 收到获取已保存WiFi网络命令");
    wifiManager.getSavedNetworks();
    return true;
  }
  return false;
}

/**
 * @brief 处理回显请求命令
 * 处理来自BLE的回显测试请求
 * @param doc JSON文档对象
 * @return 是否处理成功
 */
bool processEchoRequest(JsonDocument& doc) {
  const char* command = doc["command"];
  if (command != nullptr && strcmp(command, "echo") == 0) {
    Serial.println("📱 [BLE] 收到回显请求");
    
    const char* echoContent = doc["content"];
    if (echoContent != nullptr) {
      String echoResponse = String("{\"type\":\"echoResponse\",\"originalContent\":\"") + 
                           echoContent + String("\",\"receivedSuccessfully\":true}");
      if (deviceConnected) {
        sendJSONDataToBLE(echoResponse);
      }
    } else {
      String echoResponse = String("{\"type\":\"echoResponse\",\"receivedSuccessfully\":true,\"message\":\"Echo command received\"}");

      if (deviceConnected) {
        sendJSONDataToBLE(echoResponse);
      }
    }

    return true;
  }
  return false;
}

/**
 * @brief 发送原始数据回显响应
 * 将接收到的原始数据通过BLE回显给客户端
 * @param rawData 原始数据字符串
 */
void sendRawEchoResponse(const String& rawData) {
  if (deviceConnected) {
    String echoResponse = String("{\"type\":\"rawEchoResponse\",\"originalData\":\"") +
                         rawData + String("\",\"received\":true}");

    sendJSONDataToBLE(echoResponse);
  }
}
