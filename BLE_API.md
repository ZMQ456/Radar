# ESP32 BLE API 文档

## 概述

本文档描述了 ESP32 雷达设备的 BLE（蓝牙低功耗）通信 API，用于通过蓝牙连接配置和管理设备。

## 通信协议

- **传输方式**: BLE (Bluetooth Low Energy)
- **数据格式**: JSON
- **编码**: UTF-8
- **分包大小**: 20 字节/包
- **特征值 UUID**: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- **服务 UUID**: `a8c1e5c0-3d5d-4a9d-8d5e-7c8b6a4e2f1a`

## 通用响应格式

所有响应都遵循以下 JSON 格式：

```json
{
  "type": "响应类型",
  "success": true/false,
  "message": "可选的消息描述",
  ...
}
```

## API 列表

### 1. WiFi 扫描

扫描周围可用的 WiFi 网络。

#### 请求

```json
{
  "command": "scanWiFi"
}
```

#### 响应

成功时：

```json
{
  "type": "scanWiFiResult",
  "success": true,
  "count": 3,
  "networks": [
    {
      "ssid": "WiFi名称1",
      "rssi": -45,
      "channel": 6,
      "encryption": 3
    },
    {
      "ssid": "WiFi名称2",
      "rssi": -60,
      "channel": 11,
      "encryption": 4
    }
  ]
}
```

失败时：

```json
{
  "type": "scanWiFiResult",
  "success": false,
  "message": "未扫描到任何WiFi网络",
  "networks": [],
  "count": 0
}
```

#### 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `ssid` | string | WiFi 网络名称 |
| `rssi` | number | 信号强度（dBm），值越大信号越强 |
| `channel` | number | WiFi 通道号（1-13） |
| `encryption` | number | 加密类型：<br>0 = 开放<br>1 = WEP<br>2 = WPA_PSK<br>3 = WPA2_PSK<br>4 = WPA_WPA2_PSK |

#### 示例

```javascript
// 发送扫描命令
const scanCommand = JSON.stringify({ command: "scanWiFi" });
await characteristic.writeValue(scanCommand);

// 接收响应
characteristic.addEventListener('characteristicvaluechanged', (event) => {
  const response = JSON.parse(event.target.value);
  if (response.type === "scanWiFiResult") {
    console.log(`扫描到 ${response.count} 个网络`);
    response.networks.forEach(network => {
      console.log(`SSID: ${network.ssid}, 信号: ${network.rssi} dBm`);
    });
  }
});
```

---

### 2. 获取已保存的 WiFi 网络

获取设备中已保存的 WiFi 网络列表。

#### 请求

```json
{
  "command": "getSavedNetworks"
}
```

#### 响应

成功时：

```json
{
  "type": "savedNetworks",
  "success": true,
  "count": 2,
  "networks": [
    {
      "ssid": "MyWiFi"
    },
    {
      "ssid": "OfficeWiFi"
    }
  ]
}
```

无保存网络时：

```json
{
  "type": "savedNetworks",
  "success": true,
  "count": 0,
  "networks": []
}
```

#### 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `ssid` | string | 已保存的 WiFi 网络名称（不包含密码） |

#### 示例

```javascript
// 发送获取命令
const getCommand = JSON.stringify({ command: "getSavedNetworks" });
await characteristic.writeValue(getCommand);

// 接收响应
characteristic.addEventListener('characteristicvaluechanged', (event) => {
  const response = JSON.parse(event.target.value);
  if (response.type === "savedNetworks") {
    console.log(`已保存 ${response.count} 个网络`);
    response.networks.forEach(network => {
      console.log(`SSID: ${network.ssid}`);
    });
  }
});
```

---

### 3. 配置 WiFi

设置 WiFi 网络配置。

#### 请求

```json
{
  "command": "setWiFiConfig",
  "ssid": "WiFi名称",
  "password": "WiFi密码"
}
```

#### 响应

成功时：

```json
{
  "type": "wifiConfigResult",
  "success": true,
  "message": "WiFi配置成功"
}
```

失败时：

```json
{
  "type": "wifiConfigResult",
  "success": false,
  "message": "错误描述"
}
```

#### 错误说明

| 错误信息 | 原因 | 解决方案 |
|---------|--------|---------|
| `未扫描到任何WiFi网络，请检查设备位置` | 设备扫描不到任何 WiFi 网络 | 将设备移到更靠近路由器的位置 |
| `未找到目标WiFi网络，请检查WiFi名称是否正确` | 扫描结果中不存在目标 WiFi | 检查 WiFi 名称是否拼写正确，确保设备在 WiFi 覆盖范围内 |
| `目标WiFi信号过弱，请将设备靠近路由器` | 目标 WiFi 信号强度低于阈值（-200 dBm） | 将设备移到更靠近路由器的位置 |
| `WiFi配置失败，请检查密码是否正确` | 密码错误或连接超时 | 检查 WiFi 密码是否正确，确保设备在 WiFi 覆盖范围内 |

#### 字段说明

| 字段 | 类型 | 必需 | 说明 |
|------|------|--------|------|
| `ssid` | string | ✅ | WiFi 网络名称（最多 31 字符） |
| `password` | string | ✅ | WiFi 密码（最多 63 字符） |

#### 示例

```javascript
// 发送配置命令
const configCommand = JSON.stringify({
  command: "setWiFiConfig",
  ssid: "MyWiFi",
  password: "mypassword"
});
await characteristic.writeValue(configCommand);

// 接收响应
characteristic.addEventListener('characteristicvaluechanged', (event) => {
  const response = JSON.parse(event.target.value);
  if (response.type === "wifiConfigResult") {
    if (response.success) {
      console.log("WiFi 配置成功");
    } else {
      console.log("WiFi 配置失败:", response.message);
    }
  }
});
```

---

### 4. 查询设备状态

查询设备的当前状态，包括 WiFi 连接状态、设备 ID 等。

#### 请求

```json
{
  "command": "queryStatus"
}
```

#### 响应

```json
{
  "type": "deviceStatus",
  "success": true,
  "deviceId": 1001,
  "wifiConfigured": true,
  "wifiConnected": true,
  "ipAddress": "192.168.137.241"
}
```

#### 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `deviceId` | number | 设备 ID（1000-1999） |
| `wifiConfigured` | boolean | 是否已配置 WiFi |
| `wifiConnected` | boolean | WiFi 是否已连接 |
| `ipAddress` | string | 设备 IP 地址（已连接时） |

#### 示例

```javascript
// 发送查询命令
const queryCommand = JSON.stringify({ command: "queryStatus" });
await characteristic.writeValue(queryCommand);

// 接收响应
characteristic.addEventListener('characteristicvaluechanged', (event) => {
  const response = JSON.parse(event.target.value);
  if (response.type === "deviceStatus") {
    console.log(`设备 ID: ${response.deviceId}`);
    console.log(`WiFi 已配置: ${response.wifiConfigured}`);
    console.log(`WiFi 已连接: ${response.wifiConnected}`);
    console.log(`IP 地址: ${response.ipAddress}`);
  }
});
```

---

### 5. 设置设备 ID

设置设备的唯一标识 ID。

#### 请求

```json
{
  "command": "setDeviceId",
  "newDeviceId": 1001
}
```

#### 响应

成功时：

```json
{
  "type": "setDeviceIdResult",
  "success": true,
  "message": "设备ID设置成功",
  "newDeviceId": 1001
}
```

失败时：

```json
{
  "type": "setDeviceIdResult",
  "success": false,
  "message": "设备ID超出范围，有效范围: 1000-1999"
}
```

#### 字段说明

| 字段 | 类型 | 必需 | 说明 |
|------|------|--------|------|
| `newDeviceId` | number | ✅ | 新设备 ID（1000-1999） |

---

### 6. 启动持续发送

启动雷达数据的持续发送模式。

#### 请求

```json
{
  "command": "startContinuousSend",
  "interval": 200
}
```

#### 响应

```json
{
  "type": "startContinuousSendResult",
  "success": true,
  "message": "已启动持续发送模式",
  "interval": 200
}
```

#### 字段说明

| 字段 | 类型 | 必需 | 说明 |
|------|------|--------|------|
| `interval` | number | ❌ | 发送间隔（毫秒），默认 200ms，范围 100-10000 |

---

### 7. 停止持续发送

停止雷达数据的持续发送模式。

#### 请求

```json
{
  "command": "stopContinuousSend"
}
```

#### 响应

```json
{
  "type": "stopContinuousSendResult",
  "success": true,
  "message": "已停止持续发送模式"
}
```

---

### 8. 查询雷达数据

查询当前的雷达传感器数据。

#### 请求

```json
{
  "command": "queryRadarData"
}
```

#### 响应

```json
{
  "type": "radarData",
  "success": true,
  "deviceId": 1001,
  "timestamp": 1234567890,
  "presence": 1,
  "heartRate": 75.5,
  "breathRate": 16.2,
  "motion": 0,
  "heartbeatWaveform": 120,
  "breathingWaveform": 85,
  "sleepState": 0
}
```

#### 字段说明

| 字段 | 类型 | 说明 |
|------|------|------|
| `timestamp` | number | 时间戳（毫秒） |
| `presence` | number | 是否检测到人体（0=无，1=有） |
| `heartRate` | number | 心率（次/分钟） |
| `breathRate` | number | 呼吸率（次/分钟） |
| `motion` | number | 运动状态（0=静止，1=轻微，2=明显） |
| `heartbeatWaveform` | number | 心跳波形值 |
| `breathingWaveform` | number | 呼吸波形值 |
| `sleepState` | number | 睡眠状态（0=清醒，1=浅睡，2=深睡，3=REM） |

---

### 9. 回显测试

用于测试 BLE 连接是否正常。

#### 请求

```json
{
  "command": "echo",
  "content": "测试内容"
}
```

#### 响应

```json
{
  "type": "echoResponse",
  "originalContent": "测试内容",
  "receivedSuccessfully": true
}
```

---

## 错误响应

当请求失败时，设备会返回错误响应：

```json
{
  "type": "error",
  "message": "错误描述",
  "receivedData": "原始请求数据"
}
```

## 连接流程

1. 扫描并连接 BLE 设备
2. 开启 notify 订阅
3. 发送 `queryStatus` 命令获取设备状态
4. 根据需要配置 WiFi 或查询雷达数据

## 注意事项

1. **分包传输**: 所有 JSON 数据都会被分包发送（每包 20 字节），客户端需要正确拼接
2. **UTF-8 编码**: 确保使用 UTF-8 编码处理中文字符
3. **超时处理**: 建议为每个命令设置超时时间（推荐 5-10 秒）
4. **设备 ID 范围**: 有效范围为 1000-1999
5. **WiFi 限制**: 仅支持 2.4GHz WiFi 网络
6. **最大保存网络数**: 最多保存 10 个 WiFi 配置

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0 | 2025-02-03 | 初始版本，包含所有基础 API |
