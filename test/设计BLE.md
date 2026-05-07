# BLE TLV Protocol v1

## 1. 目标

本协议用于 ESP32 设备与手机/小程序之间的 BLE 应用层通信。

设计目标：

- 替代当前基于 JSON 文本的 BLE 传输方式
- 提供明确的消息边界、长度字段和 CRC 校验
- 支持小包单帧传输
- 支持大包分片传输
- 业务载荷使用 TLV 编码
- BLE 使用二进制协议，MQTT/云端仍可继续使用 JSON

本协议仅用于 BLE 链路，不要求项目内部模块或 MQTT 同步改为二进制。

---

## 2. 分层设计

协议分为两层：

1. 传输层 Frame
2. 业务层 TLV Payload

关系如下：

BLE Write / Notify
-> Frame
-> TLV Payload
-> 业务命令对象

---

## 3. 传输帧格式

每一条 BLE 业务消息都封装为一个数据帧：

SOF(2) | VER(1) | CMD(1) | FLAGS(1) | SEQ(1) | LEN(2) | DATA(N) | CRC16(2)

字段定义：

- SOF:
  - 帧头固定为 `0xAA 0x55`
- VER:
  - 协议版本
  - 当前固定为 `0x01`
- CMD:
  - 命令字
- FLAGS:
  - 标志位
- SEQ:
  - 帧序号，范围 `0x00 ~ 0xFF`，循环使用
- LEN:
  - `DATA` 字段长度
  - 类型为 `uint16`
  - 使用大端
- DATA:
  - TLV 业务载荷，或分片头 + TLV 业务载荷
- CRC16:
  - 对 `[VER ... DATA]` 做 CRC16 校验
  - 不包含 SOF
  - 使用大端存储

---

## 4. 编码约定

统一约定如下：

- 多字节整数：大端
- 字符串：UTF-8，不带 `\\0`
- 布尔值：`uint8`
  - `0x00 = false`
  - `0x01 = true`
- 浮点数据：
  - 原则上不直接传 float
  - 优先使用定点整数
- 时间戳：
  - 使用 `uint32`
  - 单位秒
- 错误码：
  - `0 = success`
  - 非 0 表示失败

示例：

- 心率 `78.5 bpm` 编码为 `785`
- 呼吸率 `18.2 rpm` 编码为 `182`

---

## 5. FLAGS 定义

FLAGS 为 1 字节，按位定义：

- bit0: `is_fragment`
  - `0` = 非分片
  - `1` = 分片
- bit1: `need_ack`
  - `0` = 不需要 ACK
  - `1` = 需要 ACK
- bit2: `is_ack`
  - `0` = 普通业务帧
  - `1` = ACK 帧
- bit3: `is_error`
  - `0` = 正常业务帧
  - `1` = 错误响应帧
- bit4~bit7:
  - 预留

---

## 6. TLV 格式

业务载荷统一使用 TLV：

TYPE(1) | LEN(2) | VALUE(N)

字段定义：

- TYPE:
  - 1 字节字段编号
- LEN:
  - VALUE 长度
  - `uint16`
  - 大端
- VALUE:
  - 字段值

一条消息可由多个 TLV 顺序拼接组成：

TLV1 | TLV2 | TLV3 | ...

---

## 7. TLV 类型表 v1

### 7.1 公共字段

- `0x01` `device_id` `string`
- `0x02` `result_code` `uint8`
- `0x03` `error_message` `string`
- `0x04` `timestamp` `uint32`
- `0x05` `protocol_version` `uint8`

### 7.2 雷达与状态字段

- `0x10` `heart_rate_x10` `uint16`
- `0x11` `breath_rate_x10` `uint16`
- `0x12` `presence` `uint8`
- `0x13` `motion` `uint8`
- `0x14` `sleep_state` `uint8`
- `0x15` `distance_cm` `uint16`
- `0x16` `pos_x_mm` `int16`
- `0x17` `pos_y_mm` `int16`
- `0x18` `pos_z_mm` `int16`

### 7.3 WiFi 字段

- `0x20` `ssid` `string`
- `0x21` `password` `string`
- `0x22` `wifi_count` `uint16`
- `0x23` `wifi_item` `block`
- `0x24` `rssi` `int8`
- `0x25` `security` `uint8`

### 7.4 控制字段

- `0x30` `continuous_enable` `uint8`
- `0x31` `interval_ms` `uint16`

---

## 8. 嵌套块定义

### 8.1 wifi_item

`wifi_item` 为 block 类型，其 VALUE 内部继续按 TLV 编码：

TLV(ssid) | TLV(rssi) | TLV(security)

说明：

- 一个 `wifi_item` 表示一个扫描到或保存的 WiFi 网络
- 一个响应中可包含多个 `wifi_item`

---

## 9. 命令字表 v1

### 9.1 基础链路

- `0x01` `PING_REQUEST`
- `0x02` `PING_RESPONSE`

### 9.2 设备状态与雷达数据

- `0x10` `QUERY_STATUS_REQUEST`
- `0x11` `STATUS_RESPONSE`
- `0x12` `QUERY_RADAR_DATA_REQUEST`
- `0x13` `RADAR_DATA_RESPONSE`
- `0x14` `START_CONTINUOUS_SEND_REQUEST`
- `0x15` `START_CONTINUOUS_SEND_RESPONSE`
- `0x16` `STOP_CONTINUOUS_SEND_REQUEST`
- `0x17` `STOP_CONTINUOUS_SEND_RESPONSE`
- `0x18` `CONTINUOUS_DATA_PUSH`

### 9.3 WiFi 管理

- `0x20` `WIFI_SCAN_REQUEST`
- `0x21` `WIFI_SCAN_RESPONSE`
- `0x22` `WIFI_CONFIG_REQUEST`
- `0x23` `WIFI_CONFIG_RESPONSE`
- `0x24` `GET_SAVED_WIFI_REQUEST`
- `0x25` `GET_SAVED_WIFI_RESPONSE`

### 9.4 设备标识

- `0x30` `SET_DEVICE_ID_REQUEST`
- `0x31` `SET_DEVICE_ID_RESPONSE`

### 9.5 通用控制

- `0x7E` `ERROR_RESPONSE`
- `0x7F` `ACK`

---

## 10. 各命令载荷定义

### 10.1 PING_REQUEST `0x01`

请求载荷：

- 可空

### 10.2 PING_RESPONSE `0x02`

响应载荷：

- `result_code`
- `timestamp`
- 可选 `protocol_version`

---

### 10.3 QUERY_STATUS_REQUEST `0x10`

请求载荷：

- 可空

### 10.4 STATUS_RESPONSE `0x11`

响应载荷：

- `result_code`
- `device_id`
- `timestamp`
- `presence`
- `motion`
- `sleep_state`
- `distance_cm`
- 可选 `heart_rate_x10`
- 可选 `breath_rate_x10`

---

### 10.5 QUERY_RADAR_DATA_REQUEST `0x12`

请求载荷：

- 可空

### 10.6 RADAR_DATA_RESPONSE `0x13`

响应载荷：

- `result_code`
- `timestamp`
- `heart_rate_x10`
- `breath_rate_x10`
- `presence`
- `motion`
- `sleep_state`
- `distance_cm`
- `pos_x_mm`
- `pos_y_mm`
- `pos_z_mm`

---

### 10.7 START_CONTINUOUS_SEND_REQUEST `0x14`

请求载荷：

- 可选 `interval_ms`

说明：

- 若未提供 `interval_ms`，由设备使用默认周期

### 10.8 START_CONTINUOUS_SEND_RESPONSE `0x15`

响应载荷：

- `result_code`
- 可选 `error_message`

---

### 10.9 STOP_CONTINUOUS_SEND_REQUEST `0x16`

请求载荷：

- 可空

### 10.10 STOP_CONTINUOUS_SEND_RESPONSE `0x17`

响应载荷：

- `result_code`
- 可选 `error_message`

---

### 10.11 CONTINUOUS_DATA_PUSH `0x18`

推送载荷：

- `timestamp`
- `heart_rate_x10`
- `breath_rate_x10`
- `presence`
- `motion`
- `sleep_state`
- `distance_cm`

说明：

- 连续推送数据建议尽量精简
- 不要每包都带冗余字段

---

### 10.12 WIFI_SCAN_REQUEST `0x20`

请求载荷：

- 可空

### 10.13 WIFI_SCAN_RESPONSE `0x21`

响应载荷：

- `result_code`
- `wifi_count`
- 多个 `wifi_item`

---

### 10.14 WIFI_CONFIG_REQUEST `0x22`

请求载荷：

- `ssid`
- `password`

可选：

- `device_id`

### 10.15 WIFI_CONFIG_RESPONSE `0x23`

响应载荷：

- `result_code`
- 可选 `error_message`

---

### 10.16 GET_SAVED_WIFI_REQUEST `0x24`

请求载荷：

- 可空

### 10.17 GET_SAVED_WIFI_RESPONSE `0x25`

响应载荷：

- `result_code`
- `wifi_count`
- 多个 `wifi_item`

---

### 10.18 SET_DEVICE_ID_REQUEST `0x30`

请求载荷：

- `device_id`

### 10.19 SET_DEVICE_ID_RESPONSE `0x31`

响应载荷：

- `result_code`
- 可选 `device_id`
- 可选 `error_message`

---

### 10.20 ERROR_RESPONSE `0x7E`

响应载荷：

- `result_code`
- `error_message`

说明：

- 当命令不支持、参数错误、状态不允许、CRC 错误等情况时使用

---

### 10.21 ACK `0x7F`

建议仅传输层使用。

响应载荷建议：

- 可选 `result_code`

说明：

- 若业务简单，可先不启用 ACK
- 若后续需要可靠分片或 OTA，再启用 ACK/重传机制

---

## 11. 分片传输规则

当单条业务消息超过当前单次 BLE 可安全承载长度时，使用分片。

分片帧要求：

- `FLAGS.bit0 = 1`
- `DATA` 开头先放分片头，再放 TLV 业务数据片段

分片头格式：

MSG_ID(2) | FRAG_TOTAL(2) | FRAG_INDEX(2) | PAYLOAD(N)

字段定义：

- `MSG_ID`
  - 同一条完整消息的唯一标识
- `FRAG_TOTAL`
  - 总分片数
- `FRAG_INDEX`
  - 当前分片序号，从 `0` 开始
- `PAYLOAD`
  - 当前分片承载的数据内容

接收端流程：

1. 找到 SOF
2. 读取固定头
3. 根据 LEN 收满整帧
4. 做 CRC16 校验
5. 判断是否分片
6. 若为分片，按 `MSG_ID` 缓存
7. 收齐 `FRAG_TOTAL` 后按 `FRAG_INDEX` 重组
8. 对完整重组数据执行 TLV 解析
9. 交给业务层处理

超时规则建议：

- 同一个 `MSG_ID` 的分片超过超时时间未收齐则丢弃
- 超时时间建议先取 `3~5 秒`

---

## 12. 错误码建议

- `0x00` 成功
- `0x01` 未知命令
- `0x02` 参数缺失
- `0x03` 参数格式错误
- `0x04` CRC 校验失败
- `0x05` 设备忙
- `0x06` 当前状态不允许
- `0x07` WiFi 连接失败
- `0x08` 数据过长
- `0x09` 分片重组超时
- `0x0A` 内部错误

---

## 13. 示例

### 13.1 WiFi 配网请求

命令：

- `CMD = 0x22`

DATA：

- `TLV(0x20, "MyWiFi")`
- `TLV(0x21, "12345678")`

语义：

- 请求设备使用指定 SSID 和密码进行配网

### 13.2 设备状态响应

命令：

- `CMD = 0x11`

DATA：

- `TLV(result_code, 0)`
- `TLV(device_id, "RADAR_001")`
- `TLV(timestamp, 1710000000)`
- `TLV(presence, 1)`
- `TLV(motion, 2)`
- `TLV(distance_cm, 100)`
- `TLV(heart_rate_x10, 785)`
- `TLV(breath_rate_x10, 182)`

语义：

- 设备在线
- 检测到人
- 距离 100 cm
- 心率 78.5 bpm
- 呼吸率 18.2 rpm

---

## 14. 实现建议

### 14.1 BLE 链路层职责

只负责：

- 收字节流
- 找帧头
- 长度判断
- CRC 校验
- 分片重组
- 输出完整业务消息

不要在这一层直接做 WiFi、雷达或 JSON 处理。

### 14.2 TLV 编解码层职责

只负责：

- 按 CMD 解析 TLV
- 把 TLV 转成内部对象
- 把内部对象编码成 TLV

不要在这一层直接操作 BLE 特征值。

### 14.3 业务层职责

只负责：

- 设备状态查询
- WiFi 配网
- 雷达数据获取
- 持续上报控制

业务层不应依赖原始 BLE 字节流结构。

### 14.4 MQTT 层职责

MQTT 继续保留 JSON：

- 内部对象 -> JSON
- JSON -> MQTT publish

BLE 二进制协议不应影响 MQTT JSON 上报格式。

---

## 15. 兼容性建议

若需要平滑迁移：

- 保留旧 JSON BLE 协议一段时间
- 新旧协议可通过不同特征值或不同版本号区分
- 新协议稳定后再移除旧协议

若不需要兼容旧客户端，可直接切换至本协议。

---

## 16. 后续扩展

后续可扩展内容：

- OTA 控制命令
- OTA 数据分片命令
- 日志导出命令
- 可靠 ACK / 重传
- 会话握手与鉴权
- 配置项批量读写
