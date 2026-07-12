# Agent 联动方案（WorkBuddy + Claude Code）

PC 端 `bridge` 把各 Agent 翻译成统一下行帧，经 `transport` 下发到 ESP32；
设备只负责显示与回传指令。UI 永远只认下行帧，不关心来源。

## 0. 统一事件契约（下行 PC→ESP32，每行一条 JSON）

通信模型：**通知化推送 + 设备本地替换**。Agent 以「通知」形式下发 clawd 桌宠的
状态切换、通知弹窗、会话详细内容；设备按 sessionId 维护内存，收到快照整条覆盖，
**无需每次刷新回告 Agent**。完整定义见 `shared/protocol.md`。

```ts
// 状态切换 + 通知弹窗（高频，每次 hook 一帧）
interface NotifyFrame {
  t: 'notify';
  src: 'workbuddy' | 'claude-code' | 'system';
  sid: string;            // sessionId，设备据此归位
  name?: string;          // 会话名（生命周期事件才带）
  state?: PetState;       // 桌宠表情状态（不含 disconnected，该态由设备本地推导）
  title?: string;         // 弹窗标题
  text?: string;          // 弹窗正文（超长截断加 …）
  ts: number;
}

// 会话详细内容快照（低频，按 sid 整条覆盖设备内存）
interface SessionFrame {
  t: 'session';
  src: Source;
  sid: string;            // 覆盖键
  name?: string;
  state?: PetState;
  lastReply?: string;     // 最新回复片段
  nextStep?: string;      // 下一步动作，缺省=等用户输入
  history?: { u: boolean; x: string }[]; // 最近 ≤6 条，每条 ≤80 字
  ts: number;
}

// 心跳（5s，链路保活）
interface HeartbeatFrame { t: 'heartbeat'; ts: number }
```

设备处理约定：
- `notify` → `pet_state_set(state, src)` 切表情；有 `title`/`text` 则显示为当前通知气泡。
- `session` → 按 `sid` **整条覆盖**内存会话记录，不回告 Agent。
- `heartbeat` → 仅刷新「最近收帧时间」，≥15s 无帧则切 `disconnected`。

## 1. Claude Code（已落地：claudewatch probe + Go bridge）

### 1.1 数据通路
Claude Code `settings.json` 注册 hook → 触发 `claudewatch-probe`（Go 二进制）→
probe 从 stdin 读 hook payload，`SessionStart`/`Stop` 时本地解析 transcript 提取
`aiTitle`/`recap`，`POST` 到 bridge 的 `/ingest`（PreToolUse 走 `/permission` 同步审批）。
bridge 落库 SQLite，同时广播到 `store.Hub`。

### 1.2 下发翻译
`bridge/internal/transport/encoder.go` 的 `FramesFor(ev)` 把每个 hook 事件翻译成下行帧
（映射表见 `shared/protocol.md` §4）：`notify`（状态切换 + 弹窗）+ 生命周期事件额外一条
`session` 快照。`fanoutLoop` 订阅 Hub，逐帧经 `transport.Multi`（串口 / BLE）下发。

### 1.3 「下一步」与 history
`session` 帧的 `lastReply` 复用 probe 提取的 transcript `recap`，`name` 复用 `aiTitle`。
`nextStep` / `history` 暂未从 transcript 增量解析（预留；详见 `SPEC-SESSION-SCREEN.md` §4）。

## 2. WorkBuddy（集成点待定，两种路径）

WorkBuddy 桌面端（Windows/Mac）目前对外是**任务/对话管理 UI**，未公开本地事件 API。
Connector 预留两种接入，二选一落地：

### 路径 A：本地日志监听（推荐先期）
- 约定 WorkBuddy 把会话事件写到本机固定路径 JSONL（如 `~/.workbuddy/sessions/*.jsonl`）。
- `workbuddy.ts` `fs.watch` 监听，翻译为下行帧，逻辑与 Claude Code 对称。
- 前提：需确认/约定日志路径与格式；路径不存在时静默降级。

### 路径 B：WorkBuddy 桥接（后期）
- WorkBuddy 侧加轻量扩展/桥接进程，主动向 `bridge` 推送 `AgentEvent`。
- 前提：需 WorkBuddy 提供扩展或 webhook 能力。

## 3. 设备回传指令（上行 ESP32→PC）

`shared/protocol.md` 定义 `Command`：
- `{"cmd":"voice_start"}` / `{"cmd":"voice_text","text":"..."}` —— 语音输入
- `{"cmd":"session_next"}` / `{"cmd":"session_prev"}` —— 右滑/左滑切会话
- `{"cmd":"session_scroll","dir":1|-1}` —— 上下滑翻看内容
- `{"cmd":"focus_session","id":"..."}` —— 聚焦某会话
- `{"cmd":"mute_toggle","value":true|false}` —— 控制中心「勿扰」：让 bridge 暂停下发 notification 类事件

`bridge` 收到后：语音→本地 STT→发给激活 Agent；切会话/滚动→调整当前展示索引（也可由设备本地维护）。

控制中心（屏④）的 WiFi / 蓝牙 / 亮度 / 传输开关为 ESP32 **本地功能**，不回传；仅「勿扰」通过 `mute_toggle` 上行，通知 bridge 暂停下发 `notification` 类事件。

## 4. Connector 接口

```ts
// bridge/src/connectors/connector.ts
import { AgentEvent } from '../core/types';
export interface Connector {
  readonly name: string;
  start(onEvent: (e: AgentEvent) => void): void;
  stop(): void;
}
```

新增 Agent（Gemini CLI、Codex…）只需实现该接口并在 `main.ts` 注册。

## 5. 安全

- 串行仅本机；BLE/WiFi 模式建议配对手/局域网内。
- 钩子只发事件元数据 + 文本片段，不上传代码库内容。
- 可在 `config/settings.json` 关闭任一 Connector。
