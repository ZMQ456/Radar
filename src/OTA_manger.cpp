#include "OTA_manager.h"
#include "version.h"

static OtaState currentOtaState = OTA_IDLE;// 当前OTA状态
static OtaUpgradeTask currentOtaTask;// 当前OTA升级任务

/**
 * @brief 检查OTA状态码是否被接受
 * @param codeValue OTA状态码
 * @return 是否被接受
 */
static bool isAcceptedOtaCode(const JsonVariantConst& codeValue) {
    if (codeValue.is<int>()) {// 有些平台返回的code是整数，有些是字符串，这里兼容两种情况
        int code = codeValue.as<int>();//它需要通过 as<int>() 进行类型转换/提取，而不是直接使用 as<int>() 的结果进行比较，因为 as<int>() 返回的是一个临时对象，而不是一个直接的整数值。通过将其赋值给一个变量，我们可以确保在比较时使用的是一个稳定的整数值，而不是一个临时对象。这也是C++中常见的类型转换和比较的正确方式。
        return code == 200 || code == 1000;
    }

    String code = codeValue.as<String>();
    return code == "200" || code == "1000";// 200表示有新版本，1000表示当前版本已是最新，无需升级
}

/**
 * @brief 保存待处理的OTA结果
 * @param task OTA升级任务对象
 */
void initOtaManager() {
    resetOtaTask();
    Serial.println("[OTA] manager initialized");
}

/**
 * @brief 重置OTA任务状态
 */
void resetOtaTask() {
    currentOtaState = OTA_IDLE;
    currentOtaTask = OtaUpgradeTask{};// 重置为默认构造的空任务对象
}

/**
 * @brief 检查是否有待处理的OTA任务
 * @return 是否有待处理的OTA任务
 */
bool hasPendingOtaTask() {
    return !currentOtaTask.id.isEmpty();// 通过检查当前OTA任务的ID是否为空来判断是否有待处理的OTA任务
}

/**
 * @brief 获取当前OTA状态
 * @return 当前OTA状态
 */
OtaState getOtaState() {
    return currentOtaState;
}

/**
 * @brief 获取当前OTA任务对象
 * @return 当前OTA任务对象的常量引用
 */
const OtaUpgradeTask& getCurrentOtaTask() {
    return currentOtaTask;
}

/**
 * @brief 检查是否有可执行的OTA任务
 * @return 是否有可执行的OTA任务
 */
bool hasExecutableOtaTask() {
    return currentOtaState == OTA_READY;
}

/**
 * @brief 标记OTA状态
 * @param state 要标记的OTA状态
 */
void markOtaState(OtaState state) {
    currentOtaState = state;
}

/**
 * @brief 存储OTA任务
 * @param task OTA升级任务对象
 */
void storeOtaTask(const OtaUpgradeTask& task) {
    currentOtaTask = task;
    currentOtaTask.receivedAt = millis();
}

/**
 * @brief 保存待处理的OTA结果
 * 用于在OTA升级完成后保存结果，等待设备重启后上报
 * @param task OTA升级任务对象
 */
bool parseOtaUpgradeMessage(const String& payload, OtaUpgradeTask& task, String& errorMsg) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);// 反序列化JSON字符串到JsonDocument对象中，如果失败则返回错误信息
    if (error) {
        errorMsg = "OTA message JSON parse failed";
        return false;
    }

    if (doc["id"].isNull()) {
        errorMsg = "Missing OTA request id";
        return false;
    }

    JsonObject data = doc["data"].as<JsonObject>();
    if (data.isNull()) {
        errorMsg = "Missing OTA data field";
        return false;
    }

    task = OtaUpgradeTask{};
    task.id = doc["id"].as<String>();
    if (doc["code"].is<int>()) {
        task.code = doc["code"].as<int>();
    } else {
        String code = doc["code"].as<String>();
        task.code = code.toInt();
    }
    task.message = doc["message"].as<String>();
    task.version = data["version"].as<String>();
    task.module = data["module"].as<String>();
    task.signMethod = data["signMethod"].as<String>();
    task.md5 = data["md5"].as<String>();
    task.sign = data["sign"].as<String>();
    task.url = data["url"].as<String>();
    task.extData = data["extData"].as<String>();
    task.size = data["size"] | 0;
    task.isDiff = (data["isDiff"] | 0) == 1;
    task.rawPayload = payload;
    task.receivedAt = millis();

    if (task.module.isEmpty()) {
        task.module = OTA_MODULE_NAME;
    }

    return true;
}

/**
 * @brief 验证OTA任务的有效性
 * @param task OTA升级任务对象
 * @param errorMsg 验证失败时的错误消息输出参数
 * @param errorStep 验证失败时的错误步骤输出参数，-1表示通用错误，其他值表示特定验证步骤的错误
 * @return OTA任务是否有效
 */
bool validateOtaUpgradeTask(const OtaUpgradeTask& task, String& errorMsg, int& errorStep) {
    if (task.id.isEmpty()) {
        errorMsg = "Missing OTA request id";
        errorStep = -1;
        return false;
    }

    JsonDocument rawDoc;
    if (deserializeJson(rawDoc, task.rawPayload) || !isAcceptedOtaCode(rawDoc["code"])) {
        errorMsg = "Invalid OTA task status";
        errorStep = -1;
        return false;
    }

    if (!task.module.isEmpty() && task.module != OTA_MODULE_NAME) {
        errorMsg = "OTA module mismatch";
        errorStep = -1;
        return false;
    }

    if (task.version.isEmpty()) {
        errorMsg = "Missing target version";
        errorStep = -1;
        return false;
    }

    if (task.size == 0) {
        errorMsg = "Invalid firmware size";
        errorStep = -1;
        return false;
    }

    if (task.isDiff) {
        errorMsg = "Diff OTA is not supported";
        errorStep = -2;
        return false;
    }

    if (!task.signMethod.isEmpty() && task.signMethod != "MD5") {
        errorMsg = "Only MD5 verification is supported";
        errorStep = -3;
        return false;
    }

    if (task.md5.isEmpty() && task.sign.isEmpty()) {
        errorMsg = "Missing OTA checksum";
        errorStep = -3;
        return false;
    }

    if (task.url.isEmpty()) {
        errorMsg = "Missing OTA package url";
        errorStep = -2;
        return false;
    }

    return true;
}
