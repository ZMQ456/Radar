#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>

enum OtaState {
    OTA_IDLE = 0,
    OTA_NOTIFIED,
    OTA_VALIDATING,
    OTA_READY,
    OTA_DOWNLOADING,
    OTA_VERIFYING,
    OTA_WRITING,
    OTA_PENDING_REBOOT,
    OTA_SUCCESS,
    OTA_REJECTED,
    OTA_UNSUPPORTED_PROTOCOL,
    OTA_FAILED
};

struct OtaUpgradeTask {
    String id;
    int code;
    String message;
    String version;
    String module;
    String signMethod;
    String md5;
    String sign;
    String url;
    String extData;
    uint32_t size;
    bool isDiff;
    unsigned long receivedAt;
    String rawPayload;
};

void initOtaManager();//初始化OTA管理器
void resetOtaTask();//重置OTA任务
bool hasPendingOtaTask();//检查是否有待处理的OTA任务
OtaState getOtaState();//获取当前OTA状态
const OtaUpgradeTask& getCurrentOtaTask();//获取当前OTA任务
bool hasExecutableOtaTask();//检查是否有可执行的OTA任务

bool parseOtaUpgradeMessage(const String& payload, OtaUpgradeTask& task, String& errorMsg);//解析OTA升级消息
bool validateOtaUpgradeTask(const OtaUpgradeTask& task, String& errorMsg, int& errorStep);//验证OTA升级任务
void storeOtaTask(const OtaUpgradeTask& task);//存储OTA任务
void markOtaState(OtaState state);//标记OTA状态 

#endif
