#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>
#include <Preferences.h>

#undef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 1024

class WiFiManager;

extern uint64_t device_sn;
extern Preferences preferences;
extern WiFiManager wifiManager;
extern bool continuousSendEnabled;
extern unsigned long continuousSendInterval;

extern const char* mqttServer;
extern const int mqttPort;
extern const char* mqttProductKey;
extern const char* mqttDeviceModel;
extern const char* mqttProductSecret;
extern String deviceMacAddress;

extern WiFiClient mqttWiFiClient;
extern PubSubClient mqttClient;

extern TaskHandle_t mqttTaskHandle;

void mqttTask(void *parameter);
String getMqttDeviceName();
String getMqttClientId();
String getMqttSubscribeTopic();
String getMqttPropertyPostTopic();
String makeMqttPassword(const String& clientId);
String buildReplyTopic(const char* requestTopic);
bool publishMqttReply(const char* requestTopic, const char* requestId, const char* requestMethod, int code, JsonVariant data);
void mqttMessageCallback(char* topic, byte* payload, unsigned int length);
void initMQTT();
void reconnectMQTT();
void checkMQTTStatus();
void sendDailyDataToMQTT();
void sendSleepDataToMQTT();
void sendHeartbeatToMQTT();//发送心跳包

// OTA相关函数
String getOtaUpgradeTopic();
String getOtaProgressTopic();
String getOtaVersionReportTopic();
String getOtaResultInformTopic();
bool publishOtaVersionReport();
bool publishOtaResultInform(const char* version, const char* module);
bool publishOtaProgress(const char* requestId, int step, const char* desc, const char* module);
bool handleOtaUpgradeMessage(const char* topic, const String& payload);

#endif
