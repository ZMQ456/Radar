#include "mqtt.h"
#include "wifi_manager.h"
#include "radar_manager.h"
#include <mbedtls/md5.h>

extern uint64_t device_sn;
extern Preferences preferences;
extern WiFiManager wifiManager;
extern String getDeviceMacAddress();

TaskHandle_t mqttTaskHandle = NULL;

const char* mqttServer = "www.lmhrt.cn";
const int mqttPort = 1883;
const char* mqttDeviceModel = "radar_1.0";
const char* mqttProductKey = "dEkr5BkkXTFZFBdR";
const char* mqttProductSecret = "2e7957febfcb48b08a1c69b8deb56738";

String deviceMacAddress = "";

WiFiClient mqttWiFiClient;
PubSubClient mqttClient(mqttWiFiClient);

static uint32_t mqttMessageId = 1;

unsigned long lastSleepDataTime = 0;
const unsigned long SLEEP_DATA_INTERVAL = 10000;
unsigned long lastDailyDataTime = 0;
const unsigned long DAILY_DATA_INTERVAL = 5000;
unsigned long lastHeartbeatTime = 0;
const unsigned long HEARTBEAT_INTERVAL = 10000;  // 10秒心跳间隔

String getMqttDeviceName() {
    if (device_sn != 0) {
        return String((unsigned long long)device_sn);
    }

    String fallback = getDeviceMacAddress();
    fallback.replace(":", "");
    return fallback;
}

/**
 * @brief 获取MQTT客户端ID
 * 按照enjoy-iot规范构建客户端ID
 *
 * 格式：{productKey}_{deviceName}_{model}
 *
 * @return 客户端ID字符串
 * @example "radar_2024_12345678_v1"
 */
String getMqttClientId() {
    return String(mqttProductKey) + "_" + getMqttDeviceName() + "_" + String(mqttDeviceModel);
}

/**
 * @brief 获取MQTT下行订阅主题
 * 用于订阅平台下发的所有指令
 *
 * 格式：/sys/{productKey}/{deviceName}/c/#
 * - /sys/ 系统主题前缀
 * - {productKey} 产品标识
 * - {deviceName} 设备名称
 * - /c/ 下行指令标识 (command)
 * - # 通配符，匹配所有子主题
 *
 * @return 订阅主题字符串
 * @example "/sys/radar_2024/12345678/c/#"
 */
String getMqttSubscribeTopic() {
    return String("/sys/") + mqttProductKey + "/" + getMqttDeviceName() + "/c/#";
}

/**
 * @brief 获取MQTT属性上报主题
 * 用于设备向平台上报属性数据
 *
 * 格式：/sys/{productKey}/{deviceName}/s/event/property/post
 * - /sys/ 系统主题前缀
 * - {productKey} 产品标识
 * - {deviceName} 设备名称
 * - /s/ 上行状态标识 (status)
 * - /event/property/post 属性上报事件
 *
 * @return 上报主题字符串
 * @example "/sys/radar_2024/12345678/s/event/property/post"
 */
String getMqttPropertyPostTopic() {
    return String("/sys/") + mqttProductKey + "/" + getMqttDeviceName() + "/s/event/property/post";
}

/**
 * @brief 生成下一个MQTT消息ID
 * 每次调用返回递增的ID，用于消息追踪和请求-响应匹配
 *
 * @return 消息ID字符串
 * @example "1" -> "2" -> "3" ...
 */
static String nextMqttMessageId() {
    return String(mqttMessageId++);
}

/**
 * @brief 计算MQTT连接密码
 * 按照enjoy-iot规范，使用MD5计算密码
 *
 * 公式：password = MD5(productSecret + clientId)
 *
 * @param clientId 客户端ID
 * @return 32位小写十六进制MD5字符串
 * @example MD5("abc123" + "radar_2024_12345678_v1") -> "a1b2c3d4e5f6..."
 */
String makeMqttPassword(const String& clientId) {
    String raw = String(mqttProductSecret) + clientId;

    unsigned char digest[16];
    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts_ret(&ctx);
    mbedtls_md5_update_ret(&ctx, (const unsigned char*)raw.c_str(), raw.length());
    mbedtls_md5_finish_ret(&ctx, digest);
    mbedtls_md5_free(&ctx);

    char md5str[33];
    for (int i = 0; i < 16; i++) {
        sprintf(&md5str[i * 2], "%02x", digest[i]);
    }
    md5str[32] = '\0';
    return String(md5str);
}

/**
 * @brief 统一属性上报函数
 * 封装enjoy-iot规范的属性上报格式，被sendDailyDataToMQTT和sendSleepDataToMQTT调用
 *
 * 载荷格式：
 * {
 *   "id": "消息ID",
 *   "method": "thing.event.property.post",
 *   "params": {
 *     "deviceId": "设备ID",
 *     "reportType": "daily/sleep",
 *     ...业务字段...
 *   }
 * }
 *
 * @param params 业务参数JSON对象（调用者填充业务字段）
 * @param reportType 上报类型 ("daily" 或 "sleep")
 * @return true 发布成功，false 发布失败
 */
static bool publishPropertyReport(JsonDocument& params, const char* reportType) {
    JsonDocument payloadDoc;

    payloadDoc["id"] = nextMqttMessageId();

    payloadDoc["method"] = "thing.event.property.post";

    params["deviceId"] = String((unsigned long long)device_sn);
    params["reportType"] = reportType;

    payloadDoc["params"] = params;

    String topic = getMqttPropertyPostTopic();

    String payload;
    serializeJson(payloadDoc, payload);

    return mqttClient.publish(topic.c_str(), payload.c_str());
}

/**
 * @brief 构建回复主题
 * 将下行请求主题转换为上行回复主题
 *
 * 转换规则：
 * - /c/ 替换为 /s/
 * - 末尾添加 _reply
 *
 * @param requestTopic 请求主题
 * @return 回复主题字符串
 * @example "/sys/.../c/service/property/set" -> "/sys/.../s/service/property/set_reply"
 */
String buildReplyTopic(const char* requestTopic) {
    String topic = String(requestTopic);
    topic.replace("/c/", "/s/");
    topic += "_reply";
    return topic;
}

/**
 * @brief 发送MQTT回复
 * 统一处理平台下发指令的回复
 *
 * 回复格式：
 * {
 *   "id": "原请求ID",
 *   "method": "原method_reply",
 *   "code": 0,  // 0=成功，其他=失败
 *   "data": { ... }
 * }
 *
 * @param requestTopic 请求主题
 * @param requestId 请求ID
 * @param requestMethod 请求方法
 * @param code 状态码 (0=成功，-1=失败)
 * @param data 回复数据
 * @return true 发送成功，false 发送失败
 */
bool publishMqttReply(const char* requestTopic,
                      const char* requestId,
                      const char* requestMethod,
                      int code,
                      JsonVariant data) {
    JsonDocument replyDoc;
    replyDoc["id"] = requestId ? requestId : "";
    replyDoc["method"] = String(requestMethod ? requestMethod : "") + "_reply";
    replyDoc["code"] = code;
    replyDoc["params"] = data;

    String replyTopic = buildReplyTopic(requestTopic);
    String payload;
    serializeJson(replyDoc, payload);

    Serial.printf("[MQTT] reply topic: %s\n", replyTopic.c_str());
    Serial.printf("[MQTT] reply payload: %s\n", payload.c_str());

    return mqttClient.publish(replyTopic.c_str(), payload.c_str());
}

/**
 * @brief MQTT消息回调函数
 * 处理平台下发的指令，支持属性设置、属性读取和自定义服务
 *
 * 支持的method：
 * - thing.service.property.set: 设置设备属性（如continuousSendEnabled、continuousSendInterval）
 * - thing.service.property.get: 读取设备属性（返回当前传感器数据和配置）
 * - thing.service.*: 自定义服务（预留扩展）
 *
 * @param topic 消息主题
 * @param payload 消息载荷
 * @param length 载荷长度
 */
void mqttMessageCallback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }

    Serial.printf("[MQTT] 收到主题: %s\n", topic);
    Serial.printf("[MQTT] 收到内容: %s\n", message.c_str());

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, message);
    if (err) {
        Serial.printf("[MQTT] JSON解析失败: %s\n", err.c_str());
        return;
    }

    const char* method = doc["method"] | "";
    const char* id = doc["id"] | "";
    JsonObject params = doc["params"].as<JsonObject>();

    if (strcmp(method, "thing.service.property.set") == 0) {
        bool ok = true;

        if (params["continuousSendEnabled"].is<bool>()) {
            continuousSendEnabled = params["continuousSendEnabled"].as<bool>();
        }

        if (params["continuousSendInterval"].is<unsigned long>()) {
            continuousSendInterval = params["continuousSendInterval"].as<unsigned long>();
        }

        JsonDocument replyData;
        replyData["success"] = ok;

        if (publishMqttReply(topic, id, method, ok ? 0 : -1, replyData.as<JsonVariant>())) {
            Serial.println("[MQTT] property.set reply 发送成功");
        } else {
            Serial.println("[MQTT] property.set reply 发送失败");
        }

    } else if (strcmp(method, "thing.service.property.get") == 0) {
        JsonDocument replyData;

        replyData["heartRate"] = sensorData.heart_rate;
        replyData["breathingRate"] = sensorData.breath_rate;
        replyData["personDetected"] = sensorData.presence;
        replyData["humanActivity"] = sensorData.motion;
        replyData["humanDistance"] = sensorData.distance;
        replyData["sleepState"] = sensorData.sleep_state;
        replyData["continuousSendEnabled"] = continuousSendEnabled;
        replyData["continuousSendInterval"] = continuousSendInterval;

        if (publishMqttReply(topic, id, method, 0, replyData.as<JsonVariant>())) {
            Serial.println("[MQTT] property.get reply 发送成功");
        } else {
            Serial.println("[MQTT] property.get reply 发送失败");
        }

    } else if (strncmp(method, "thing.service.", 14) == 0) {
        JsonDocument replyData;
        bool ok = true;

        replyData["success"] = ok;

        if (publishMqttReply(topic, id, method, ok ? 0 : -1, replyData.as<JsonVariant>())) {
            Serial.printf("[MQTT] service reply 发送成功, method=%s\n", method);
        } else {
            Serial.printf("[MQTT] service reply 发送失败, method=%s\n", method);
        }

    } else {
        Serial.printf("[MQTT] 暂不支持 method=%s\n", method);
    }
}

/**
 * @brief 初始化MQTT客户端
 * 配置MQTT服务器地址、端口、缓冲区大小和消息回调函数
 *
 * 初始化内容：
 * 1. 获取设备MAC地址
 * 2. 设置MQTT服务器地址和端口
 * 3. 设置消息缓冲区大小为1024字节
 * 4. 注册下行消息回调函数
 */
void initMQTT() {
    deviceMacAddress = getDeviceMacAddress();
    mqttClient.setServer(mqttServer, mqttPort);
    mqttClient.setBufferSize(MQTT_MAX_PACKET_SIZE);
    mqttClient.setCallback(mqttMessageCallback);

    Serial.printf("[MQTT] broker: %s:%d\n", mqttServer, mqttPort);
    Serial.printf("[MQTT] clientId: %s\n", getMqttClientId().c_str());
    Serial.printf("[MQTT] username: %s\n", getMqttDeviceName().c_str());
}

/**
 * @brief 连接MQTT服务器
 * 使用enjoy-iot规范的身份认证方式连接，并订阅下行主题
 *
 * 注意：此函数同时承担首次连接和断线重连的职责
 * - 首次连接：checkMQTTStatus() 检测到未连接时调用
 * - 断线重连：checkMQTTStatus() 检测到断开时调用
 *
 * 连接流程：
 * 1. 检查WiFi是否已连接
 * 2. 计算clientId、username、password
 * 3. 连接MQTT服务器
 * 4. 订阅下行主题 /sys/{productKey}/{deviceName}/c/#
 *
 * 密码计算：password = MD5(productSecret + clientId)
 */
void reconnectMQTT() {
    if (!WiFi.isConnected()) {
        return;
    }

    String clientId = getMqttClientId();
    String username = getMqttDeviceName();
    String password = makeMqttPassword(clientId);

    bool connected = mqttClient.connect(clientId.c_str(), username.c_str(), password.c_str());

    if (connected) {
        Serial.printf("[MQTT] 连接成功, clientId=%s\n", clientId.c_str());

        String subTopic = getMqttSubscribeTopic();
        mqttClient.subscribe(subTopic.c_str());
        Serial.printf("[MQTT] 已订阅: %s\n", subTopic.c_str());
    } else {
        Serial.printf("[MQTT] 连接失败, state=%d\n", mqttClient.state());
    }
}

/**
 * @brief 检查MQTT连接状态
 * 如果未连接则尝试重连，并保持心跳
 *
 * 重连策略：
 * - 仅在WiFi已连接时尝试重连
 * - 每5秒尝试一次重连，避免频繁重连
 * - 调用mqttClient.loop()保持心跳和处理消息
 */
void checkMQTTStatus() {
    if (!mqttClient.connected() && WiFi.isConnected()) {
        static unsigned long lastReconnectAttempt = 0;
        unsigned long now = millis();
        if (now - lastReconnectAttempt > 5000) {
            lastReconnectAttempt = now;
            reconnectMQTT();
        }
    }
    mqttClient.loop();
}

/**
 * @brief 发送日常数据到MQTT
 * 上报当前雷达监测的实时状态数据
 *
 * 上报字段：
 * - heartRate: 心率
 * - breathingRate: 呼吸率
 * - personDetected: 人体存在
 * - humanActivity: 人体活动
 * - humanDistance: 人体距离
 * - sleepState: 睡眠状态
 * - humanPositionX/Y/Z: 人体坐标
 * - heartbeatWaveform: 心跳波形
 * - breathingWaveform: 呼吸波形
 * - abnormalState: 异常状态
 * - bedStatus: 床状态
 * - struggleAlert: 挣扎警报
 * - noOneAlert: 无人警报
 *
 * 触发条件：mqttTask中每次取到数据时调用
 */
void sendDailyDataToMQTT() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    checkMQTTStatus();

    if (!mqttClient.connected()) {
        Serial.println("[MQTT] 未连接，跳过发送日常数据");
        return;
    }

    JsonDocument doc;

    if (sensorData.heart_rate > 0) {
        doc["heartRate"] = sensorData.heart_rate;
    }

    if (sensorData.breath_rate > 0) {
        doc["breathingRate"] = sensorData.breath_rate;
    }

    doc["personDetected"] = sensorData.presence;
    doc["humanActivity"] = sensorData.motion;

    if (sensorData.distance > 0) {
        doc["humanDistance"] = sensorData.distance;
    }

    doc["sleepState"] = sensorData.sleep_state;
    doc["humanPositionX"] = sensorData.pos_x;
    doc["humanPositionY"] = sensorData.pos_y;
    doc["humanPositionZ"] = sensorData.pos_z;
    doc["heartbeatWaveform"] = (int)sensorData.heart_waveform[0];
    doc["breathingWaveform"] = (int)sensorData.breath_waveform[0];
    doc["abnormalState"] = sensorData.abnormal_state;
    doc["bedStatus"] = sensorData.bed_status;
    doc["struggleAlert"] = sensorData.struggle_alert;
    doc["noOneAlert"] = sensorData.no_one_alert;

    if (publishPropertyReport(doc, "daily")) {
        Serial.println("[MQTT] 日常数据上报成功");
    } else {
        Serial.printf("[MQTT] 日常数据上报失败, state=%d\n", mqttClient.state());
    }
}

/**
 * @brief 发送睡眠数据到MQTT
 * 上报睡眠统计数据和质量评估
 *
 * 上报字段：
 * - sleepQualityScore: 睡眠质量评分
 * - sleepQualityGrade: 睡眠质量等级
 * - totalSleepDuration: 总睡眠时长
 * - awakeDurationRatio: 清醒时长比例
 * - lightSleepRatio: 浅睡比例
 * - deepSleepRatio: 深睡比例
 * - outOfBedDuration: 离床时长
 * - outOfBedCount: 离床次数
 * - turnCount: 翻身次数
 * - avgBreathingRate: 平均呼吸率
 * - avgHeartRate: 平均心率
 * - apneaCount: 呼吸暂停次数
 * - awakeDuration: 清醒时长
 * - lightSleepDuration: 浅睡时长
 * - deepSleepDuration: 深睡时长
 *
 * 触发条件：
 * - mqttTask中每10秒调用一次
 * - 仅在sleep_state为0(深睡)或1(浅睡)时上报
 */
void sendSleepDataToMQTT() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[MQTT] WiFi未连接，跳过发送睡眠数据");
        return;
    }

    checkMQTTStatus();

    if (!mqttClient.connected()) {
        Serial.println("[MQTT] MQTT未连接，跳过发送睡眠数据");
        return;
    }

    if (sensorData.sleep_state != 0 && sensorData.sleep_state != 1) {
        Serial.printf("[MQTT] 当前不是睡眠状态，sleep_state=%d\n", sensorData.sleep_state);
        return;
    }

    JsonDocument doc;
    doc["sleepQualityScore"] = sensorData.sleep_score;
    doc["sleepQualityGrade"] = sensorData.sleep_grade;
    doc["totalSleepDuration"] = sensorData.sleep_total_time;
    doc["awakeDurationRatio"] = sensorData.awake_ratio;
    doc["lightSleepRatio"] = sensorData.light_sleep_ratio;
    doc["deepSleepRatio"] = sensorData.deep_sleep_ratio;
    doc["outOfBedDuration"] = sensorData.bed_Out_Time;
    doc["outOfBedCount"] = sensorData.turn_count;
    doc["turnCount"] = sensorData.turnover_count;
    doc["avgBreathingRate"] = sensorData.avg_breath_rate;
    doc["avgHeartRate"] = sensorData.avg_heart_rate;
    doc["apneaCount"] = sensorData.apnea_count;
    doc["abnormalState"] = sensorData.abnormal_state;
    doc["bodyMovement"] = sensorData.body_movement;
    doc["breathStatus"] = sensorData.breath_status;
    doc["sleepState"] = sensorData.sleep_state;
    doc["largeMoveRatio"] = sensorData.large_move_ratio;
    doc["smallMoveRatio"] = sensorData.small_move_ratio;
    doc["struggleAlert"] = sensorData.struggle_alert;
    doc["noOneAlert"] = sensorData.no_one_alert;
    doc["awakeDuration"] = sensorData.awake_time;
    doc["lightSleepDuration"] = sensorData.light_sleep_time;
    doc["deepSleepDuration"] = sensorData.deep_sleep_time;

    if (publishPropertyReport(doc, "sleep")) {
        Serial.println("[MQTT] 睡眠数据上报成功");
    } else {
        Serial.printf("[MQTT] 睡眠数据上报失败, state=%d\n", mqttClient.state());
    }
}

/**
 * @brief 发送心跳包到MQTT
 * 当无人时发送轻量心跳，防止平台判定设备离线
 * 
 * 上报字段：
 * - personDetected: 0 (无人)
 * - heartbeat: 1 (心跳标识)
 * - timestamp: 时间戳
 * - sleepState: 睡眠状态
 * - noOneAlert: 无人警报
 * 
 * 触发条件：
 * - mqttTask中心率和呼吸率都为0时，每30秒发送一次
 */
void sendHeartbeatToMQTT() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    checkMQTTStatus();

    if (!mqttClient.connected()) {
        return;
    }

    JsonDocument doc;
    doc["personDetected"] = 0;
    doc["heartbeat"] = 1;
    doc["timestamp"] = millis();
    doc["sleepState"] = sensorData.sleep_state;
    doc["noOneAlert"] = sensorData.no_one_alert;

    if (publishPropertyReport(doc, "heartbeat")) {
        Serial.println("[MQTT] 心跳包发送成功");
    } else {
        Serial.printf("[MQTT] 心跳包发送失败, state=%d\n", mqttClient.state());
    }
}

/**
 * @brief MQTT任务
 * 在FreeRTOS任务中处理所有MQTT功能：
 * - 保持MQTT连接心跳
 * - 处理MQTT消息回调
 * - 定时发送日常数据和睡眠数据
 * @param parameter 任务参数（未使用）
 */
void mqttTask(void *parameter) {
    Serial.println("📡 MQTT任务启动");

    initMQTT();

    while (1) {
        esp_task_wdt_reset();

        if (WiFi.status() == WL_CONNECTED) {
            checkMQTTStatus();

            if (mqttClient.connected()) {
                unsigned long currentTime = millis();

                // 结合存在检测和生命体征检测结果
                bool isNoOne = !sensorData.presence || (sensorData.heart_rate == 0 && sensorData.breath_rate == 0);
                
                if (isNoOne) {
                    // 无人时发送心跳包，防止平台判定离线
                    if (currentTime - lastHeartbeatTime >= HEARTBEAT_INTERVAL) {
                        Serial.println("🔄 [MQTT] 检测到无人状态，发送心跳包");
                        sendHeartbeatToMQTT();
                        lastHeartbeatTime = currentTime;
                    }
                } else if (currentTime - lastDailyDataTime >= DAILY_DATA_INTERVAL) {
                    // 有人时发送日常数据
                    Serial.println("📊 [MQTT] 检测到有人状态，发送日常数据");
                    sendDailyDataToMQTT();
                    lastDailyDataTime = currentTime;
                }

                esp_task_wdt_reset();

                if (currentTime - lastSleepDataTime >= SLEEP_DATA_INTERVAL) {
                    sendSleepDataToMQTT();
                    lastSleepDataTime = currentTime;
                }
            } else {
                static unsigned long lastWifiCheck = 0;
                if (millis() - lastWifiCheck > 10000) {
                    lastWifiCheck = millis();
                }
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}