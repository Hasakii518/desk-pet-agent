# 项目布局（Project Layout）

```
desk-pet-agent/
├── README.md
├── bridge/                       # PC 端（Windows / macOS）桥接进程：采集 Agent 事件并下发
│   ├── package.json
│   └── src/
│       ├── main.ts              # 入口：装配连接器 → 聚合 → 状态解析 → transport 下发
│       ├── transport.ts         # 串口/USB-CDC（或 BLE/WiFi）收发 AgentEvent / Command
│       ├── core/                # 纯逻辑（TS，可单测）
│       │   ├── types.ts         # PetState / AgentEvent / SessionSummary / HardwareStatus
│       │   ├── state-machine.ts # 多 Agent 状态优先级 → 桌宠表情
│       │   ├── event-bus.ts     # 本地聚合（接收 Hook POST + 连接器事件）
│       │   └── session-store.ts # Session 列表（按时间排序）+ 变化订阅
│       └── connectors/          # Agent 联动层（每个 Agent 一个文件）
│           ├── connector.ts     # Connector 接口
│           ├── claude-code.ts   # Claude Code：hooks + JSONL 会话解析
│           ├── workbuddy.ts     # WorkBuddy：本地日志监听 / 桥接
│           └── hooks/
│               ├── install-hooks.js  # 向 Claude Code settings.json 注入钩子
│               └── pet-hook.js       # 钩子脚本：POST 事件到 bridge
│
├── firmware/                    # 设备端：ESP32-S3 AMOLED（466×466 圆形）LVGL9
│   ├── CMakeLists.txt           # 顶层 CMake
│   ├── sdkconfig.defaults
│   ├── README.md                # 固件 demo 说明
│   └── main/
│       ├── CMakeLists.txt       # 组件源文件列表
│       ├── app_main.c           # 入口 + 手势分发 + 视图切换
│       ├── ui_common.h          # 屏幕尺寸、颜色 Token、状态枚举
│       ├── mock_data.h / .c     # mock 会话与桌宠状态数据
│       ├── gesture.h / .c       # 触摸滑动分类（左/右/上/下 + 底边上滑回家）
│       ├── ui_pet.h / .c        # ① 主页面：圆形中心桌宠 + 通知气泡
│       ├── ui_negative.h / .c   # ② 左滑负一屏：语音 + 硬件状态
│       ├── ui_session.h / .c    # ③ 会话页：单会话 + 右滑/上下滑导航
│       ├── ui_control.h / .c    # ④ 下拉控制中心：WiFi/蓝牙/设置/亮度/勿扰/传输
│       └── assets/
│           └── clawd/           # Clawd 素材（经授权，需转换为 LVGL C 数组使用）
│
├── shared/
│   └── protocol.md              # 通信协议：AgentEvent 下行 / Command 上行（行分隔 JSON）
│
├── config/
│   └── settings.example.json    # 传输方式、串口、设备名、语音引擎、Connector 开关
│
├── src/
│   └── assets/
│       └── pet/                 # 桌宠美术资源
│           └── clawd/           # Clawd 角色素材（经授权，详见目录内 LICENSE）
│               ├── gif/         # 动态参考
│               ├── svg/         # 矢量素材（推荐 ESP32 使用）
│               ├── LICENSE      # 原项目美术资源授权声明
│               ├── NOTICE.md    # 第三方素材声明
│               └── README.md    # 状态映射与使用建议
│
├── scripts/
│   └── dev.sh                   # 一键：起 bridge + 提示烧录 firmware
│
└── docs/
    ├── PROJECT-LAYOUT.md
    ├── ARCHITECTURE.md
    ├── DESIGN-SYSTEM.md
    ├── SPEC-DESKTOP-PET.md
    ├── SPEC-NEGATIVE-SCREEN.md
    ├── SPEC-SESSION-SCREEN.md
    ├── SPEC-CONTROL-CENTER.md
    └── AGENT-LINK.md
```

## 职责一句话

| 目录 | 职责 |
|---|---|
| `bridge/` | PC 端大脑：采集 WorkBuddy/Claude Code 事件，聚合成 `AgentEvent`，下发给设备 |
| `firmware/` | 设备端显示+输入：LVGL 圆形 UI、手势识别、事件渲染 |
| `shared/` | 两端共享的通信协议（字段语义、帧格式） |
| `config/` | 运行配置（传输、硬件、语音、开关） |
| `src/assets/` | 桌宠、图标等美术资源 |
| `docs/` | 规划与规格 |

## 设计原则

1. **ESP32 只显示、PC 才思考**：重逻辑（JSONL 解析、next-step 提取、状态聚合）全在 `bridge`，
   设备只接收已处理好的 `AgentEvent`。
2. **Connector 隔离**：`bridge` 只认 `AgentEvent`，新增 Agent 只加一个连接器文件。
3. **圆形优先**：所有 UI 落在 466×466 圆内（安全半径 ≈210），用环形/居中布局，不做溢出矩形。
4. **滑动即导航**：多视图（主页/负一屏/会话页/控制中心）是同一圆形画布的不同视图，靠左/右/上/下滑动切换，无多窗口概念。
5. **协议即契约**：`shared/protocol.md` 定义字段，两端独立演进但互认 JSON 行。
