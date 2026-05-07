#ifndef BLE_TLV_PROTOCOL_H
#define BLE_TLV_PROTOCOL_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

namespace BleProto {

static const uint8_t SOF1 = 0xAA;
static const uint8_t SOF2 = 0x55;
static const uint8_t VERSION = 0x01;//协议版本

// flags
static const uint8_t FLAG_FRAGMENT = 0x01;//是否为分片
static const uint8_t FLAG_NEED_ACK = 0x02;//是否需要确认
static const uint8_t FLAG_IS_ACK = 0x04;//是否为确认
static const uint8_t FLAG_IS_ERROR = 0x08;//是否为错误

// cmd
enum Command : uint8_t {
    CMD_PING_REQ = 0x01,//Ping请求
    CMD_PING_RESP = 0x02,//Ping响应

    CMD_QUERY_STATUS_REQ = 0x10,//查询状态请求
    CMD_STATUS_RESP = 0x11,//状态响应
    CMD_QUERY_RADAR_REQ = 0x12,//查询雷达请求
    CMD_RADAR_RESP = 0x13,//雷达响应
    CMD_START_CONTINUOUS_REQ = 0x14,//启动连续请求
    CMD_START_CONTINUOUS_RESP = 0x15,//启动连续响应
    CMD_STOP_CONTINUOUS_REQ = 0x16,//停止连续请求
    CMD_STOP_CONTINUOUS_RESP = 0x17,//停止连续响应
    CMD_CONTINUOUS_PUSH = 0x18,//连续推送

    CMD_WIFI_SCAN_REQ = 0x20,//WiFi扫描请求
    CMD_WIFI_SCAN_RESP = 0x21,//WiFi扫描响应
    CMD_WIFI_CONFIG_REQ = 0x22,//WiFi配置请求
    CMD_WIFI_CONFIG_RESP = 0x23,//WiFi配置响应
    CMD_GET_SAVED_WIFI_REQ = 0x24,//获取保存的WiFi请求
    CMD_GET_SAVED_WIFI_RESP = 0x25,//获取保存的WiFi响应

    CMD_SET_DEVICE_ID_REQ = 0x30,//设置设备ID请求
    CMD_SET_DEVICE_ID_RESP = 0x31,//设置设备ID响应

    CMD_ERROR_RESP = 0x7E,//错误响应
    CMD_ACK = 0x7F//确认
};

// tlv type
enum TlvType : uint8_t {
    TLV_DEVICE_ID = 0x01,//设备ID
    TLV_RESULT_CODE = 0x02,//结果码
    TLV_ERROR_MESSAGE = 0x03,//错误信息
    TLV_TIMESTAMP = 0x04,//时间戳
    TLV_PROTOCOL_VERSION = 0x05,//协议版本

    TLV_HEART_RATE_X10 = 0x10,//心率（x10）
    TLV_BREATH_RATE_X10 = 0x11,//呼吸率（x10）
    TLV_PRESENCE = 0x12,//存在
    TLV_MOTION = 0x13,//运动
    TLV_SLEEP_STATE = 0x14,//睡眠状态
    TLV_DISTANCE_CM = 0x15,//距离（cm）
    TLV_POS_X_MM = 0x16,//X坐标（mm）
    TLV_POS_Y_MM = 0x17,//Y坐标（mm）
    TLV_POS_Z_MM = 0x18,//Z坐标（mm）

    TLV_SSID = 0x20,//SSID
    TLV_PASSWORD = 0x21,//密码
    TLV_WIFI_COUNT = 0x22,//WiFi数量
    TLV_WIFI_ITEM = 0x23,//WiFi项
    TLV_RSSI = 0x24,//RSSI
    TLV_SECURITY = 0x25,//安全类型（uint8，见WifiSecurityType枚举）

    TLV_INTERVAL_MS = 0x31,//间隔时间（毫秒）

    TLV_MESSAGE = 0x40,//消息
    TLV_IP_ADDRESS = 0x41,//IP地址
    TLV_WIFI_CONFIGURED = 0x42,//WiFi配置
    TLV_WIFI_CONNECTED = 0x43,//WiFi连接
    TLV_ECHO_CONTENT = 0x44,//回显内容
};

// WiFi安全类型枚举
enum WifiSecurityType : uint8_t {
    WIFI_SEC_OPEN = 0,      // 开放网络
    WIFI_SEC_WEP = 1,      // WEP加密
    WIFI_SEC_WPA = 2,      // WPA加密
    WIFI_SEC_WPA2 = 3,     // WPA2加密
    WIFI_SEC_WPA3 = 4,     // WPA3加密
    WIFI_SEC_UNKNOWN = 255 // 未知类型
};

struct Frame {
    uint8_t version = VERSION;//协议版本
    uint8_t cmd = 0;//命令
    uint8_t flags = 0;//标志位
    uint8_t seq = 0;//序列号
    std::vector<uint8_t> data;//数据
};

class FrameParser {
public:
    FrameParser();
    bool input(const uint8_t* bytes, size_t len, Frame& outFrame);
    void reset();

private:
    std::vector<uint8_t> buffer;
    bool tryParseOne(Frame& outFrame);
};

uint16_t crc16Ccitt(const uint8_t* data, size_t len);//计算CRC16-CCITT校验和

void appendU8(std::vector<uint8_t>& out, uint8_t value);//添加8位无符号整数
void appendU16(std::vector<uint8_t>& out, uint16_t value);//添加16位无符号整数
void appendI16(std::vector<uint8_t>& out, int16_t value);//添加16位有符号整数
void appendU32(std::vector<uint8_t>& out, uint32_t value);//添加32位无符号整数
void appendBytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len);//添加字节数组
void appendString(std::vector<uint8_t>& out, const String& s);//添加字符串

void appendTlvU8(std::vector<uint8_t>& out, uint8_t type, uint8_t value);//添加8位无符号整数TLV
void appendTlvU16(std::vector<uint8_t>& out, uint8_t type, uint16_t value);//添加16位无符号整数TLV
void appendTlvI16(std::vector<uint8_t>& out, uint8_t type, int16_t value);//添加16位有符号整数TLV
void appendTlvU32(std::vector<uint8_t>& out, uint8_t type, uint32_t value);//添加32位无符号整数TLV
void appendTlvString(std::vector<uint8_t>& out, uint8_t type, const String& value);//添加字符串TLV
void appendTlvBlock(std::vector<uint8_t>& out, uint8_t type, const std::vector<uint8_t>& value);//添加块TLV

bool readTlv(const std::vector<uint8_t>& data, size_t& offset, uint8_t& type, uint16_t& len, const uint8_t*& value);

std::vector<uint8_t> encodeFrame(const Frame& frame);

// 入站：TLV -> 旧 JSON 命令
bool decodeFrameToLegacyJson(const Frame& frame, String& legacyJson);//将TLV帧解码为旧JSON命令

// 出站：旧 JSON 响应 -> TLV Frame
bool encodeLegacyJsonToFrame(const String& json, uint8_t seq, Frame& frame);//将旧JSON响应编码为TLV帧

} // namespace BleProto

#endif
