# ClaudeWatch

跨平台 Claude Code hook 观测代理。probe 捕获 Claude Code 的所有 hook 事件，按 session 归类写入本地 SQLite，浏览器实时查看。

- **probe**：Claude Code hook 触发，单行 JSON 通过 HTTP POST 给 agent，失败静默不阻塞
- **agent**：HTTP+WebSocket server，批写 SQLite（WAL），实时推送事件给 UI
- **interface**：Svelte 前端，嵌入 agent 二进制，浏览器打开即用

## 架构

```
┌────────── WSL ──────────┐         ┌──────── Windows 本体 ────────┐
│ Claude Code             │         │  claudewatch.exe (agent+UI)  │
│   └─ hook → probe       │  HTTP   │   ├─ :7777 HTTP+WS           │
│        │                │ ───────▶│   ├─ SQLite (WAL, batched)   │
│        └─ /usr/local/   │  网关IP │   ├─ static frontend         │
│            bin/         │         │   └─ 系统托盘 / 开机自启      │
│            claudewatch- │         │                               │
│            probe        │         │  浏览器: http://127.0.0.1:7777│
└─────────────────────────┘         └───────────────────────────────┘
```

WSL2 NAT 模式下，probe 通过默认网关 IP（`192.168.160.1`）连 Windows agent。agent 绑 `0.0.0.0:7777` + 防火墙规则只允许 WSL 子网入站。

## 前置依赖

**WSL 内**：
- Go 1.23+（项目内 `env.sh` 切换，不影响全局）
- Node.js 18+（构建前端用）
- `jq` 或 `python3`（install.sh 合并 settings.json 用）

**Windows 本体**：
- 无需预装 Go/Node（agent 是单二进制，前端已嵌入）
- Windows 10/11，WSL2

## 安装

### 1. WSL 内构建

```bash
cd ~/projects/personal/claudewatch
source ./env.sh          # 激活项目内 Go 1.23.12（可选，Makefile 已硬编码绝对路径）
make all                 # 构建 Linux agent + probe + 前端
make agent-windows       # 交叉编译 Windows agent (.exe)
```

产物在 `bin/`：`claudewatch`、`claudewatch-probe`、`claudewatch.exe`。

### 2. Windows 侧安装 agent

普通用户 PowerShell：

```powershell
powershell -ExecutionPolicy Bypass -File \\wsl$\Ubuntu-24.04\home\<user>\projects\personal\claudewatch\install\install.ps1
```

脚本会：
- 部署 `claudewatch.exe` 到 `%APPDATA%\ClaudeWatch\`
- 生成 token（crypto/rand 32 字节 hex）
- 添加防火墙规则（**需要管理员**；非管理员会跳过并打印命令让你事后补）
- 添加启动文件夹快捷方式（开机自启）
- 同步 token 到 WSL `~/.config/claudewatch/token`
- 立即启动 agent

**防火墙**（若上一步跳过）：以管理员身份打开 PowerShell，执行脚本打印的那行：
```powershell
New-NetFirewallRule -DisplayName 'ClaudeWatch WSL' -Direction Inbound -LocalPort 7777 -Protocol TCP -Action Allow -RemoteAddress 192.168.160.0/20 -Profile Any
```

### 3. WSL 侧安装 probe

```bash
cd ~/projects/personal/claudewatch
./install/install.sh
```

脚本会：
- 部署 probe 到 `/usr/local/bin/claudewatch-probe`（内部按需 sudo）
- 写 `~/.config/claudewatch/agent.addr`（自动从 `ip route` 取网关 IP）
- 拷贝 token 到 `~/.config/claudewatch/token`
- **增量合并** `~/.claude/settings.json`：为 9 类 hook 各追加一条 `matcher:"*"` 指向 probe，不破坏已有 hook

> ⚠️ **不要用 `sudo ./install.sh`**。脚本只在写 `/usr/local/bin` 时内部调用 sudo，其余操作必须在普通用户 HOME 下。sudo 会让 `$HOME` 变成 `/root`，导致配置写错位置。

### 4. 打开界面

Windows 浏览器访问 `http://127.0.0.1:7777`。左侧 session 列表，右侧事件时间线。绿点表示 WebSocket 实时连接。

之后在 WSL 里正常用 Claude Code，所有工具调用、prompt 提交、session 生命周期事件都会实时显示。

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
│   ├── install.sh           # WSL 侧
│   └── install.ps1          # Windows 侧
├── env.sh                   # 项目内激活 Go 1.23.12
├── .go-version
├── go.mod                   # go 1.23.0 + toolchain go1.23.12
└── Makefile
```

## 数据

- DB 路径：`%APPDATA%\ClaudeWatch\events.db`（Windows agent 侧）
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
| GET | `/healthz` | 健康检查 |

## 卸载

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

- **不支持 PreToolUse 同步拦截**：probe 是 fire-and-forget，只观测。若以后要加拦截能力，需要 probe 等待 agent 返回 decision，是另一套设计。
- **WSL2 NAT 模式**：probe 通过网关 IP 连 Windows agent。若 Windows 重启后 vSwitch IP 变化，install.sh 会重新计算并覆盖 `~/.config/claudewatch/agent.addr`，重跑一次即可。
- **Mirrored 网络模式未支持**：当前假设 NAT 模式。若你切到 mirrored，`127.0.0.1` 直通，可手动改 `agent.addr` 为 `127.0.0.1:7777`。
- **前端视觉风格朴素**：MVP 功能完整，没做精细设计。Svelte 源码在 `frontend/src/`，可自行改造。
