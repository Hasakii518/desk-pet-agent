# Agent 联动方案（WorkBuddy + Claude Code）

PC 端 `bridge` 把各 Agent 翻译成统一 `AgentEvent`，经 `transport` 下发到 ESP32；
设备只负责显示与回传指令。UI 永远只认 `AgentEvent`，不关心来源。

## 0. 统一事件契约（AgentEvent，下行 PC→ESP32）

```ts
interface AgentEvent {
  id: string;
  source: 'workbuddy' | 'claude-code' | 'system';
  sessionId: string;
  sessionName: string;
  type: 'status' | 'message' | 'plan' | 'next-step' | 'next-session' | 'permission';
  state?: PetState;     // 桌宠表情状态（不含 disconnected，该态由设备本地推导）
  text?: string;        // 消息 / 回复内容
  nextStep?: string;    // Claude Code 返回的下一步动作
  sessionIndex?: number;// 会话在「时间序列表」中的位置（可选，便于设备翻页）
  timestamp: number;
}
```

## 1. Claude Code（已验证可行，参考 clawd-on-desk）

### 1.1 钩子注入
`bridge/src/connectors/hooks/install-hooks.js` 向 Claude Code `settings.json` 写入命令钩子，
事件触发时调用 `pet-hook.js`：

| 钩子事件 | 发送的 AgentEvent |
|---|---|
| `SessionStart` | status: thinking |
| `PreToolUse` | status: building |
| `PostToolUse` | message（工具结果摘要） |
| `UserPromptSubmit` | status: typing |
| `Stop` | status: idle / happy |
| `Notification` | message / notification |

### 1.2 钩子脚本
`pet-hook.js` 读取钩子环境变量（`CLAUDE_SESSION_ID` 等），组装 `AgentEvent`，
`POST` 到 `bridge` 本地端口（由 `event-bus.ts` 接收）。

### 1.3 「下一步」解析
`claude-code.ts` 用 `fs.watch` 监听 `~/.claude/projects/**/*.jsonl` 增量，
解析最后一条 assistant 消息提取 `tool_use` / plan 步骤 → 发 `next-step` 事件
（详见 `SPEC-SESSION-SCREEN.md` 第 4 节）。

## 2. WorkBuddy（集成点待定，两种路径）

WorkBuddy 桌面端（Windows/Mac）目前对外是**任务/对话管理 UI**，未公开本地事件 API。
Connector 预留两种接入，二选一落地：

### 路径 A：本地日志监听（推荐先期）
- 约定 WorkBuddy 把会话事件写到本机固定路径 JSONL（如 `~/.workbuddy/sessions/*.jsonl`）。
- `workbuddy.ts` `fs.watch` 监听，翻译为 `AgentEvent`，逻辑与 Claude Code 对称。
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
