# BLE TLV Protocol

本文档描述固件与小程序之间的 BLE GATT 通信协议。协议目标是避免 BLE 长包兼容问题，统一使用 `Notify + TLV 帧 + 20 字节分包重组`，并明确请求响应与主动推送的边界。

## 1. 设计原则

1. 客户端必须先按特征 UUID 分流，再按 `cmd` 解析。
2. `b1/b2` 是请求响应通道，所有命令响应和错误响应都走 `b2`。
3. `a1/a2/b3` 是主动推送通道，客户端不能把这些帧当成命令响应。
4. 所有 Notify 数据都可能超过 20 字节，客户端必须按特征分别维护重组缓冲区。
5. 同一个 TLV 类型在所有通道中的数据类型必须保持一致。
6. `READ` 不作为主协议能力使用，客户端不要调用 `readBLECharacteristicValue` 读取业务数据。

## 2. GATT 服务与特征

### 2.1 Radar Data Service

| 名称 | UUID | 属性 | 方向 | 职责 |
| --- | --- | --- | --- | --- |
| `RADAR_DATA_SERVICE_UUID` | `a8c1e5c0-3d5d-4a9d-8d5e-7c8b6a4e2f1a` | Service | - | 雷达数据服务 |
| `RADAR_STREAM_CHAR_UUID` (`a1`) | `beb5483e-36e1-4688-b7f5-ea07361b26a1` | `NOTIFY` | 设备 -> 客户端 | 连续雷达数据流 |
| `RADAR_STATUS_CHAR_UUID` (`a2`) | `beb5483e-36e1-4688-b7f5-ea07361b26a2` | `NOTIFY` | 设备 -> 客户端 | 雷达状态主动推送 |

### 2.2 Device Config Service

| 名称 | UUID | 属性 | 方向 | 职责 |
| --- | --- | --- | --- | --- |
| `DEVICE_CONFIG_SERVICE_UUID` | `a8c1e5c0-3d5d-4a9d-8d5e-7c8b6a4e2f1b` | Service | - | 设备配置服务 |
| `DEVICE_COMMAND_CHAR_UUID` (`b1`) | `beb5483e-36e1-4688-b7f5-ea07361b26b1` | `WRITE` | 客户端 -> 设备 | 命令写入 |
| `DEVICE_RESULT_CHAR_UUID` (`b2`) | `beb5483e-36e1-4688-b7f5-ea07361b26b2` | `NOTIFY` | 设备 -> 客户端 | 命令响应与错误响应 |
| `DEVICE_INFO_CHAR_UUID` (`b3`) | `beb5483e-36e1-4688-b7f5-ea07361b26b3` | `NOTIFY` | 设备 -> 客户端 | 设备信息低频推送 |

## 3. 通信模型

### 3.1 请求响应模型

客户端写 `b1`，设备从 `b2` 返回响应。

适用场景：

- 查询设备概览。
- 查询雷达快照。
- 启动或停止连续推送。
- 设置设备 ID。
- WiFi 扫描、配置、查询。
- 错误返回。

请求响应规则：

- 请求帧由客户端生成 `seq`。
- 响应帧必须原样带回请求 `seq`。
- `b2` 是唯一需要按 `seq` 匹配请求的通道。
- 失败应答分两类：
  - 协议层失败：使用 `CMD_ERROR_RESP (0x7E)`。
  - 业务层失败：优先使用“原命令对应的 `RESP` + `FLAG_IS_ERROR`”。
- 无论哪种失败应答，都必须带回请求 `seq`。

### 3.2 主动推送模型

设备通过 `a1/a2/b3` 主动 Notify。

适用场景：

- `a1` 连续雷达流。
- `a2` 雷达状态定时推送。
- `b3` 设备信息连接后一次或信息变化时推送。

主动推送规则：

- 推送帧不参与请求响应匹配。
- 推送帧可使用 `seq = 0`。
- 客户端必须先按特征 UUID 分流。
- 连接后首个推送可能在客户端完成订阅前丢失，关键数据必须能通过 `b1/b2` 重新查询。

## 4. 二进制帧格式

所有业务数据统一封装为 TLV 帧：

```text
SOF1 SOF2 VERSION CMD FLAGS SEQ LEN_H LEN_L PAYLOAD CRC_H CRC_L
```

| 字段 | 长度 | 说明 |
| --- | --- | --- |
| `SOF1` | 1 | 固定 `0xAA` |
| `SOF2` | 1 | 固定 `0x55` |
| `VERSION` | 1 | 当前 `0x01` |
| `CMD` | 1 | 命令码 |
| `FLAGS` | 1 | 标志位 |
| `SEQ` | 1 | 序列号 |
| `LEN` | 2 | `PAYLOAD` 长度，大端 |
| `PAYLOAD` | N | TLV 数据区 |
| `CRC` | 2 | CRC16-CCITT，大端 |

CRC 校验范围：从 `VERSION` 到 `PAYLOAD` 末尾，不包含 `SOF1/SOF2`，不包含 CRC 字段本身。

### 4.1 Flags

| 标志 | 值 | 说明 |
| --- | --- | --- |
| `FLAG_FRAGMENT` | `0x01` | 预留分片标记。当前 Notify 分包通过连续 20 字节片段和帧长度重组，不依赖该位 |
| `FLAG_NEED_ACK` | `0x02` | 发送方要求 ACK，预留 |
| `FLAG_IS_ACK` | `0x04` | ACK 帧，预留 |
| `FLAG_IS_ERROR` | `0x08` | 错误帧 |

### 4.2 Notify 分包

固件通过 `sendFrameToBLE()` 发送 Notify 帧，当前固定按 20 字节分包。

客户端不应假设一次 Notify 就是一帧完整数据。客户端必须对每个 Notify 特征分别维护接收缓冲区：

- `a1` 一个 buffer。
- `a2` 一个 buffer。
- `b2` 一个 buffer。
- `b3` 一个 buffer。

## 5. 命令码

### 5.1 系统命令

| 命令 | 值 | 通道 | 说明 |
| --- | --- | --- | --- |
| `CMD_PING` | `0x01` | `b1/b2` | Ping 请求/响应（一问一答共用命令码） |

### 5.2 雷达与状态命令

| 命令 | 值 | 通道 | 说明 |
| --- | --- | --- | --- |
| `CMD_QUERY_STATUS` | `0x10` | `b1/b2` | 查询设备概览 请求/响应（一问一答共用命令码） |
| `CMD_QUERY_RADAR` | `0x12` | `b1/b2` | 查询雷达快照 请求/响应（一问一答共用命令码） |
| `CMD_START_CONTINUOUS` | `0x14` | `b1/b2` | 启动连续推送 请求/响应（一问一答共用命令码） |
| `CMD_STOP_CONTINUOUS` | `0x16` | `b1/b2` | 停止连续推送 请求/响应（一问一答共用命令码） |
| `CMD_CONTINUOUS_PUSH` | `0x18` | `a1` | 连续雷达数据推送（主动推送） |
| `CMD_DEVICE_INFO_PUSH` | `0x19` | `b3` | 设备信息主动推送（主动推送） |
| `CMD_RADAR_STATUS_PUSH` | `0x1A` | `a2` | 雷达状态主动推送（主动推送） |

### 5.3 WiFi 命令

| 命令 | 值 | 通道 | 说明 |
| --- | --- | --- | --- |
| `CMD_WIFI_SCAN` | `0x20` | `b1/b2` | WiFi 扫描 请求/响应（一问一答共用命令码） |
| `CMD_WIFI_CONFIG` | `0x22` | `b1/b2` | WiFi 配置 请求/响应（一问一答共用命令码） |
| `CMD_GET_SAVED_WIFI` | `0x24` | `b1/b2` | 查询已保存 WiFi 请求/响应（一问一答共用命令码） |

### 5.4 设备命令

| 命令 | 值 | 通道 | 说明 |
| --- | --- | --- | --- |
| `CMD_SET_DEVICE_ID` | `0x30` | `b1/b2` | 设置设备 ID 请求/响应（一问一答共用命令码） |

### 5.5 通用命令

| 命令 | 值 | 通道 | 说明 |
| --- | --- | --- | --- |
| `CMD_ERROR_RESP` | `0x7E` | `b2` | 错误响应 |
| `CMD_ACK` | `0x7F` | `b2` | ACK，预留 |

## 6. TLV 类型

TLV 编码格式：

```text
TYPE(1) LEN_H(1) LEN_L(1) VALUE(N)
```

`LEN` 为大端。

### 6.1 设备信息 TLV

| TLV | 值 | 类型 | 说明 |
| --- | --- | --- | --- |
| `TLV_DEVICE_ID` | `0x01` | `uint16` | 设备 ID |
| `TLV_RESULT_CODE` | `0x02` | `uint8` | 结果码 |
| `TLV_ERROR_MESSAGE` | `0x03` | `string` | 错误信息 |
| `TLV_TIMESTAMP` | `0x04` | `uint32` | 时间戳，通常为 `millis()` |
| `TLV_PROTOCOL_VERSION` | `0x05` | `string` | 协议版本 |
| `TLV_DEVICE_SN` | `0x06` | `uint64` | 设备序列号 |
| `TLV_FIRMWARE_VERSION` | `0x07` | `string` | 固件版本 |
| `TLV_DEVICE_TYPE` | `0x08` | `string` | 设备类型 |
| `TLV_MAC_ADDRESS` | `0x09` | `string` | MAC 地址 |

约束：

- `TLV_DEVICE_SN` 必须始终是 `uint64`。
- 没有设备 SN 时，不发送 `TLV_DEVICE_SN`。
- 不要把 MAC 字符串写入 `TLV_DEVICE_SN`。
- 如需统一字符串身份标识，应新增独立 TLV，例如 `TLV_DEVICE_UID`。

### 6.2 雷达数据 TLV

| TLV | 值 | 类型 | 说明 |
| --- | --- | --- | --- |
| `TLV_HEART_RATE_X10` | `0x10` | `uint16` | 心率乘 10 |
| `TLV_BREATH_RATE_X10` | `0x11` | `uint16` | 呼吸率乘 10 |
| `TLV_PRESENCE` | `0x12` | `uint8` | 是否存在人体 |
| `TLV_MOTION` | `0x13` | `uint8` | 运动状态 |
| `TLV_SLEEP_STATE` | `0x14` | `uint8` | 睡眠状态 |
| `TLV_DISTANCE_CM` | `0x15` | `uint16` | 距离，单位 cm |
| `TLV_POS_X_MM` | `0x16` | `int16` | X 坐标，单位 mm |
| `TLV_POS_Y_MM` | `0x17` | `int16` | Y 坐标，单位 mm |
| `TLV_POS_Z_MM` | `0x18` | `int16` | Z 坐标，单位 mm |
| `TLV_BODY_MOVEMENT` | `0x19` | `uint8` | 体动状态 |

### 6.3 WiFi TLV

| TLV | 值 | 类型 | 说明 |
| --- | --- | --- | --- |
| `TLV_SSID` | `0x20` | `string` | WiFi SSID |
| `TLV_PASSWORD` | `0x21` | `string` | WiFi 密码 |
| `TLV_WIFI_COUNT` | `0x22` | `uint16` | WiFi 数量 |
| `TLV_WIFI_ITEM` | `0x23` | `block` | WiFi 条目 |
| `TLV_RSSI` | `0x24` | `int8` 或 `int16` | RSSI，建议固化为一种类型 |
| `TLV_SECURITY` | `0x25` | `uint8` | 加密类型 |

### 6.4 控制与通用 TLV

| TLV | 值 | 类型 | 说明 |
| --- | --- | --- | --- |
| `TLV_INTERVAL_MS` | `0x31` | `uint16` | 推送间隔，单位 ms |
| `TLV_MESSAGE` | `0x40` | `string` | 普通消息 |
| `TLV_IP_ADDRESS` | `0x41` | `string` | IP 地址 |
| `TLV_WIFI_CONFIGURED` | `0x42` | `uint8` | 是否保存过 WiFi |
| `TLV_WIFI_CONNECTED` | `0x43` | `uint8` | WiFi 是否连接 |
| `TLV_ECHO_CONTENT` | `0x44` | `string` | Echo 内容 |
| `TLV_STATE` | `0x45` | `uint8` | 状态 |
| `TLV_STEP` | `0x46` | `uint8` | 流程步骤 |
| `TLV_REASON` | `0x47` | `uint8` | 原因码 |

### 6.5 波形 TLV

| TLV | 值 | 类型 | 说明 |
| --- | --- | --- | --- |
| `TLV_HEART_WAVEFORM` | `0x60` | `uint8` | 心跳波形，原始 `int8 + 128` |
| `TLV_BREATH_WAVEFORM` | `0x61` | `uint8` | 呼吸波形，原始 `int8 + 128` |

## 7. 主要命令载荷

### 7.1 设备概览查询

请求：

| 字段 | 说明 |
| --- | --- |
| `cmd` | `CMD_QUERY_STATUS (0x10)` |
| `channel` | 写入 `b1` |
| `payload` | 可为空 |

响应：

| 字段 | 说明 |
| --- | --- |
| `cmd` | `CMD_QUERY_STATUS (0x10)`（与请求共用命令码） |
| `channel` | 从 `b2` Notify |
| `seq` | 请求 `seq` |

建议返回 TLV：

- `TLV_RESULT_CODE`
- `TLV_DEVICE_ID`
- `TLV_PROTOCOL_VERSION`
- `TLV_FIRMWARE_VERSION`
- `TLV_DEVICE_TYPE`
- `TLV_MAC_ADDRESS`
- `TLV_DEVICE_SN`，仅设备 SN 存在时发送
- `TLV_WIFI_CONFIGURED`
- `TLV_WIFI_CONNECTED`
- `TLV_IP_ADDRESS`，仅 WiFi 已连接时发送

### 7.2 雷达快照查询

请求：

| 字段 | 说明 |
| --- | --- |
| `cmd` | `CMD_QUERY_RADAR (0x12)` |
| `channel` | 写入 `b1` |
| `payload` | 可为空 |

响应：

| 字段 | 说明 |
| --- | --- |
| `cmd` | `CMD_QUERY_RADAR (0x12)`（与请求共用命令码） |
| `channel` | 从 `b2` Notify |
| `seq` | 请求 `seq` |

建议返回 TLV：

- `TLV_RESULT_CODE`
- `TLV_DEVICE_ID`
- `TLV_TIMESTAMP`
- `TLV_PRESENCE`
- `TLV_HEART_RATE_X10`
- `TLV_BREATH_RATE_X10`
- `TLV_MOTION`
- `TLV_DISTANCE_CM`
- `TLV_SLEEP_STATE`
- `TLV_POS_X_MM`
- `TLV_POS_Y_MM`
- `TLV_POS_Z_MM`
- `TLV_BODY_MOVEMENT`

### 7.3 设备信息主动推送

| 字段 | 说明 |
| --- | --- |
| `cmd` | `CMD_DEVICE_INFO_PUSH (0x19)` |
| `channel` | `b3` |
| `seq` | `0` |
| `trigger` | 连接后一次，或设备信息变化时 |

建议返回 TLV：

- `TLV_RESULT_CODE`
- `TLV_DEVICE_ID`
- `TLV_PROTOCOL_VERSION`
- `TLV_FIRMWARE_VERSION`
- `TLV_DEVICE_TYPE`
- `TLV_MAC_ADDRESS`
- `TLV_DEVICE_SN`，仅设备 SN 存在时发送

说明：`b3` 不应周期推送。客户端若未收到 `b3`，应通过 `CMD_QUERY_STATUS (0x10)` 查询。

### 7.4 雷达状态主动推送

| 字段 | 说明 |
| --- | --- |
| `cmd` | `CMD_RADAR_STATUS_PUSH (0x1A)` |
| `channel` | `a2` |
| `seq` | `0` |
| `trigger` | 当前固件每 1 秒推送一次 |

建议返回 TLV：

- `TLV_DISTANCE_CM`
- `TLV_POS_X_MM`
- `TLV_POS_Y_MM`
- `TLV_POS_Z_MM`
- `TLV_BODY_MOVEMENT`

客户端如需完整雷达快照，应通过 `0x12 -> 0x13` 查询。

## 8. 错误码

失败应答规范：

### 8.1 即时命令失败

适用命令：

- `CMD_QUERY_STATUS`
- `CMD_QUERY_RADAR`
- `CMD_START_CONTINUOUS`
- `CMD_STOP_CONTINUOUS`
- `CMD_SET_DEVICE_ID`
- `CMD_PING`

规则：

- 使用该命令自己的命令码（一问一答共用），不使用 `CMD_ERROR_RESP`。
- `flags` 必须包含 `FLAG_IS_ERROR`。
- 必须包含 `TLV_RESULT_CODE`。
- 可选包含 `TLV_ERROR_MESSAGE`。
- 不强制包含 `TLV_STATE/TLV_STEP/TLV_REASON`。

推荐格式：

| 字段 | 值 |
| --- | --- |
| `cmd` | 原命令码，例如 `CMD_QUERY_STATUS (0x10)`、`CMD_QUERY_RADAR (0x12)` |
| `flags` | 包含 `FLAG_IS_ERROR (0x08)` |
| `channel` | `b2` |
| `seq` | 对应请求 `seq` |

推荐 TLV：

- `TLV_RESULT_CODE`
- `TLV_ERROR_MESSAGE`

说明：

- 对“一问一答”的即时响应，`TLV_RESULT_CODE` 是唯一事实来源。
- `FLAG_IS_ERROR` 负责告诉客户端“这是失败帧”。
- `TLV_STATE = FAILED` 在即时命令里通常没有信息增量，不建议强制保留。

### 8.2 异步多阶段流程失败

适用命令：

- `CMD_WIFI_CONFIG`
- `CMD_WIFI_SCAN`
- 未来其他需要 `PROCESSING -> SUCCESS/FAILED` 的命令

规则：

- 仍使用该命令自己的命令码，例如 `CMD_WIFI_CONFIG (0x22)`、`CMD_WIFI_SCAN (0x20)`。
- 处理中 ACK 不带 `FLAG_IS_ERROR`，但应返回 `TLV_RESULT_CODE = PROCESSING`。
- 失败完成态带 `FLAG_IS_ERROR`。
- 必须包含 `TLV_RESULT_CODE`。
- 推荐包含 `TLV_STATE/TLV_STEP`。
- 需要诊断信息时可加 `TLV_REASON`、`TLV_ERROR_MESSAGE`。

处理中 ACK 推荐格式：

| 字段 | 值 |
| --- | --- |
| `cmd` | 原命令码 |
| `flags` | `0` |
| `channel` | `b2` |
| `seq` | 对应请求 `seq` |

推荐 TLV：

- `TLV_RESULT_CODE = PROCESSING`
- `TLV_STATE = PROCESSING`
- `TLV_STEP = 当前步骤`

失败完成态推荐格式：

| 字段 | 值 |
| --- | --- |
| `cmd` | 原命令对应的 `RESP` |
| `flags` | 包含 `FLAG_IS_ERROR (0x08)` |
| `channel` | `b2` |
| `seq` | 对应请求 `seq` |

推荐 TLV：

- `TLV_RESULT_CODE`
- `TLV_STATE = FAILED`
- `TLV_STEP`
- `TLV_REASON`
- `TLV_ERROR_MESSAGE`

### 8.3 协议层失败

适用场景：

- 未知命令
- 帧格式错误
- 长度非法
- CRC 错误
- 命令帧超过接收缓冲区上限
- 无法归属到某个具体业务响应命令的入口级失败

协议层失败使用：

| 字段 | 值 |
| --- | --- |
| `cmd` | `CMD_ERROR_RESP (0x7E)` |
| `flags` | 包含 `FLAG_IS_ERROR (0x08)` |
| `channel` | `b2` |
| `seq` | 对应请求 `seq` |

协议层失败推荐 TLV：

- `TLV_RESULT_CODE`
- `TLV_ERROR_MESSAGE`

说明：

- 协议层失败的重点是告诉客户端“这条请求压根没被正常受理”。
- 这类错误通常不需要 `TLV_STATE/TLV_STEP`。
- `CMD_ERROR_RESP` 不应用来承载正常业务命令的失败结果，否则客户端很难按命令分类处理。

### 8.4 协议错误

| 错误码 | 值 | 建议客户端处理 |
| --- | --- | --- |
| `SUCCESS` | `0x00` | 成功 |
| `PROCESSING` | `0x01` | 等待后续状态 |
| `PARTIAL_SUCCESS` | `0x02` | 展示部分成功原因 |
| `UNKNOWN` | `0x0F` | 记录日志 |
| `ERR_PROTO_CRC_FAIL` | `0x10` | 丢弃，必要时重发 |
| `ERR_PROTO_FRAME_INVALID` | `0x11` | 检查帧格式 |
| `ERR_PROTO_LEN_INVALID` | `0x12` | 检查长度字段 |
| `ERR_PROTO_CMD_UNKNOWN` | `0x13` | 检查协议版本和命令码 |
| `ERR_PROTO_PARAM_MISSING` | `0x14` | 补齐参数 |
| `ERR_PROTO_PARAM_INVALID` | `0x15` | 修正参数 |
| `ERR_PROTO_BUSY` | `0x16` | 延迟重试 |
| `ERR_PROTO_TIMEOUT` | `0x17` | 可重试 |
| `ERR_PROTO_FRAME_TOO_LARGE` | `0x18` | 缩小请求帧，不要原样重试 |

### 8.5 WiFi 错误

| 错误码 | 值 | 建议客户端处理 |
| --- | --- | --- |
| `ERR_WIFI_SCAN_TIMEOUT` | `0x20` | 提示扫描超时，可重试 |
| `ERR_WIFI_SSID_NOT_FOUND` | `0x21` | 提示未找到网络 |
| `ERR_WIFI_WRONG_PASSWORD` | `0x22` | 提示密码错误 |
| `ERR_WIFI_CONNECT_TIMEOUT` | `0x23` | 提示连接超时 |
| `ERR_WIFI_IP_TIMEOUT` | `0x24` | 提示获取 IP 超时 |
| `ERR_WIFI_SIGNAL_WEAK` | `0x25` | 提示信号弱 |
| `ERR_WIFI_BUSY` | `0x26` | 稍后重试 |
| `ERR_WIFI_DISCONNECTED` | `0x27` | 提示连接中断 |

### 8.6 雷达错误

| 错误码 | 值 | 建议客户端处理 |
| --- | --- | --- |
| `ERR_RADAR_NO_DATA` | `0x30` | 展示无数据 |
| `ERR_RADAR_UART_TIMEOUT` | `0x31` | 设备通信异常 |
| `ERR_RADAR_FRAME_INVALID` | `0x32` | 雷达帧异常 |
| `ERR_RADAR_HW_FAULT` | `0x33` | 硬件故障 |
| `ERR_RADAR_NOT_READY` | `0x34` | 稍后重试 |

### 8.7 设备错误

| 错误码 | 值 | 建议客户端处理 |
| --- | --- | --- |
| `ERR_DEV_STATE_INVALID` | `0x40` | 当前状态不允许 |
| `ERR_DEV_STORAGE_FAIL` | `0x41` | 存储失败 |
| `ERR_DEV_QUEUE_FULL` | `0x42` | 延迟重试 |
| `ERR_DEV_NO_MEMORY` | `0x43` | 设备资源不足 |
| `ERR_DEV_NOT_CONNECTED` | `0x44` | 检查连接状态 |

### 8.8 云端错误

| 错误码 | 值 | 建议客户端处理 |
| --- | --- | --- |
| `ERR_CLOUD_MQTT_FAIL` | `0x50` | 展示云端通信异常 |
| `ERR_CLOUD_HTTP_FAIL` | `0x51` | 展示 HTTP 异常 |
| `ERR_CLOUD_UPLOAD_TIMEOUT` | `0x52` | 展示上传超时 |

### 8.9 客户端判定顺序

客户端收到 `b2` 响应后，建议按下面顺序处理：

1. 先看 `cmd`，确定这是哪类响应：
   - 原命令对应的 `RESP`
   - `CMD_ERROR_RESP`
2. 再看 `flags` 是否包含 `FLAG_IS_ERROR`。
3. 必读 `TLV_RESULT_CODE`，这是唯一结果事实来源。
4. 只有异步多阶段流程才进一步解析 `TLV_STATE/TLV_STEP/TLV_REASON`。
5. 若存在 `TLV_ERROR_MESSAGE`，仅用于展示和诊断，不作为逻辑分支依据。

## 9. 客户端接入流程

### 9.1 连接流程

```text
客户端扫描设备
客户端连接设备
客户端发现服务和特征
客户端订阅 a1/a2/b2/b3
客户端初始化每个 Notify 特征的独立重组缓冲区
客户端发送 0x10 查询设备概览
客户端按需发送 0x12 查询雷达快照
```

说明：即使设备连接后会主动推送 `b3/a2`，客户端也不应依赖首包必达。

### 9.2 请求响应时序

```text
Client                     Device
  |                          |
  | write b1: CMD_REQ(seq=N) |
  |------------------------->|
  |                          |
  | notify b2: CMD_RESP(seq=N), chunk 1
  |<-------------------------|
  | notify b2: CMD_RESP(seq=N), chunk 2
  |<-------------------------|
  |                          |
  | reassemble + CRC check   |
  | match by cmd + seq       |
```

### 9.3 主动推送时序

```text
Client                     Device
  |                          |
  | subscribe a2             |
  |------------------------->|
  |                          |
  | notify a2: CMD_RADAR_STATUS_PUSH
  |<-------------------------|
  | notify a2: CMD_RADAR_STATUS_PUSH
  |<-------------------------|
```

### 9.4 设备信息兜底时序

```text
Client                     Device
  |                          |
  | subscribe b3             |
  |------------------------->|
  |                          |
  | maybe notify b3: CMD_DEVICE_INFO_PUSH
  |<-------------------------|
  |                          |
  | if b3 missing or invalid |
  | write b1: CMD_QUERY_STATUS(seq=N)
  |------------------------->|
  | notify b2: CMD_QUERY_STATUS(seq=N)
  |<-------------------------|
```

## 10. 客户端伪代码

### 10.1 Notify 重组器

```javascript
const buffers = {
  a1: new Uint8Array(0),
  a2: new Uint8Array(0),
  b2: new Uint8Array(0),
  b3: new Uint8Array(0),
}

function onNotify(characteristicId, chunk) {
  const channel = mapCharacteristicToChannel(characteristicId)
  buffers[channel] = concatBytes(buffers[channel], chunk)

  while (true) {
    const frame = tryExtractFrame(buffers[channel])
    if (!frame) break

    buffers[channel] = frame.remaining

    if (!verifyCrc(frame.bytes)) {
      continue
    }

    dispatchByChannel(channel, frame.decoded)
  }
}
```

### 10.2 按通道分发

```javascript
function dispatchByChannel(channel, frame) {
  switch (channel) {
    case 'a1':
      handleContinuousPush(frame)
      break
    case 'a2':
      handleRadarStatusPush(frame)
      break
    case 'b3':
      handleDeviceInfoPush(frame)
      break
    case 'b2':
      handleCommandResponse(frame)
      break
  }
}
```

### 10.3 请求响应匹配

```javascript
const pending = new Map()

function sendCommand(cmd, payload) {
  const seq = nextSeq()
  const frame = encodeFrame({ cmd, seq, payload })

  pending.set(seq, { cmd, createdAt: Date.now() })
  return writeB1(frame)
}

function handleCommandResponse(frame) {
  const req = pending.get(frame.seq)
  if (!req) {
    return
  }

  pending.delete(frame.seq)

  if (frame.flags & FLAG_IS_ERROR || frame.cmd === CMD_ERROR_RESP) {
    handleError(frame)
    return
  }

  handleSuccess(req.cmd, frame)
}
```

## 11. 固件实现约束

1. 所有业务 Notify 必须调用 `sendFrameToBLE()`。
2. 不允许直接 `setValue(fullFrame); notify();` 发送完整业务帧。
3. `DEVICE_INFO_CHAR_UUID (b3)` 不支持 `READ`，只支持 `NOTIFY`。
4. `RADAR_STATUS_CHAR_UUID (a2)` 不支持 `READ`，只支持 `NOTIFY`。
5. `CMD_QUERY_STATUS (0x10)` 用于 b1 请求和 b2 响应（一问一答共用命令码）。
6. `CMD_QUERY_RADAR (0x12)` 用于 b1 请求和 b2 响应（一问一答共用命令码）。
7. `CMD_DEVICE_INFO_PUSH (0x19)` 只用于 `b3`。
8. `CMD_RADAR_STATUS_PUSH (0x1A)` 只用于 `a2`。
9. `TLV_DEVICE_SN` 必须固定为 `uint64`。
10. `b1` 写入超过固件缓冲区上限时，必须返回 `ERR_PROTO_FRAME_TOO_LARGE`，不能静默截断。

## 12. 当前建议收口项

### 12.1 设备 SN 语义

当前协议应固定：

- `TLV_DEVICE_SN`：真实设备 SN，`uint64`。
- 无 SN 时不发送该字段。
- 如需字符串唯一标识，新增 `TLV_DEVICE_UID`，不要复用 `TLV_DEVICE_SN`。

### 12.2 设备概览命名

`CMD_QUERY_STATUS (0x10)` 实际语义建议定义为"设备概览查询/响应"，一问一答共用命令码。

### 12.3 b3 使用策略

`b3` 不做周期推送。推荐策略：

- 连接后可推一次。
- 设备静态信息变化时推一次。
- 客户端关键流程以 `CMD_QUERY_STATUS (0x10)` 为可靠兜底。

### 12.4 客户端实现要求

客户端最重要的实现点：

- 先按特征 UUID 分流。
- 每个 Notify 特征一个重组 buffer。
- 只在 `b2` 上按 `seq` 匹配请求响应。
- 对 `a1/a2/b3` 不做请求响应匹配。
- 任何超过 20 字节的 Notify 都必须经过重组后再解析 TLV。