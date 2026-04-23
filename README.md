# Rader_Success_5 - ESP32 雷达数据采集与传输系统

## 项目简介

本项目是一个基于 ESP32 的智能雷达数据采集与传输系统，通过 R60ABD1 雷达传感器采集人体生命体征数据，并通过蓝牙（BLE）与小程序交互，同时支持 WiFi 连接将数据上传到云服务器数据库。

## 主要功能

### 1. 雷达数据采集
- **人体存在检测**：实时检测区域内是否有人
- **心率监测**：监测人体心率（BPM）
- **呼吸率监测**：监测人体呼吸频率
- **睡眠监测**：监测睡眠状态（清醒/浅睡/深睡/REM）
- **运动检测**：检测人体运动状态
- **距离测量**：测量人体与设备的距离
- **睡眠质量评分**：提供睡眠质量评估

### 2. 蓝牙通信（BLE）
- 设备配置与管理
- WiFi 网络扫描与配置
- 设备状态查询
- 雷达数据实时查询
- 持续数据发送模式
- 设备 ID 设置

### 3. WiFi 连接
- 自动 WiFi 连接与重连
- 支持多 WiFi 网络配置（最多 10 个）
- 网络信号强度检测
- 网络状态监控

### 4. 数据上传
- 将雷达数据上传到 InfluxDB 云数据库
- 支持每日数据汇总
- 支持睡眠数据专项上传

## 硬件配置

### 主控芯片
- **ESP32**：双核微控制器，支持 WiFi 和 BLE

### 雷达传感器
- **型号**：R60ABD1
- **通信接口**：UART
- **检测范围**：人体存在、心率、呼吸率、睡眠状态等

### 引脚定义
| 引脚 | 功能 | 说明 |
|------|------|------|
| GPIO 0 | Boot 按钮 | 配置清除按钮 |
| GPIO 48 | 网络状态 LED | 指示网络连接状态 |
| GPIO 4 | 配置清除 LED | 指示配置清除状态 |
| GPIO 8 | 自定义 GPIO | 用户自定义功能 |
| GPIO 9 | 自定义 GPIO | 用户自定义功能 |

### LED 状态指示
| 状态 | LED 行为 | 说明 |
|------|----------|------|
| 初始化/未连接 | 慢闪（1秒间隔） | 正在初始化或未连接网络 |
| 连接中 | 快闪（200ms间隔） | 正在连接 WiFi |
| 已连接 | 呼吸灯 | WiFi 已连接 |
| 断开连接 | 慢闪（1秒间隔） | WiFi 断开连接 |

## 软件架构

### 核心模块

#### 1. RadarManager（雷达管理器）
- 雷达数据采集与解析
- UART 通信管理
- 数据队列管理
- BLE 数据发送

#### 2. WiFiManager（WiFi 管理器）
- WiFi 网络扫描
- WiFi 连接管理
- 配置存储与加载
- 自动重连机制

#### 3. 主程序（main.cpp）
- 任务调度与管理
- LED 状态控制
- 按钮监控
- 系统初始化

### FreeRTOS 任务
| 任务名称 | 功能 | 优先级 |
|----------|------|--------|
| uartProcessTask | UART 数据处理 | 高 |
| radarDataTask | 雷达数据处理 | 高 |
| bleSendTask | BLE 数据发送 | 中 |
| vitalSendTask | 生命体征数据发送 | 中 |
| wifiMonitorTask | WiFi 状态监控 | 中 |
| ledControlTask | LED 状态控制 | 低 |
| bootButtonMonitorTask | 按钮监控 | 低 |
| configClearLedTask | 配置清除 LED 控制 | 低 |

## BLE API 文档

详细的 BLE API 文档请参考 [BLE_API.md](BLE_API.md)

### 主要 API 命令
- `scanWiFi` - 扫描 WiFi 网络
- `getSavedNetworks` - 获取已保存的 WiFi 网络
- `setWiFiConfig` - 配置 WiFi 网络
- `queryStatus` - 查询设备状态
- `setDeviceId` - 设置设备 ID
- `startContinuousSend` - 启动持续发送
- `stopContinuousSend` - 停止持续发送
- `queryRadarData` - 查询雷达数据
- `echo` - 回显测试

### BLE 连接参数
- **服务 UUID**: `a8c1e5c0-3d5d-4a9d-8d5e-7c8b6a4e2f1a`
- **特征值 UUID**: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- **分包大小**: 20 字节/包
- **数据格式**: JSON（UTF-8 编码）

## 数据结构

### 传感器数据（SensorData）
```cpp
typedef struct {
    float breath_rate;           // 呼吸率
    float heart_rate;            // 心率
    uint8_t breath_valid;        // 呼吸率有效标志
    uint8_t heart_valid;         // 心率有效标志
    uint8_t presence;            // 存在状态
    uint8_t motion;              // 运动状态
    int heartbeat_waveform;      // 心跳波形
    int breathing_waveform;       // 呼吸波形
    uint16_t distance;           // 距离
    uint8_t sleep_state;         // 睡眠状态
    uint32_t sleep_time;         // 睡眠时长
    uint8_t sleep_score;         // 睡眠评分
    // ... 更多字段
} SensorData;
```

### 睡眠状态定义
| 值 | 状态 | 说明 |
|----|------|------|
| 0 | 清醒 | 处于清醒状态 |
| 1 | 浅睡 | 处于浅度睡眠 |
| 2 | 深睡 | 处于深度睡眠 |
| 3 | REM | 快速眼动睡眠 |

## 配置管理

### 设备 ID
- **有效范围**：1000-1999
- **存储位置**：ESP32 Flash（Preferences）
- **默认值**：0000（未设置）

### WiFi 配置
- **最大保存数量**：10 个网络
- **最小信号强度**：-200 dBm
- **连接超时**：15 秒
- **重连间隔**：2 秒

### 配置清除
- **触发方式**：启动时按住 Boot 按钮 3 秒
- **清除内容**：所有 WiFi 配置和设备 ID
- **LED 指示**：呼吸灯模式

## 编译与烧录

### 开发环境
- **IDE**：Arduino IDE 2.x
- **框架**：Arduino ESP32
- **编译器**：GCC for ESP32

### 依赖库
- `Arduino.h` - Arduino 核心库
- `WiFi.h` - WiFi 功能库
- `BLEDevice.h` - BLE 功能库
- `ArduinoJson.h` - JSON 处理库
- `Preferences.h` - Flash 存储库
- `HTTPClient.h` - HTTP 客户端库

### 编译步骤
1. 使用 platformIO ,在vscode中打开项目
3. 选择正确的串口
4. 点击"上传"按钮编译并烧录

## 使用说明

### 首次使用
1. 上电启动设备
2. 通过手机蓝牙连接设备
3. 使用小程序扫描 WiFi 网络
4. 配置 WiFi 连接信息
5. 设置设备 ID（可选）
6. 设备自动连接 WiFi 并开始上传数据

### 日常使用
1. 设备自动连接已保存的 WiFi
2. 雷达数据实时采集
3. 数据自动上传到云服务器
4. 可通过 BLE 查询设备状态和雷达数据

### 配置清除
1. 断电重启设备
2. 按住 Boot 按钮不放
3. 上电后保持按住 3 秒
4. 配置清除 LED 呼吸闪烁
5. 释放按钮，配置已清除

## 项目结构

```
Rader_Success_5/
├── src/
│   ├── main.cpp              # 主程序
│   ├── radar_manager.cpp      # 雷达管理器实现
│   ├── radar_manager.h        # 雷达管理器头文件
│   ├── wifi_manager.cpp       # WiFi 管理器实现
│   └── wifi_manager.h         # WiFi 管理器头文件
├── BLE_API.md                 # BLE API 文档
├── 传感器数据.txt             # 传感器数据说明
└── README.md                  # 项目说明文档
```

## 技术特点

1. **模块化设计**：雷达管理和 WiFi 管理分离，便于维护和扩展
2. **多任务处理**：使用 FreeRTOS 实现多任务并发处理
3. **队列通信**：使用 FreeRTOS 队列实现任务间数据传递
4. **流控机制**：BLE 数据发送采用流控，避免数据拥塞
5. **自动重连**：WiFi 断开后自动重连，提高系统稳定性
6. **Flash 存储**：使用 Preferences 持久化存储配置信息
7. **LED 状态指示**：直观的 LED 状态显示，便于用户了解设备状态

## 注意事项

1. **WiFi 限制**：仅支持 2.4GHz WiFi 网络
2. **BLE 连接**：一次只能连接一个 BLE 客户端
3. **数据上传**：需要确保 WiFi 连接正常才能上传数据
4. **设备 ID**：必须在有效范围内（1000-1999）
5. **配置清除**：清除配置后需要重新配置 WiFi 和设备 ID
6. **雷达安装**：建议安装在床铺正上方，距离 1-2 米

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 5.0 | 2025-02-28 | 第5版，重构代码结构，添加雷达和 WiFi 管理器模块 |

## 许可证

本项目仅供学习和研究使用。

## 联系方式

如有问题或建议，请联系项目维护者。

---

**项目地址**：http://lmhrt.cn:6771/ming/Rader_Success_5.git
