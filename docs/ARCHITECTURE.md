# 系统架构（Architecture）

## 1. 一句话

**PC 采集 Agent 事件 → 下发到 ESP32 → 圆形屏显示桌宠与多会话。** ESP32 是显示器 + 输入，
PC 是「大脑」（解析 Agent、聚合成事件）。两者通过串口 / BLE / WiFi 通信。

```
┌──────────────────────── PC（Windows / macOS）────────────────────────┐
│  WorkBuddy            Claude Code                                     │
│   (任务/对话)          (hooks + ~/.claude/projects/*.jsonl)            │
│      │                    │                                            │
│      ▼                    ▼                                            │
│  ┌──────────────────────────────────────────────────┐                 │
│  │  bridge/  (Node + TypeScript)                      │                 │
│  │   connectors/* → core 聚合(AgentEvent)             │                 │
│  │   → state-machine(表情) / session-store(时间排序)  │                 │
│  └───────────────────────┬──────────────────────────┘                 │
│                          │  transport（Serial / BLE / WiFi）           │
└──────────────────────────┼────────────────────────────────────────────┘
                           │  ← AgentEvent(下行) / Command(上行) →
                           ▼
┌──────────────── ESP32-S3 AMOLED（466×466 圆形）────────────────┐
│  firmware/ (ESP-IDF + LVGL9)                                     │
│   transport.c  解析事件 / 回传指令                               │
│   gesture.c    触摸滑动分类（左/右/上/下）                       │
│   ui_pet.cpp   ① 桌宠主页面（圆形中心 + Source Halo）          │
│   ui_negative.cpp ② 左滑负一屏（语音+硬件）                     │
│   ui_session.cpp  ③ 右滑/上下滑 会话页                          │
│   ui_control.cpp  ④ 首页下滑 控制中心（WiFi/蓝牙/设置）         │
│   pet_states.c 表情状态机（移植 core 的优先级）                 │
└────────────────────────────────────────────────────────────────┘
```

![系统架构](visuals/architecture.svg)

## 2. 数据流（一次「收到 Agent 消息」）

1. Claude Code 触发 `PostToolUse` 钩子 → `pet-hook.js` 把事件 `POST` 到 bridge 本地端口。
2. `bridge` 的 `claude-code` 连接器转成统一 `AgentEvent`，送入 `core` 聚合。
3. `state-machine` 计算表情状态（多 Agent 优先级）；`session-store` 更新该会话（时间序）。
4. `transport` 把 `AgentEvent` 经 **串口/USB-CDC**（或 BLE/WiFi）写成一行 JSON 下发。
5. ESP32 `transport.c` 收到并解析 → 通知 `ui_pet`：宠物切「通知」表情 + 边缘高亮/气泡。
6. 用户在圆形屏**右滑**查看该会话、**上下滑**翻看内容；若**左滑**进入负一屏点语音，
   ESP32 经 `transport` 回传 `Command {cmd:"voice_start", text?}`，bridge 转给激活的 Agent。

## 3. 状态优先级（桌宠表情）

当多 Agent 活跃，`state-machine` 取最显著状态驱动表情（与参考项目同源，12 态）：

```
disconnected > error > permission > notification(新消息) > building > typing
> thinking > speaking > waiting > happy > idle > sleeping
```

> `disconnected` 是**设备级**状态：transport 链路断开 / 心跳超时时由 `pet_states.c` 直接置位，优先级高于一切 Agent 状态，避免失联后仍显示过时的 building/error。
> 其余 11 态由 `bridge` 的 `state-machine` 计算，设备端 `pet_states.c` 是其精简移植（仅渲染所需子集），不跑完整 TS 逻辑。

## 4. 圆形屏约束（关键）

设计系统详见 [`DESIGN-SYSTEM.md`](DESIGN-SYSTEM.md)。核心约束：

- 屏幕 466×466，**有效内容半径 ≈ 210px**（边缘留安全区，避免圆形裁切）。
- 背景色使用 `#0A0A0C` 纯黑，发挥 AMOLED 省电优势。
- Source Halo（外缘状态光环）在四屏中保持一致，颜色随当前 Agent/状态变化。
- 不用矩形「窗口」概念：多视图（主页 / 负一屏 / 会话页 / 控制中心）是**同一圆形画布上的不同视图**，靠滑动切换，而非多窗口。
- 文案/控件沿环形或居中堆叠排布；长文本截断 + 上下滑翻页。
- 状态用**环形进度/边缘高亮**表达（颜色随 PetState），比角落小图标更适配圆形。

## 5. 传输与协议

- 默认 **USB-CDC 串口**（插上即用，零配置）；BLE / WiFi-TCP 为可选项（见 `shared/protocol.md`）。
- 帧格式：**每行一条 JSON**（`\n` 分隔）。下行 `AgentEvent`，上行 `Command`。
- bridge 与 ESP32 都只解析自己关心的字段，互不阻塞。

## 6. 安全边界

- 串口仅本机；BLE/WiFi 模式建议配对手/局域网内。
- 钩子只发事件元数据 + 文本片段，不上传代码库内容。
- 可在 `config/settings.json` 关闭任一 Connector。
