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

void initOtaManager();
void resetOtaTask();
bool hasPendingOtaTask();
OtaState getOtaState();
const OtaUpgradeTask& getCurrentOtaTask();
bool hasExecutableOtaTask();

bool parseOtaUpgradeMessage(const String& payload, OtaUpgradeTask& task, String& errorMsg);
bool validateOtaUpgradeTask(const OtaUpgradeTask& task, String& errorMsg, int& errorStep);
void storeOtaTask(const OtaUpgradeTask& task);
void markOtaState(OtaState state);

#endif
