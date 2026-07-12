# ClaudeWatch

跨平台 Claude Code hook 观测代理。probe 捕获 Claude Code 的所有 hook 事件，按 session 归类写入本地 SQLite，浏览器实时查看。

- **probe**：Claude Code hook 触发，单行 JSON 通过 HTTP POST 给 agent，失败静默不阻塞
- **agent**：HTTP+WebSocket server，批写 SQLite（WAL），实时推送事件给 UI
- **interface**：Svelte 前端，嵌入 agent 二进制，浏览器打开即用

## 架构

- **probe**：Claude Code hook 触发，单行 JSON 通过 HTTP POST 给 agent，失败静默不阻塞。
- **agent**：HTTP+WebSocket server，批写 SQLite（WAL），实时推送事件给 UI；同时是 ESP32 的下行通道（串口/BLE）。
- **interface**：Svelte 前端，嵌入 agent 二进制，浏览器打开即用。

agent 与 probe 的部署形态随运行环境自动适配：

| 环境 | agent | probe | 浏览器 |
|------|-------|-------|--------|
| **Windows 原生**（Claude Code 跑在 Windows） | `claudewatch.exe`，启动文件夹自启 | `claudewatch-probe.exe`，hooks 注册到 `%USERPROFILE%\.claude` | `http://127.0.0.1:7777` |
| **WSL**（Claude Code 在 WSL，agent 在 Windows 宿主） | `claudewatch.exe`（Windows 宿主） | `claudewatch-probe`（WSL 内），经网关 IP 连宿主 | `http://127.0.0.1:7777`（Windows 侧） |
| **macOS 原生** | `/usr/local/bin/claudewatch`，launchd 自启 | `/usr/local/bin/claudewatch-probe`，hooks 注册到 `~/.claude` | `http://127.0.0.1:7777` |
| **Linux 原生** | `/usr/local/bin/claudewatch`，systemd --user 自启 | `/usr/local/bin/claudewatch-probe`，hooks 注册到 `~/.claude` | `http://127.0.0.1:7777` |

安装脚本会**自动探测**上述环境，无需手动指定。配置与 token 统一放在跨平台配置目录：

- Windows：`%APPDATA%\ClaudeWatch\`
- macOS / Linux：`~/.config/claudewatch\`

## 构建（发布包）

在**有 Go + Node 的机器**上交叉编译出全平台二进制（对方机器无需 Go/Node）：

```bash
cd bridge
make release     # 前端 + agent(win/mac/linux) + probe(win/mac/linux) → bin/
```

产物在 `bin/`：`claudewatch.exe`、`claudewatch-linux`、`claudewatch-darwin`、
`claudewatch-probe.exe`、`claudewatch-probe`、`claudewatch-probe-darwin`。

> Go 工具链默认 `~/.g/versions/1.23.12`，可用 `GOROOT=` / `GO=` 环境变量覆盖：
> `make GO=/usr/local/go/bin/go release`。

把 `bin/` 连同 `install/` 目录发给对方即可（或打成 zip）。

## 安装（对方机器，一键）

### Windows

普通用户 PowerShell 运行 `install/install.ps1`：

```powershell
powershell -ExecutionPolicy Bypass -File install\install.ps1
# 可选参数:
#   -ProbeTarget windows | wsl | auto(默认，自动探测，优先 WSL)
#   -ClaudeConfigDir "C:\Users\<user>\.claude"   # 自定义 Claude Code hooks 配置目录
```

脚本（**不写死任何用户名/发行版**）会：
- 部署 `claudewatch.exe` 到 `%APPDATA%\ClaudeWatch\`，生成并持久化 token，写 `config.json`
- 添加启动文件夹快捷方式（开机自启，参数 `--config`）
- **自动探测** probe 落点（`auto` 模式优先 WSL，可用 `-ProbeTarget windows` 强制原生 Windows）：在 WSL 内跑 `install.sh` 装 probe，或装原生 Windows probe 并幂等合并 hooks 到 Claude Code 的 `settings.json`（默认 `%USERPROFILE%\.claude`，可用 `-ClaudeConfigDir` 指定其它路径，如 tme-claude）
- 立即启动 agent

### macOS / Linux（及 WSL 内的 probe）

```bash
cd bridge
./install/install.sh                 # 自动探测 Darwin / Linux / WSL
# 可选: -c /path/to/claude-config-dir   # 自定义 Claude Code hooks 配置目录
```

- **macOS 原生**：部署 agent 到 `/usr/local/bin`，注册 launchd 自启，装 probe + hooks
- **Linux 原生**：部署 agent 到 `/usr/local/bin`，注册 systemd --user 自启，装 probe + hooks
- **WSL**：仅装 probe（agent 在 Windows 宿主），从 Windows 动态取 token、写 `agent.addr`（网关 IP）、合并 hooks

> ⚠️ **不要用 `sudo ./install.sh`**。脚本只在写 `/usr/local/bin` 时内部调用 sudo，其余操作在普通用户 HOME 下。sudo 会让 `$HOME` 变成 `/root` 写错位置。

### 打开界面

浏览器访问 `http://127.0.0.1:7777`。

**左侧栏**：☰ 按钮可折叠成 32px 窄条；展开时右侧边缘可**拖拽调整宽度**（120–500px），双击边缘重置。

**Serial 标签页**（ESP32 固件日志）：
- **方向过滤**：`all` / `TX ▼`（下发） / `RX ▲`（设备上行）
- **增量滚动**：日志增量追加不重排，上翻时自动暂停跟随；`follow` 开关 + 「↓ 回到底部」按钮
- **连接控制**：红点→ **Connect**，绿点→ **Disconnect**

**Logs 标签页**（agent 自身日志）：按级别过滤，`auto-scroll` 滚动感知。

## ESP32 通信

bridge 与 ESP32 经**串口**（USB‑CDC）通信，每行一条 JSON + `\n` 分隔。

### 下行帧类型

| `t` | 说明 | 触发时机 |
|-----|------|----------|
| `notify` | 状态切换 + 通知弹窗 | 每条 hook 事件 |
| `session` | 会话快照（设备按 `sid` 覆盖内存） | SessionStart / Stop / SessionEnd + 初始同步 |
| `heartbeat` | 链路保活 | 每 5s |

### 初始同步

串口重连后，bridge 等待 500ms 后下发最近 **3 条 session 快照**（`state: "idle"`），帧间间隔 **800ms**。

### 会话心跳

每 **20s** 扫描等待用户操作的 session，按优先级重复下发状态通知（每次仅发一条）：

| 优先级 | state | 条件 | 弹窗 text |
|--------|-------|------|-----------|
| 1 | `permission` | 有待审批的 PreToolUse | `等待审批: <tool_name>` |
| 2 | `waiting` | 最新事件为 Stop（等待用户回复） | truncated recap |

### 帧大小

所有下行帧截断至 **≤250 字节**（`MaxText=16` runes、`MaxName=20` runes），确保不超 ESP32 串口行缓冲（256B），并用 **`…`** 标记截断。

## 配置（`config.json`）

agent 启动解析顺序：**命令行 flag(显式) > 环境变量 > config.json > 内置默认**。
配置文件在 `%APPDATA%\ClaudeWatch\config.json`（Win）/ `~/.config/claudewatch/config.json`（Mac/Linux）。

| 字段 | 默认 | 说明 |
|------|------|------|
| `addr` | `:7777` | 监听地址；要跨机访问用 `0.0.0.0:7777` |
| `serialPort` | `auto` | 串口路径或 `auto`（自动扫描 `/dev/cu.usbmodem*` 等） |
| `db` | 配置目录/`events.db` | SQLite 路径 |
| `token` | 空 | 鉴权 token；空则由 agent 自动生成并持久化到 `token` 文件 |
| `wslDistro` | 空 | WSL 发行版名（doctor UNC 诊断用） |
| `ble` | `false` | 启用 BLE 传输（桩） |

token 若最终为空，agent 自动生成 32 字节随机 hex 并写入配置目录的 `token` 文件，
供 probe 共享（probe 读取同一配置文件目录下的 `token`）。

## 开发

```bash
# 前端热重载（Vite dev server，proxy 到 agent :7777）
cd frontend && npm run dev
# 浏览器开 http://127.0.0.1:5173

# agent 热改（手动重启）
make dev-agent

# 模拟一个 hook 事件测 probe
make dev-probe
```

构建产物路径：
- 前端 → `web/dist/`（被 agent 通过 `//go:embed` 嵌入）
- agent → `bin/claudewatch`
- Windows agent → `bin/claudewatch.exe`

## 目录结构

```
claudewatch/
├── cmd/
│   ├── claudewatch/         # agent 主程序
│   └── claudewatch-probe/   # 探针
├── internal/
│   ├── protocol/            # Event 结构
│   └── store/               # SQLite 批写 + 查询 + Hub (live push)
├── web/                     # //go:embed 前端产物
├── frontend/                # Svelte 源码 (Vite)
├── install/
│   ├── install.sh           # macOS / Linux / WSL（自动探测）
│   └── install.ps1          # Windows 本体（自动探测 probe 落点）
├── env.sh                   # 项目内激活 Go 1.23.12
├── .go-version
├── go.mod                   # go 1.23.0 + toolchain go1.23.12
└── Makefile
```

## 数据

- DB 路径：配置目录下的 `events.db`（Win `%APPDATA%\ClaudeWatch\` / Mac·Linux `~/.config/claudewatch\`）
- WAL 模式，`synchronous=NORMAL`，单写连接串行
- 批写：200 条或 200ms 刷一次
- 保留：默认 30 天，每小时清理一次（`store.Config.Retention` 可配）

## API

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/ingest` | probe 投递事件（需 `Authorization: Bearer <token>`） |
| GET | `/api/sessions?limit=N` | session 列表，按 last_active 倒序 |
| GET | `/api/sessions/{id}` | session 详情 |
| GET | `/api/sessions/{id}/events?limit=N&offset=N` | session 事件流（ts 升序） |
| GET | `/api/stats` | 全局统计 |
| GET | `/ws` | WebSocket，实时推送新事件 |
| GET | `/ws/logs` | WebSocket，agent 日志实时流 |
| GET | `/api/logs?level=&limit=&since=` | agent 日志 |
| GET | `/api/doctor` | 诊断报告（hook 注册、probe 活性、DB 健康） |
| GET | `/healthz` | 健康检查 |
| **Serial** | | |
| GET | `/api/serial/status` | 串口状态（端口、连接、帧计数、暂停） |
| GET | `/api/serial/log?limit=&since=` | 串口通信日志；`since` 仅返回增量行 |
| POST | `/api/serial/send` | 下发 JSON 帧到设备（body 为原始 JSON） |
| POST | `/api/serial/disconnect` | 释放 COM 口（供刷固件用） |
| POST | `/api/serial/connect` | 恢复串口连接 |
| **自更新** | | |
| POST | `/api/update` | 上传新 agent 二进制并触发重启（需 token） |

## 卸载

**macOS / Linux 原生**：
```bash
# macOS
launchctl unload ~/Library/LaunchAgents/com.claudewatch.agent.plist
rm ~/Library/LaunchAgents/com.claudewatch.agent.plist
# Linux
systemctl --user --now disable claudewatch.service
rm ~/.config/systemd/user/claudewatch.service
sudo rm /usr/local/bin/claudewatch /usr/local/bin/claudewatch-probe
rm -rf ~/.config/claudewatch
# 从 ~/.claude/settings.json 删除 claudewatch-probe 相关条目（或恢复 .bak 备份）
```

**WSL**：
```bash
sudo rm /usr/local/bin/claudewatch-probe
rm -rf ~/.config/claudewatch
# 从 ~/.claude/settings.json 删除 claudewatch-probe 相关条目（手动或用 jq）
cp ~/.claude/settings.json.bak.* ~/.claude/settings.json  # 或从备份恢复
```

**Windows**：
```powershell
Get-Process claudewatch | Stop-Process -Force
Remove-Item "$env:APPDATA\ClaudeWatch" -Recurse
Remove-Item "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Startup\ClaudeWatch.lnk"
Remove-NetFirewallRule -DisplayName 'ClaudeWatch WSL'
```

## 已知限制

- **不支持 PreToolUse 同步拦截**：probe 是 fire-and-forget，只观测。若以后要加拦截能力，需要 probe 等待 agent 返回 decision，是另一套设计（注册 PreToolUse 会让 agent 的同步审批闸门锁死 Claude Code，故默认不注册）。
- **WSL2 NAT 模式**：probe 通过网关 IP 连 Windows agent。若 Windows 重启后 vSwitch IP 变化，install.sh 会重新计算并覆盖 `~/.config/claudewatch/agent.addr`，重跑一次即可。
- **Mirrored 网络模式未支持**：当前假设 NAT 模式。若你切到 mirrored，`127.0.0.1` 直通，可手动改 `agent.addr` 为 `127.0.0.1:7777`。
- **macOS 未签名**：`claudewatch-darwin` 默认 ad-hoc 未签名，首次运行可能被 Gatekeeper 拦截。开发分发可在「系统设置→隐私与安全性」允许，或用 `codesign` 自签。
- **前端视觉风格朴素**：MVP 功能完整，没做精细设计。Svelte 源码在 `frontend/src/`，可自行改造。
