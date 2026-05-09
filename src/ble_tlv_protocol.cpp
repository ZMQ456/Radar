#include "ble_tlv_protocol.h"

namespace BleProto {//BLE协议命名空间

static uint16_t readBe16(const uint8_t* p) {
    return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}

static int16_t readBeI16(const uint8_t* p) {
    return static_cast<int16_t>(readBe16(p));
}

static uint32_t readBe32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           p[3];
}

FrameParser::FrameParser() {}

void FrameParser::reset() {
    buffer.clear();
}

bool FrameParser::input(const uint8_t* bytes, size_t len, Frame& outFrame) {
    if (bytes != nullptr && len > 0) {
        buffer.insert(buffer.end(), bytes, bytes + len);
    }
    return tryParseOne(outFrame);
}

bool FrameParser::tryParseOne(Frame& outFrame) {
    while (buffer.size() >= 2) {
        if (buffer[0] == SOF1 && buffer[1] == SOF2) {
            break;
        }
        buffer.erase(buffer.begin());
    }

    if (buffer.size() < 10) {
        return false;
    }

    uint16_t dataLen = readBe16(&buffer[6]);
    size_t fullLen = 2 + 1 + 1 + 1 + 1 + 2 + dataLen + 2;
    if (buffer.size() < fullLen) {
        return false;
    }

    uint16_t expectedCrc = readBe16(&buffer[fullLen - 2]);
    uint16_t actualCrc = crc16Ccitt(&buffer[2], fullLen - 4);
    if (expectedCrc != actualCrc) {
        buffer.erase(buffer.begin());
        return false;
    }

    outFrame.version = buffer[2];
    outFrame.cmd = buffer[3];
    outFrame.flags = buffer[4];
    outFrame.seq = buffer[5];
    outFrame.data.assign(buffer.begin() + 8, buffer.begin() + 8 + dataLen);

    buffer.erase(buffer.begin(), buffer.begin() + fullLen);
    return true;
}

uint16_t crc16Ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

void appendU8(std::vector<uint8_t>& out, uint8_t value) {
    out.push_back(value);
}

void appendU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendI16(std::vector<uint8_t>& out, int16_t value) {
    appendU16(out, static_cast<uint16_t>(value));
}

void appendU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void appendBytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len) {
    out.insert(out.end(), data, data + len);
}

void appendString(std::vector<uint8_t>& out, const String& s) {
    appendBytes(out, reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
}

void appendTlvU8(std::vector<uint8_t>& out, uint8_t type, uint8_t value) {
    out.push_back(type);
    appendU16(out, 1);
    out.push_back(value);
}

void appendTlvU16(std::vector<uint8_t>& out, uint8_t type, uint16_t value) {
    out.push_back(type);
    appendU16(out, 2);
    appendU16(out, value);
}

void appendTlvI16(std::vector<uint8_t>& out, uint8_t type, int16_t value) {
    out.push_back(type);
    appendU16(out, 2);
    appendI16(out, value);
}

void appendTlvU32(std::vector<uint8_t>& out, uint8_t type, uint32_t value) {
    out.push_back(type);
    appendU16(out, 4);
    appendU32(out, value);
}

void appendTlvString(std::vector<uint8_t>& out, uint8_t type, const String& value) {
    out.push_back(type);
    appendU16(out, static_cast<uint16_t>(value.length()));
    appendString(out, value);
}

void appendTlvBlock(std::vector<uint8_t>& out, uint8_t type, const std::vector<uint8_t>& value) {
    out.push_back(type);
    appendU16(out, static_cast<uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

bool readTlv(const std::vector<uint8_t>& data, size_t& offset, uint8_t& type, uint16_t& len, const uint8_t*& value) {
    if (offset + 3 > data.size()) {
        return false;
    }

    type = data[offset++];
    len = readBe16(&data[offset]);
    offset += 2;

    if (offset + len > data.size()) {
        return false;
    }

    value = &data[offset];
    offset += len;
    return true;
}

std::vector<uint8_t> encodeFrame(const Frame& frame) {
    std::vector<uint8_t> out;
    out.push_back(SOF1);
    out.push_back(SOF2);
    out.push_back(frame.version);
    out.push_back(frame.cmd);
    out.push_back(frame.flags);
    out.push_back(frame.seq);
    appendU16(out, static_cast<uint16_t>(frame.data.size()));
    out.insert(out.end(), frame.data.begin(), frame.data.end());

    uint16_t crc = crc16Ccitt(&out[2], out.size() - 2);
    appendU16(out, crc);
    return out;
}

static String bytesToString(const uint8_t* data, uint16_t len) {
    String out;
    out.reserve(len);
    for (uint16_t i = 0; i < len; ++i) {
        out += static_cast<char>(data[i]);
    }
    return out;
}

static bool decodeCommandPayloadToJson(const Frame& frame, JsonDocument& doc) {
    size_t offset = 0;
    uint8_t type = 0;
    uint16_t len = 0;
    const uint8_t* value = nullptr;

    switch (frame.cmd) {
        case CMD_QUERY_STATUS_REQ:
            doc["command"] = "queryStatus";
            return true;

        case CMD_QUERY_RADAR_REQ:
            doc["command"] = "queryRadarData";
            return true;

        case CMD_START_CONTINUOUS_REQ:
            doc["command"] = "startContinuousSend";
            while (readTlv(frame.data, offset, type, len, value)) {
                if (type == TLV_INTERVAL_MS && len == 2) {
                    doc["interval"] = readBe16(value);
                }
            }
            return true;

        case CMD_STOP_CONTINUOUS_REQ:
            doc["command"] = "stopContinuousSend";
            return true;

        case CMD_WIFI_SCAN_REQ:
            doc["command"] = "scanWiFi";
            return true;

        case CMD_GET_SAVED_WIFI_REQ:
            doc["command"] = "getSavedNetworks";
            return true;

        case CMD_WIFI_CONFIG_REQ:
            doc["command"] = "setWiFiConfig";
            while (readTlv(frame.data, offset, type, len, value)) {
                if (type == TLV_SSID) {
                    doc["ssid"] = bytesToString(value, len);
                } else if (type == TLV_PASSWORD) {
                    doc["password"] = bytesToString(value, len);
                }
            }
            return true;

        case CMD_SET_DEVICE_ID_REQ:
            doc["command"] = "setDeviceId";
            while (readTlv(frame.data, offset, type, len, value)) {
                if (type == TLV_DEVICE_ID && len > 0) {
                    String s = bytesToString(value, len);
                    doc["newDeviceId"] = s.toInt();
                }
            }
            return true;

        default:
            return false;
    }
}

bool decodeFrameToLegacyJson(const Frame& frame, String& legacyJson) {
    JsonDocument doc;
    if (!decodeCommandPayloadToJson(frame, doc)) {
        return false;
    }
    serializeJson(doc, legacyJson);
    return true;
}

static WifiSecurityType securityStringToEnum(const String& securityStr) {
    if (securityStr == "OPEN") return WIFI_SEC_OPEN;
    if (securityStr == "WEP") return WIFI_SEC_WEP;
    if (securityStr == "WPA" || securityStr == "WPA/WPA2") return WIFI_SEC_WPA;
    if (securityStr == "WPA2" || securityStr == "WPA2-PSK") return WIFI_SEC_WPA2;
    if (securityStr == "WPA3") return WIFI_SEC_WPA3;
    return WIFI_SEC_UNKNOWN;
}

static void encodeWifiItems(JsonVariantConst networks, std::vector<uint8_t>& data) {
    if (!networks.is<JsonArrayConst>()) {
        return;
    }

    JsonArrayConst arr = networks.as<JsonArrayConst>();
    appendTlvU16(data, TLV_WIFI_COUNT, static_cast<uint16_t>(arr.size()));

    for (JsonVariantConst item : arr) {
        std::vector<uint8_t> block;
        if (item["ssid"].is<const char*>()) {
            appendTlvString(block, TLV_SSID, String(item["ssid"].as<const char*>()));
        }
        if (item["rssi"].is<int>()) {
            int rssi = item["rssi"].as<int>();
            appendTlvU8(block, TLV_RSSI, static_cast<uint8_t>(static_cast<int8_t>(rssi)));
        }
        if (item["security"].is<const char*>()) {
            WifiSecurityType sec = securityStringToEnum(String(item["security"].as<const char*>()));
            appendTlvU8(block, TLV_SECURITY, static_cast<uint8_t>(sec));
        }
        appendTlvBlock(data, TLV_WIFI_ITEM, block);
    }
}

static uint16_t toX10(float v) {
    if (v <= 0) return 0;
    return static_cast<uint16_t>(v * 10.0f + 0.5f);
}

bool encodeLegacyJsonToFrame(const String& json, uint8_t seq, Frame& frame) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) {
        return false;
    }

    const char* type = doc["type"];
    if (!type) {
        return false;
    }

    frame.version = VERSION;
    frame.flags = 0;
    frame.seq = seq;
    frame.data.clear();

    String t(type);

    if (t == "status" || t == "deviceStatus") {
        frame.cmd = CMD_STATUS_RESP;
        appendTlvU8(frame.data, TLV_RESULT_CODE, ErrorCode::SUCCESS);
        appendTlvU8(frame.data, TLV_STATE, State::SUCCESS);
        appendTlvU8(frame.data, TLV_STEP, Step::COMPLETED);
        appendTlvString(frame.data, TLV_DEVICE_ID, String(doc["deviceId"] | 0));
        appendTlvU8(frame.data, TLV_WIFI_CONFIGURED, (doc["wifiConfigured"] | false) ? 1 : 0);
        appendTlvU8(frame.data, TLV_WIFI_CONNECTED, (doc["wifiConnected"] | false) ? 1 : 0);
        appendTlvString(frame.data, TLV_IP_ADDRESS, String(doc["ipAddress"] | ""));
        return true;
    }

    if (t == "radarData") {
        frame.cmd = CMD_RADAR_RESP;
        appendTlvU8(frame.data, TLV_RESULT_CODE, (doc["success"] | false) ? ErrorCode::SUCCESS : ErrorCode::ERR_RADAR_NO_DATA);
        appendTlvU8(frame.data, TLV_STATE, (doc["success"] | false) ? State::SUCCESS : State::FAILED);
        appendTlvU32(frame.data, TLV_TIMESTAMP, static_cast<uint32_t>(doc["timestamp"] | 0));
        appendTlvString(frame.data, TLV_DEVICE_ID, String(doc["deviceId"] | 0));
        appendTlvU8(frame.data, TLV_PRESENCE, static_cast<uint8_t>(doc["presence"] | 0));
        appendTlvU16(frame.data, TLV_HEART_RATE_X10, toX10(doc["heartRate"] | 0.0f));
        appendTlvU16(frame.data, TLV_BREATH_RATE_X10, toX10(doc["breathRate"] | 0.0f));
        appendTlvU8(frame.data, TLV_MOTION, static_cast<uint8_t>(doc["motion"] | 0));
        appendTlvU16(frame.data, TLV_DISTANCE_CM, static_cast<uint16_t>(doc["distance"] | 0));
        appendTlvU8(frame.data, TLV_SLEEP_STATE, static_cast<uint8_t>(doc["sleepState"] | 0));
        return true;
    }

    if (t == "startContinuousSendResult") {
        frame.cmd = CMD_START_CONTINUOUS_RESP;
        appendTlvU8(frame.data, TLV_RESULT_CODE, (doc["success"] | false) ? ErrorCode::SUCCESS : ErrorCode::ERR_DEV_STATE_INVALID);
        appendTlvU8(frame.data, TLV_STATE, (doc["success"] | false) ? State::SUCCESS : State::FAILED);
        if (doc["interval"].is<int>()) {
            appendTlvU16(frame.data, TLV_INTERVAL_MS, static_cast<uint16_t>(doc["interval"].as<int>()));
        }
        if (doc["message"].is<const char*>()) {
            appendTlvString(frame.data, TLV_MESSAGE, String(doc["message"].as<const char*>()));
        }
        return true;
    }

    if (t == "stopContinuousSendResult") {
        frame.cmd = CMD_STOP_CONTINUOUS_RESP;
        appendTlvU8(frame.data, TLV_RESULT_CODE, (doc["success"] | false) ? ErrorCode::SUCCESS : ErrorCode::ERR_DEV_STATE_INVALID);
        appendTlvU8(frame.data, TLV_STATE, (doc["success"] | false) ? State::SUCCESS : State::FAILED);
        if (doc["message"].is<const char*>()) {
            appendTlvString(frame.data, TLV_MESSAGE, String(doc["message"].as<const char*>()));
        }
        return true;
    }

    if (t == "wifiConfigResult" || t == "wifiConnected") {
        frame.cmd = CMD_WIFI_CONFIG_RESP;
        bool success = doc["success"] | false;
        
        // 根据消息内容判断具体的错误码
        String message = String(doc["message"] | "");
        uint8_t resultCode = ErrorCode::SUCCESS;
        uint8_t state = State::SUCCESS;
        uint8_t step = Step::COMPLETED;
        
        if (!success) {
            state = State::FAILED;
            if (message.indexOf("正在被其他操作占用") >= 0) {
                resultCode = ErrorCode::ERR_WIFI_BUSY;
                step = Step::RECEIVED;
            } else if (message.indexOf("扫描超时") >= 0) {
                resultCode = ErrorCode::ERR_WIFI_SCAN_TIMEOUT;
                step = Step::SCANNING;
            } else if (message.indexOf("未扫描到任何WiFi") >= 0) {
                resultCode = ErrorCode::ERR_WIFI_SSID_NOT_FOUND;
                step = Step::SCANNING;
            } else if (message.indexOf("信号过弱") >= 0) {
                resultCode = ErrorCode::ERR_WIFI_SIGNAL_WEAK;
                step = Step::SCANNING;
            } else if (message.indexOf("未找到目标WiFi") >= 0) {
                resultCode = ErrorCode::ERR_WIFI_SSID_NOT_FOUND;
                step = Step::SCANNING;
            } else if (message.indexOf("密码") >= 0) {
                resultCode = ErrorCode::ERR_WIFI_WRONG_PASSWORD;
                step = Step::CONNECTING_AP;
            } else {
                resultCode = ErrorCode::ERR_WIFI_CONNECT_TIMEOUT;
                step = Step::CONNECTING_AP;
            }
        }
        
        appendTlvU8(frame.data, TLV_RESULT_CODE, resultCode);
        appendTlvU8(frame.data, TLV_STATE, state);
        appendTlvU8(frame.data, TLV_STEP, step);
        
        if (doc["message"].is<const char*>()) {
            appendTlvString(frame.data, TLV_MESSAGE, String(doc["message"].as<const char*>()));
        }
        if (doc["ssid"].is<const char*>()) {
            appendTlvString(frame.data, TLV_SSID, String(doc["ssid"].as<const char*>()));
        }
        if (doc["ipAddress"].is<const char*>()) {
            appendTlvString(frame.data, TLV_IP_ADDRESS, String(doc["ipAddress"].as<const char*>()));
        }
        return true;
    }

    if (t == "scanWiFiResult") {
        frame.cmd = CMD_WIFI_SCAN_RESP;
        appendTlvU8(frame.data, TLV_RESULT_CODE, (doc["success"] | false) ? ErrorCode::SUCCESS : ErrorCode::ERR_WIFI_SCAN_TIMEOUT);
        appendTlvU8(frame.data, TLV_STATE, (doc["success"] | false) ? State::SUCCESS : State::FAILED);
        encodeWifiItems(doc["networks"], frame.data);
        return true;
    }

    if (t == "savedNetworksResult" || t == "savedNetworks") {
        frame.cmd = CMD_GET_SAVED_WIFI_RESP;
        appendTlvU8(frame.data, TLV_RESULT_CODE, (doc["success"] | false) ? ErrorCode::SUCCESS : ErrorCode::ERR_DEV_STORAGE_FAIL);
        appendTlvU8(frame.data, TLV_STATE, (doc["success"] | false) ? State::SUCCESS : State::FAILED);
        encodeWifiItems(doc["networks"], frame.data);
        return true;
    }

    if (t == "setDeviceIdResult") {
        frame.cmd = CMD_SET_DEVICE_ID_RESP;
        appendTlvU8(frame.data, TLV_RESULT_CODE, (doc["success"] | false) ? ErrorCode::SUCCESS : ErrorCode::ERR_PROTO_PARAM_INVALID);
        appendTlvU8(frame.data, TLV_STATE, (doc["success"] | false) ? State::SUCCESS : State::FAILED);
        if (doc["newDeviceId"].is<int>()) {
            appendTlvString(frame.data, TLV_DEVICE_ID, String(doc["newDeviceId"].as<int>()));
        }
        if (doc["message"].is<const char*>()) {
            appendTlvString(frame.data, TLV_MESSAGE, String(doc["message"].as<const char*>()));
        }
        return true;
    }

    if (t == "echoResponse" || t == "rawEchoResponse") {
        frame.cmd = CMD_PING_RESP;
        appendTlvU8(frame.data, TLV_RESULT_CODE, ErrorCode::SUCCESS);
        appendTlvU8(frame.data, TLV_STATE, State::SUCCESS);
        if (doc["originalContent"].is<const char*>()) {
            appendTlvString(frame.data, TLV_ECHO_CONTENT, String(doc["originalContent"].as<const char*>()));
        } else if (doc["originalData"].is<const char*>()) {
            appendTlvString(frame.data, TLV_ECHO_CONTENT, String(doc["originalData"].as<const char*>()));
        } else if (doc["message"].is<const char*>()) {
            appendTlvString(frame.data, TLV_MESSAGE, String(doc["message"].as<const char*>()));
        }
        return true;
    }

    if (t == "error") {
        frame.cmd = CMD_ERROR_RESP;
        frame.flags |= FLAG_IS_ERROR;
        appendTlvU8(frame.data, TLV_RESULT_CODE, ErrorCode::UNKNOWN);
        appendTlvU8(frame.data, TLV_STATE, State::FAILED);
        appendTlvString(frame.data, TLV_ERROR_MESSAGE, String(doc["message"] | "unknown error"));
        return true;
    }

    return false;
}

} // namespace BleProto
