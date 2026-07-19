# BLE + WiFi 连接功能设计（2026-07-19）

## 目标

- Agent 数据经 **BLE** 下发到 ESP32-S3 设备，替代/并行于 USB 串口
- 设备经 BLE **配网连上 WiFi**（仅验证连通，WiFi 暂无业务用途）
- bridge Web UI 新增「设备连接」页；设备屏控制中心接真实状态 + 新增连接详情页
- BLE GATT 采用标准 NUS，为未来手机端（Android/iOS）接入留路

## 已定决策

| 问题 | 决策 |
|---|---|
| 手机端形态 | 远期再做，本轮不实现；GATT 用标准 NUS 保兼容 |
| WiFi 用途 | 仅配网连通测试，数据仍走 BLE |
| 配网入口 | bridge Web 界面新页面 |
| 设备端 UI | 控制中心真实状态 + 独立连接详情页 |
| BLE 栈 | 设备 NimBLE；bridge tinygo-org/bluetooth |
| 配网方案 | 自定义 GATT Service（非 BluFi） |

## 架构

```
claudewatch (Go)
  fanoutLoop → transport.Multi → SerialWriter（保留）
                              → BLEWriter（真实实现：扫描/连接/NUS 分片写/自动重连）
  HTTP: /api/ble/status /api/ble/scan /api/ble/connect /api/ble/disconnect
        /api/wifi/provision
  Web: DeviceTab.svelte（扫描/连接、信号、WiFi 表单、配网结果）

ESP32-S3 (ESP-IDF + NimBLE)
  bt_stack.c    NimBLE host + GAP 广播（名 ClawdPet-XXXX）+ GATT server
  ble_proto.c   NUS RX 分片 → 按 \n 重组行 → 共用帧解析
  frame_parse.c 从 serial_protocol.c 抽出的行 JSON 解析（串口/BLE 共用）
  wifi_prov.c   配网特征回调 → esp_wifi STA 连接 → NVS 保存 → 状态 notify
  ui_connect.c  连接详情页（BLE 状态/设备名/RSSI、WiFi 状态/IP/MAC）
  ui_control.c  WiFi/蓝牙开关接真实状态
```

## BLE GATT 定义

- NUS：Service `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
  - RX `6E400002-…`（Write/WriteNoResp，bridge→设备，数据帧分片）
  - TX `6E400003-…`（Notify，设备→bridge，预留上行指令 + 状态）
- 配网 Service 自定义 base UUID `4A1A0000-…-E50E24DCCA9E`（ClawdPet 专用）
  - SSID `4A1A0001-…`（Write，UTF-8 ≤32B）
  - Password `4A1A0002-…`（Write，≤64B）
  - Commit `4A1A0003-…`（Write，任意值触发连接）
  - Status `4A1A0004-…`（Read/Notify，JSON：`{"s":"idle|connecting|ok|fail","ip":"…","rssi":-55,"err":…}`）
- 连接后协商 MTU 247；下行帧按 (MTU-3) 分片，设备端按 `\n` 重组

## 数据流

1. Agent 数据：hook → fanoutLoop → Multi → BLEWriter → NUS RX（分片）→ ble_proto 重组 → frame_parse → UI（协议零改动）
2. 配网：Web 表单 → /api/wifi/provision → BLE 写 SSID/Password/Commit → 设备连 WiFi → Status notify → bridge 轮询展示
3. 状态上行：BLE/WiFi 状态 → 设备 UI；NUS TX 预留

## 错误处理

- BLE 未连接：Write 返回错误，Multi 丢帧不阻塞（现有语义）
- 断连：设备广播自动重启；bridge 后台每 5s 重扫重连（可关）
- 心跳：≥15s 无下行帧 → 设备本地 disconnected（现有逻辑不变）
- WiFi 失败：Status `fail` + 错误码，Web 页与设备详情页同时显示
- WSL2 无蓝牙：bridge 需跑原生系统，UI 在无适配器时显示提示

## 测试

- Go：分片/重组、重连状态机、provision 端点参数校验单测
- 固件：idf.py build 通过；BLE 用 nRF Connect 手机 App 验证广播/GATT/配网
- 端到端：拔串口纯 BLE 跑通 notify/session/heartbeat 下发
