# 通信协议（shared/protocol.md）

ESP32（设备）与 PC `bridge` 之间的通信。帧格式：**每行一条 JSON**，以 `\n` 分隔（便于串口逐行解析）。

## 0. 通信模型：通知化推送 + 设备本地替换

- **纯推送**：`bridge` 主动把 Agent 状态推给设备，设备**永不**主动请求刷新。
- **通知即状态切换**：Agent 把桌宠（clawd）的**状态切换**、**通知弹窗**、**会话详细内容**统一以「通知」形式下发——每条 `notify` 帧既驱动桌宠表情切换，又可带弹窗内容。
- **内存替换语义**：设备按 `sid`（sessionId）维护内存会话表。收到 `session` 帧，**整条覆盖**该 `sid` 的记录（name / state / lastReply / nextStep / history 全量替换）；收到 `notify` 帧只刷新该 `sid` 的实时 `state` 与当前弹窗。设备**不需要**在每次刷新时回告 Agent——Agent 只管推，设备只管覆盖。

## 1. 物理通道（可配，见 config/settings.json）

| 通道 | 说明 | bridge 实现 |
|---|---|---|
| `serial`（默认） | USB-CDC 虚拟串口，插上即用，零配置 | `transport.SerialWriter`，`os.OpenFile` 直写，自动扫描 `/dev/ttyACM*` / `COMn` |
| `ble` | 蓝牙串口服务（GATT UART） | `transport.BLEWriter`（桩，后续接 BlueZ/WinRT） |
| `wifi` | 局域网 TCP，设备做 client 连 PC | 预留 |

> 无论哪种通道，payload 都是下面的 JSON 行。多通道可同时启用，`bridge` 用 `transport.Multi` 扇出，单通道出错不影响其他。

## 2. 下行：PC → ESP32

统一帧，`t` 区分类型，未用字段省略。

### 2.1 `notify` —— 状态切换 + 通知弹窗（高频，每次 hook 一帧）

```json
{"t":"notify","src":"claude-code","sid":"s1","name":"auth-refactor","state":"building","title":"Edit","text":"edited 3 files, running tests…","ts":1718000000000}
```

| 字段 | 含义 |
|---|---|
| `t` | 固定 `"notify"` |
| `src` | 来源：`claude-code` / `workbuddy` / `system` |
| `sid` | 会话 ID，设备据此归位 |
| `name` | 会话名（可选，生命周期事件才带） |
| `state` | 桌宠表情状态，见 §4 枚举 |
| `title` | 弹窗标题（可选） |
| `text` | 弹窗正文（可选，超长截断加 `…`） |
| `ts` | 毫秒时间戳 |

设备处理：`pet_state_set(state, src)` 切表情；若有 `title`/`text` 显示为当前通知气泡。

### 2.2 `session` —— 会话详细内容快照（低频，全量替换内存）

```json
{"t":"session","src":"claude-code","sid":"s1","name":"auth-refactor","state":"building","lastReply":"Edited 3 files, running tests…","nextStep":"Run npm test","history":[{"u":true,"x":"User: refactor login"},{"u":false,"x":"Agent: edited 3 files"}],"ts":1718000000000}
```

| 字段 | 含义 |
|---|---|
| `t` | 固定 `"session"` |
| `sid` | 会话 ID —— **覆盖键** |
| `name` / `state` | 同 notify |
| `lastReply` | 最新回复片段 |
| `nextStep` | 下一步动作，缺省表示等用户输入 |
| `history` | 最近消息尾（≤6 条，每条 ≤80 字截断）；`u`=true 用户侧，`x`=文本 |
| `ts` | 毫秒时间戳 |

设备处理：按 `sid` **整条覆盖**内存中的会话记录，无需回告 Agent。`bridge` 在 `SessionStart` / `Stop` / `SessionEnd` 等生命周期事件下发；`lastReply` 复用 transcript 的 recap，`name` 复用 ai-title。

### 2.3 `heartbeat` —— 链路保活

```json
{"t":"heartbeat","ts":1718000000000}
```

无业务字段，仅维持「最近收帧时间」。见 §5。

## 3. 上行：ESP32 → PC（Command）

设备回传指令（本轮 `bridge` 下行优先，上行解析为下一步；契约先行）：

```json
{"cmd":"voice_start"}
{"cmd":"voice_text","text":"帮我把登录模块重构一下"}
{"cmd":"session_next"}
{"cmd":"session_prev"}
{"cmd":"session_scroll","dir":1}
{"cmd":"focus_session","id":"s1"}
{"cmd":"mute_toggle","value":true}
```

| cmd | 含义 | bridge 处理 |
|---|---|---|
| `voice_start` | 请求语音输入 | 在 PC 端开麦 STT |
| `voice_text` | 直接带文本 | 作为用户指令发给激活 Agent |
| `session_next` / `session_prev` | 右滑/左滑切会话 | 调整当前展示索引（可设备本地维护） |
| `session_scroll` | 上下滑翻内容 | 调整当前会话滚动位置（设备本地维护） |
| `focus_session` | 聚焦某会话 | 切到该会话（设备本地维护，无需回告） |
| `mute_toggle` | 控制中心「勿扰」 | bridge 暂停下发 `notification` 态 / 弹窗 |

> 切会话 / 滚动 / 聚焦均为**设备本地内存操作**（数据已由 `session` 帧驻留），不上行、不触发 Agent 刷新——这是 §0「无需每次刷新回告」的直接体现。

## 4. 状态枚举（对齐固件 `pet_state_t`）

`state` 取值（小写），优先级高→低：

`error` > `permission` > `notification` > `building` > `typing` > `thinking` > `speaking` > `waiting` > `happy` > `idle` > `sleeping`

> `disconnected` **从不下发**——它由设备本地推导（见 §5）。

### Hook → state 映射（bridge `transport.FramesFor`）

| hook_event_name | state | 附带 |
|---|---|---|
| `SessionStart` | `thinking` | 同时发 `session` 快照 |
| `UserPromptSubmit` | `typing` | `notify.text` = prompt 摘要 |
| `PreToolUse` | `building` | `notify.title` = tool_name |
| `PostToolUse` | `typing` | `notify.title` = tool_name，`text` = 结果摘要 |
| `Notification` | `notification` | `notify.text` = message |
| `Stop` | `happy` | 同时发 `session` 快照（lastReply = recap） |
| `SessionEnd` | `idle` | 同时发 `session` 快照 |
| 权限请求（PreToolUse 同步审批） | `permission` | `notify.title` = tool_name |

## 5. 心跳与断连检测

- `bridge` 在无其他事件时，每隔约 **5s** 下发一条 `heartbeat`。
- 设备端维护「最近收帧时间」：若 **≥ 15s**（3 个心跳周期）未收到任何下行帧，判定主机失联 → 强制切 `disconnected`。
- 任意下行帧（含 `heartbeat` / `notify` / `session`）到达即清除 `disconnected`，恢复由 `state` 驱动的真实表情。
- `disconnected` 从不出现在下行帧的 `state` 里，它纯粹由设备本地推导。

## 6. 约定

- 单行 ≤ 256 字节（串口缓冲友好）；`notify` / `heartbeat` 严格遵守，过长 `text` 截断并加 `…`。
- `session` 快照允许更长（带 history），但 `history` ≤ 6 条、每条 ≤ 80 字截断，整体 ≤ 1.5KB。
- 数字时间戳用毫秒（与 TS `Date.now()` 一致）。
- 未知字段忽略；未知 `cmd` 忽略，不影响其他指令。
- 设备端解析失败的行直接丢弃，不阻塞后续帧。
