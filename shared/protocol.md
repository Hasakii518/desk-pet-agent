# 通信协议（shared/protocol.md）

ESP32（设备）与 PC `bridge` 之间的通信。帧格式：**每行一条 JSON**，以 `\n` 分隔（便于串口逐行解析）。

## 1. 物理通道（可配，见 config/settings.json）

| 通道 | 说明 |
|---|---|
| `serial`（默认） | USB-CDC 虚拟串口，插上即用，零配置 |
| `ble` | 蓝牙串口服务（GATT UART），需配对手 |
| `wifi` | 局域网 TCP，设备做 client 连 PC |

> 无论哪种通道，payload 都是下面的 JSON 行。

## 2. 下行：PC → ESP32（AgentEvent）

```json
{"type":"status","source":"claude-code","sessionId":"s1","sessionName":"refactor","state":"building","timestamp":1718000000000}
{"type":"message","source":"workbuddy","sessionId":"s2","sessionName":"周报","text":"已生成草稿，请确认","timestamp":1718000001000}
{"type":"next-step","source":"claude-code","sessionId":"s1","nextStep":"运行 npm test 验证改动","timestamp":1718000002000}
```

字段见 `docs/AGENT-LINK.md` 第 0 节。设备只解析关心的字段：
- `state` → 驱动桌宠表情
- `text` → 通知气泡 / 会话最新回复
- `nextStep` → 会话页高亮
- `sessionId/sessionName/timestamp` → 维护时间序会话列表

## 3. 上行：ESP32 → PC（Command）

```json
{"cmd":"voice_start"}
{"cmd":"voice_text","text":"帮我把登录模块重构一下"}
{"cmd":"session_next"}
{"cmd":"session_prev"}
{"cmd":"session_scroll","dir":1}
{"cmd":"focus_session","id":"s1"}
```

| cmd | 含义 | bridge 处理 |
|---|---|---|
| `voice_start` | 请求语音输入 | 在 PC 端开麦 STT |
| `voice_text` | 直接带文本（设备本地识别时） | 作为用户指令发给激活 Agent |
| `session_next` / `session_prev` | 右滑/左滑切会话 | 调整当前展示索引 |
| `session_scroll` | 上下滑翻内容 | 调整当前会话滚动位置 |
| `focus_session` | 聚焦某会话 | 切到该会话 |

## 4. 约定

- 单行 ≤ 256 字节（串口缓冲友好）；过长文本由发送端截断并标 `(…)`。
- 数字时间戳用毫秒（与 TS `Date.now()` 一致）。
- 未知字段忽略；未知 `cmd` 忽略，不影响其他指令。
- 设备端解析失败的行直接丢弃，不阻塞后续帧。

## 5. 心跳与断连检测

- `bridge` 在无其他事件时，每隔约 **5s** 下发一条轻量心跳：
  `{"type":"heartbeat","timestamp":...}`（计入 256 字节限制，无业务字段）。
- 设备端 `transport.c` 维护「最近收帧时间」：若 **≥ 15s**（3 个心跳周期）未收到任何下行帧，判定主机失联 → `pet_states.c` 强制切 `disconnected`。
- 任意下行帧（含心跳、status、message）到达即清除 `disconnected`，恢复由 `state` 驱动的真实表情。
- `disconnected` 从不出现在 `AgentEvent.state`（bridge 无法感知设备链路），它纯粹由设备本地推导。
