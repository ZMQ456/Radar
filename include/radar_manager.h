#ifndef RADAR_MANAGER_H
#define RADAR_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <Preferences.h>

class WiFiManager;

#define SERVICE_UUID        "a8c1e5c0-3d5d-4a9d-8d5e-7c8b6a4e2f1a" // BLE服务UUID
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8" // BLE特征值UUID
#define UART_RX_BUFFER_SIZE 4096 // UART接收缓冲区大小
#define QUEUE_SIZE 200 // 队列大小（增加到200以防止溢出）
#define TASK_STACK_SIZE 8192 // 任务堆栈大小

#define FRAME_HEADER1 0x53  // 帧头字节1
#define FRAME_HEADER2 0x59  // 帧头字节2
#define FRAME_TAIL1   0x54  // 帧尾字节1
#define FRAME_TAIL2   0x43  // 帧尾字节2

// 控制字定义
#define CTRL_PRESENCE    0x80  // 人体存在检测
#define CTRL_BREATH      0x81  // 呼吸检测
#define CTRL_SLEEP       0x84  // 睡眠监测
#define CTRL_HEARTRATE   0x85  // 心率监测

// 命令字定义
#define CMD_REPORT       0x80  // 主动上报
#define CMD_QUERY        0x81  // 查询命令
#define CMD_SET          0x82  // 设置命令

// 定义R60ABD1数据结构
typedef struct {
    uint8_t present;      // 有人/无人状态 (DP1)
    uint16_t distance;    // 人体距离 (DP3) 单位cm
    uint8_t heartRate;    // 心率 (DP6) 单位BPM
    uint8_t breathRate;   // 呼吸率 (DP8) 单位次/分钟
    uint8_t heartWave;    // 心率波形 (DP7) 数值+128
    uint8_t breathWave;   // 呼吸波形 (DP10) 数值+128
    uint8_t sleepState;   // 睡眠状态 (DP12)
    uint32_t sleepTime;   // 睡眠时长 (DP13) 单位秒
    uint8_t sleepScore;   // 睡眠质量评分 (DP14)
    uint8_t bedEntry;     // 入床/离床状态 (DP11)
    uint8_t abnormal;     // 异常状态 (DP18)
} R60ABD1Data; // R60ABD1雷达数据结构体

typedef struct { // 传感器数据结构体
    float breath_rate; // 呼吸率
    float heart_rate; // 心率
    uint8_t breath_valid; // 呼吸率有效标志
    uint8_t heart_valid; // 心率有效标志
    uint8_t presence; // 存在状态
    uint8_t motion; // 运动状态
    int heartbeat_waveform; // 心跳波形
    int breathing_waveform; // 呼吸波形
    uint16_t distance; // 距离
    uint8_t body_movement; // 身体运动
    uint8_t breath_status; // 呼吸状态
    uint8_t sleep_state; // 睡眠状态
    uint32_t sleep_time; // 睡眠时长
    uint8_t sleep_score; // 睡眠评分
    uint8_t sleep_grade; // 睡眠等级
    uint8_t bed_entry; // 入床状态
    uint8_t abnormal_state; // 异常状态
    uint8_t avg_heart_rate; // 平均心率
    uint8_t avg_breath_rate; // 平均呼吸率
    uint8_t turn_count; // 翻身次数
    uint8_t large_move_ratio; // 大幅运动比例
    uint8_t small_move_ratio; // 小幅运动比例
    int16_t pos_x; // X坐标
    int16_t pos_y; // Y坐标
    int16_t pos_z; // Z坐标
    int8_t breath_waveform[5]; // 呼吸波形数组
    int8_t heart_waveform[5]; // 心跳波形数组
    uint16_t deep_sleep_time; // 深度睡眠时长
    uint16_t light_sleep_time; // 浅度睡眠时长
    uint16_t awake_time; // 清醒时长
    uint16_t sleep_total_time; // 总睡眠时长
    uint8_t deep_sleep_ratio; // 深度睡眠比例
    uint8_t light_sleep_ratio; // 浅度睡眠比例
    uint8_t awake_ratio; // 清醒比例
    uint8_t turnover_count; // 翻身计数
    uint8_t struggle_alert; // 挣扎警报
    uint8_t no_one_alert; // 无人警报
    uint8_t bed_status; // 床状态
    uint8_t bed_Out_Time; // 离床时间
    uint8_t apnea_count; // 呼吸暂停次数
} SensorData; // 传感器数据结构体

typedef struct { // 相位数据结构体
    int heartbeat_waveform; // 心跳波形
    int breathing_waveform; // 呼吸波形
} PhaseData; // 相位数据结构体

typedef struct { // 生命体征数据结构体
    float heart_rate; // 心率
    float breath_rate; // 呼吸率
    uint8_t presence; // 存在状态
    uint8_t motion; // 运动状态
    uint16_t distance; // 距离
    uint8_t sleep_state; // 睡眠状态
    uint8_t sleep_score; // 睡眠评分
    uint8_t body_movement; // 身体运动
    uint8_t breath_status; // 呼吸状态
    uint32_t sleep_time; // 睡眠时长
    uint8_t bed_entry; // 入床状态
    uint8_t abnormal_state; // 异常状态
    uint8_t avg_heart_rate; // 平均心率
    uint8_t avg_breath_rate; // 平均呼吸率
    uint8_t turn_count; // 翻身次数
    uint8_t large_move_ratio; // 大幅运动比例
    uint8_t small_move_ratio; // 小幅运动比例
    int16_t pos_x; // X坐标
    int16_t pos_y; // Y坐标
    int16_t pos_z; // Z坐标
    uint16_t deep_sleep_time; // 深度睡眠时长
    uint16_t light_sleep_time; // 浅度睡眠时长
    uint16_t awake_time; // 清醒时长
    uint16_t sleep_total_time; // 总睡眠时长
    uint8_t deep_sleep_ratio; // 深度睡眠比例
    uint8_t light_sleep_ratio; // 浅度睡眠比例
    uint8_t awake_ratio; // 清醒比例
    uint8_t turnover_count; // 翻身计数
    uint8_t struggle_alert; // 挣扎警报
    uint8_t no_one_alert; // 无人警报
    uint8_t bed_status; // 床状态
    uint8_t apnea_count; // 呼吸暂停次数
    int heartbeat_waveform; // 心跳波形
    int breathing_waveform; // 呼吸波形
} VitalData; // 生命体征数据结构体

typedef struct { // 上次发送数据结构体
    float heart_rate; // 心率
    float breath_rate; // 呼吸率
    uint8_t presence; // 存在状态
    uint8_t motion; // 运动状态
    uint8_t sleep_state; // 睡眠状态
} LastSentData; // 上次发送数据结构体

class BLEFlowController { // BLE流控制器类
private:
    size_t maxBytesPerSecond; // 最大每秒发送字节数
    size_t bytesSent; // 已发送字节数
    unsigned long lastResetTime; // 上次重置时间
    unsigned long lastSendTime; // 上次发送时间
    
public:
    BLEFlowController(size_t maxBps);
    bool canSend(size_t dataSize);
    bool check();
    void recordSend(size_t dataSize);
    void reset();
};

extern SensorData sensorData; // 传感器数据
extern HardwareSerial mySerial1; // 硬件串口1
extern QueueHandle_t phaseDataQueue; // 相位数据队列
extern QueueHandle_t vitalDataQueue; // 生命体征数据队列
extern QueueHandle_t uartQueue; // UART数据队列
extern TaskHandle_t bleSendTaskHandle; // BLE发送任务句柄
extern TaskHandle_t vitalSendTaskHandle; // 生命体征发送任务句柄
extern TaskHandle_t uartProcessTaskHandle; // UART处理任务句柄
extern BLEServer* pServer; // BLE服务器指针
extern BLECharacteristic* pCharacteristic; // BLE特征值指针
extern bool deviceConnected; // 设备连接状态
extern bool oldDeviceConnected; // 旧设备连接状态
extern String receivedData; // 接收到的数据
extern String completeData; // 完整数据
extern unsigned long lastReceiveTime; // 上次接收数据时间
extern bool continuousSendEnabled; // 持续发送使能标志
extern unsigned long continuousSendInterval; // 持续发送间隔
extern unsigned long lastSleepDataTime; // 上次发送睡眠数据时间
extern BLEFlowController bleFlow; // BLE流控制器
extern unsigned long lastSensorUpdate; // 上次传感器更新时间
extern LastSentData lastSentData; // 上次发送的数据
extern unsigned long lastCheckTime; // 上次检测时间

extern uint16_t currentDeviceId; // 当前设备ID
extern Preferences preferences; // Flash存储对象
extern WiFiManager wifiManager; // WiFi管理器

void initRadarManager();
void initR60ABD1();
bool parseR60ABD1Frame(uint8_t *frame, uint16_t frameLen);
int16_t parseSignedCoordinate(uint16_t raw_value);
void sendRadarCommand(uint8_t ctrl, uint8_t cmd, uint8_t value);
void IRAM_ATTR serialRxCallback();

void bleSendTask(void *parameter);
void vitalSendTask(void *parameter);
void radarDataTask(void *parameter);
void uartProcessTask(void *parameter);

void sendDataInChunks(const String& data);
void sendJSONDataToBLE(const String& jsonData);
bool sendCustomJSONData(const String& jsonType, const String& jsonString);
void sendRadarDataToBLE();

bool processQueryRadarData(JsonDocument& doc);
bool processStartContinuousSend(JsonDocument& doc);
bool processStopContinuousSend(JsonDocument& doc);
void processBLEConfig();

bool processSetDeviceId(JsonDocument& doc);
bool processQueryStatus(JsonDocument& doc);
bool processWiFiConfigCommand(JsonDocument& doc);
bool processScanWiFi(JsonDocument& doc);
bool processGetSavedNetworks(JsonDocument& doc);
bool processEchoRequest(JsonDocument& doc);
void sendRawEchoResponse(const String& rawData);
void sendStatusToBLE();

bool sendDailyDataToInfluxDB(String dailyDataLine);
void sendSleepDataToInfluxDB();

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer);
    void onDisconnect(BLEServer* pServer);
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic);
};

#endif
