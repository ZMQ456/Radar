#ifndef BLE_TLV_PROTOCOL_H
#define BLE_TLV_PROTOCOL_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

namespace BleProto {

static const uint8_t SOF1 = 0xAA;//帧头
static const uint8_t SOF2 = 0x55;//
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
    TLV_DEVICE_SN = 0x06,//设备序列号 uint64
    TLV_FIRMWARE_VERSION = 0x07,//固件版本 string
    TLV_DEVICE_TYPE = 0x08,//设备类型 string
    TLV_MAC_ADDRESS = 0x09,//MAC地址 string

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

    TLV_CONTINUOUS_ENABLE = 0x30,//持续发送开关 uint8
    TLV_INTERVAL_MS = 0x31,//间隔时间（毫秒）
    TLV_SENSOR_ACTIVE = 0x32,//传感器活跃状态 uint8

    TLV_MESSAGE = 0x40,//消息
    TLV_IP_ADDRESS = 0x41,//IP地址
    TLV_WIFI_CONFIGURED = 0x42,//WiFi配置
    TLV_WIFI_CONNECTED = 0x43,//WiFi连接
    TLV_ECHO_CONTENT = 0x44,//回显内容
    
    // 状态和步骤字段
    TLV_STATE = 0x45,//状态 uint8
    TLV_STEP = 0x46,//步骤 uint8
    TLV_REASON = 0x47,//原因码 uint8
};

// 分层分域错误码定义 (高4位=模块，低4位=具体错误)
namespace ErrorCode {
    // 通用状态 (0x0_)
    constexpr uint8_t SUCCESS = 0x00;              // 成功
    constexpr uint8_t PROCESSING = 0x01;           // 已接收，处理中
    constexpr uint8_t PARTIAL_SUCCESS = 0x02;      // 部分成功
    constexpr uint8_t UNKNOWN = 0x0F;              // 未知结果
    
    // 协议层错误 (0x1_)
    constexpr uint8_t ERR_PROTO_CRC_FAIL = 0x10;       // CRC校验失败
    constexpr uint8_t ERR_PROTO_FRAME_INVALID = 0x11;  // 帧格式错误
    constexpr uint8_t ERR_PROTO_LEN_INVALID = 0x12;    // 长度非法
    constexpr uint8_t ERR_PROTO_CMD_UNKNOWN = 0x13;    // 未知命令
    constexpr uint8_t ERR_PROTO_PARAM_MISSING = 0x14;  // 缺少参数
    constexpr uint8_t ERR_PROTO_PARAM_INVALID = 0x15;  // 参数非法
    constexpr uint8_t ERR_PROTO_BUSY = 0x16;           // 设备忙
    constexpr uint8_t ERR_PROTO_TIMEOUT = 0x17;       // 协议处理超时
    
    // WiFi错误 (0x2_)
    constexpr uint8_t ERR_WIFI_SCAN_TIMEOUT = 0x20;      // 扫描超时
    constexpr uint8_t ERR_WIFI_SSID_NOT_FOUND = 0x21;    // 找不到SSID
    constexpr uint8_t ERR_WIFI_WRONG_PASSWORD = 0x22;    // 密码错误
    constexpr uint8_t ERR_WIFI_CONNECT_TIMEOUT = 0x23;   // 连接AP超时
    constexpr uint8_t ERR_WIFI_IP_TIMEOUT = 0x24;        // 获取IP超时
    constexpr uint8_t ERR_WIFI_SIGNAL_WEAK = 0x25;       // 信号太弱
    constexpr uint8_t ERR_WIFI_BUSY = 0x26;              // WiFi正在被其他操作占用
    constexpr uint8_t ERR_WIFI_DISCONNECTED = 0x27;      // 连接过程被断开
    
    // 雷达错误 (0x3_)
    constexpr uint8_t ERR_RADAR_NO_DATA = 0x30;          // 无数据
    constexpr uint8_t ERR_RADAR_UART_TIMEOUT = 0x31;     // UART超时
    constexpr uint8_t ERR_RADAR_FRAME_INVALID = 0x32;    // 雷达帧异常
    constexpr uint8_t ERR_RADAR_HW_FAULT = 0x33;         // 硬件故障
    constexpr uint8_t ERR_RADAR_NOT_READY = 0x34;        // 雷达未就绪
    
    // 设备/状态错误 (0x4_)
    constexpr uint8_t ERR_DEV_STATE_INVALID = 0x40;      // 当前状态不允许
    constexpr uint8_t ERR_DEV_STORAGE_FAIL = 0x41;       // 存储失败
    constexpr uint8_t ERR_DEV_QUEUE_FULL = 0x42;         // 队列已满
    constexpr uint8_t ERR_DEV_NO_MEMORY = 0x43;          // 内存不足
    constexpr uint8_t ERR_DEV_NOT_CONNECTED = 0x44;      // 设备未连接
    
    // 云端/网络错误 (0x5_)
    constexpr uint8_t ERR_CLOUD_MQTT_FAIL = 0x50;        // MQTT失败
    constexpr uint8_t ERR_CLOUD_HTTP_FAIL = 0x51;        // HTTP失败
    constexpr uint8_t ERR_CLOUD_UPLOAD_TIMEOUT = 0x52;   // 上传超时
}

// 状态枚举
namespace State {
    constexpr uint8_t IDLE = 0x00;        // 空闲
    constexpr uint8_t PROCESSING = 0x01;  // 处理中
    constexpr uint8_t SUCCESS = 0x02;     // 成功
    constexpr uint8_t FAILED = 0x03;      // 失败
}

// 步骤枚举
namespace Step {
    constexpr uint8_t NONE = 0x00;           // 无
    constexpr uint8_t RECEIVED = 0x01;       // 已接收
    constexpr uint8_t SCANNING = 0x02;       // 扫描中
    constexpr uint8_t CONNECTING_AP = 0x03;  // 连接AP
    constexpr uint8_t REQUESTING_IP = 0x04;  // 请求IP
    constexpr uint8_t COMPLETED = 0x05;      // 完成
}

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
void appendU64(std::vector<uint8_t>& out, uint64_t value);//添加64位无符号整数
void appendBytes(std::vector<uint8_t>& out, const uint8_t* data, size_t len);//添加字节数组
void appendString(std::vector<uint8_t>& out, const String& s);//添加字符串

void appendTlvU8(std::vector<uint8_t>& out, uint8_t type, uint8_t value);//添加8位无符号整数TLV
void appendTlvU16(std::vector<uint8_t>& out, uint8_t type, uint16_t value);//添加16位无符号整数TLV
void appendTlvI16(std::vector<uint8_t>& out, uint8_t type, int16_t value);//添加16位有符号整数TLV
void appendTlvU32(std::vector<uint8_t>& out, uint8_t type, uint32_t value);//添加32位无符号整数TLV
void appendTlvU64(std::vector<uint8_t>& out, uint8_t type, uint64_t value);//添加64位无符号整数TLV
void appendTlvString(std::vector<uint8_t>& out, uint8_t type, const String& value);//添加字符串TLV
void appendTlvBlock(std::vector<uint8_t>& out, uint8_t type, const std::vector<uint8_t>& value);//添加块TLV

bool readTlv(const std::vector<uint8_t>& data, size_t& offset, uint8_t& type, uint16_t& len, const uint8_t*& value);

std::vector<uint8_t> encodeFrame(const Frame& frame);

// ==================== 已废弃的JSON过渡层函数 ====================
// 这些函数是从JSON到TLV过渡期间的兼容层，现在BLE模块已完全TLV化，可以安全移除

// 入站：TLV -> 旧 JSON 命令（已废弃）
bool decodeFrameToLegacyJson(const Frame& frame, String& legacyJson); // 已废弃：BLE模块已完全TLV化

// 出站：旧 JSON 响应 -> TLV Frame（已废弃）
bool encodeLegacyJsonToFrame(const String& json, uint8_t seq, Frame& frame); // 已废弃：请直接构造TLV帧

} // namespace BleProto

#endif
