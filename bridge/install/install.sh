#!/usr/bin/env bash
# install.sh — ClaudeWatch bridge 安装（macOS / Linux / WSL）
#
# 自动探测运行环境：
#   Darwin            → macOS 原生：安装 agent(自启 launchd) + probe + Claude Code hooks
#   Linux + microsoft → WSL：仅安装 probe（agent 跑在 Windows 宿主）+ hooks
#   Linux (其它)      → Linux 原生：安装 agent(自启 systemd --user) + probe + hooks
#
# 用法: ./install.sh
set -euo pipefail

# optional: -c/--claude-config-dir <path> to specify where Claude Code's settings.json lives
CLAUDE_CONFIG_DIR_ARG=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    -c|--claude-config-dir) CLAUDE_CONFIG_DIR_ARG="$2"; shift 2 ;;
    -*) echo "unknown option: $1" >&2; exit 1 ;;
    *) shift ;;
  esac
done

OS="$(uname -s)"
IS_WSL=0
if [[ "$OS" == "Linux" ]] && grep -qi microsoft /proc/version 2>/dev/null; then
  IS_WSL=1
fi

case "$OS" in
  Darwin)
    OS_LOWER=darwin
    # Apple Silicon 优先用 arm64 构建，否则 amd64（Rosetta 也可跑）
    if [[ "$(uname -m)" == "arm64" ]]; then
      AGENT_SUFFIX="darwin-arm64"; PROBE_SUFFIX="darwin-arm64"
    else
      AGENT_SUFFIX="darwin"; PROBE_SUFFIX="darwin"
    fi
    ;;
  Linux)  OS_LOWER=linux;  AGENT_SUFFIX=linux;  PROBE_SUFFIX=linux ;;
  *)      OS_LOWER="";     AGENT_SUFFIX="";     PROBE_SUFFIX="" ;;
esac

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# release 包里二进制带平台后缀；退化为无后缀（本机直接 make 构建的情况）
pick() {
  local f
  for f in "$@"; do [[ -f "$f" ]] && { echo "$f"; return; } done
  echo "$1"
}
AGENT_SRC="$(pick "$REPO_ROOT/bin/claudewatch-$AGENT_SUFFIX" "$REPO_ROOT/bin/claudewatch")"
PROBE_SRC="$(pick "$REPO_ROOT/bin/claudewatch-probe-$PROBE_SUFFIX" "$REPO_ROOT/bin/claudewatch-probe")"
PROBE_DST="/usr/local/bin/claudewatch-probe"
AGENT_DST="/usr/local/bin/claudewatch"
CFG_DIR="$HOME/.config/claudewatch"
PORT=7777

# Claude Code 配置目录：显式 -c > CLAUDE_CONFIG_DIR 环境变量 > .tme-claude > .claude
if [[ -n "$CLAUDE_CONFIG_DIR_ARG" ]]; then
  CLAUDE_DIR="$CLAUDE_CONFIG_DIR_ARG"
elif [[ -n "${CLAUDE_CONFIG_DIR:-}" ]]; then
  CLAUDE_DIR="$CLAUDE_CONFIG_DIR"
elif [[ -d "$HOME/.tme-claude" ]]; then
  CLAUDE_DIR="$HOME/.tme-claude"
else
  CLAUDE_DIR="$HOME/.claude"
fi

# 纯观测 hook（默认不注册 PreToolUse，避免同步审批闸门锁死 Claude Code）
HOOK_TYPES=(PostToolUse UserPromptSubmit Stop SubagentStop Notification SessionStart SessionEnd PreCompact)

echo "=== ClaudeWatch 安装 ==="
echo "环境: ${OS}$([ "$IS_WSL" = 1 ] && echo ' (WSL)')"

# 需要写系统目录时内部按需 sudo
install_bin() {
  local src="$1" dst="$2"
  if [[ ! -f "$src" ]]; then
    echo "错误: 找不到 $src，请先运行 make release" >&2
    exit 1
  fi
  if [[ -w "$(dirname "$dst")" ]]; then
    cp "$src" "$dst"
  else
    echo "需要 sudo 写入 $(dirname "$dst")"
    sudo cp "$src" "$dst"
  fi
  sudo chmod 755 "$dst" 2>/dev/null || chmod 755 "$dst" 2>/dev/null || true
  echo "✓ 部署: $dst"
}

ensure_token() {
  if [[ -f "$CFG_DIR/token" ]]; then
    echo "✓ token 已存在"
    return
  fi
  local tok
  tok="$(head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n')"
  mkdir -p "$CFG_DIR"
  printf '%s' "$tok" > "$CFG_DIR/token"
  chmod 600 "$CFG_DIR/token"
  echo "✓ 生成 token: $CFG_DIR/token"
}

write_config() {
  mkdir -p "$CFG_DIR"
  if [[ -f "$CFG_DIR/config.json" ]]; then
    echo "✓ config.json 已存在: $CFG_DIR/config.json"
    return
  fi
  cat > "$CFG_DIR/config.json" <<JSON
{
  "addr": ":$PORT",
  "serialPort": "auto"
}
JSON
  echo "✓ config.json: $CFG_DIR/config.json"
}

register_hooks() {
  mkdir -p "$CLAUDE_DIR"
  local settings="$CLAUDE_DIR/settings.json"
  [[ -f "$settings" ]] || echo '{}' > "$settings"
  cp "$settings" "$settings.bak.$(date +%s)"
  echo "✓ settings.json 已备份"
  python3 - "$settings" "$PROBE_DST" "${HOOK_TYPES[@]}" <<'PY'
import json, sys
settings_path, probe, *hook_types = sys.argv[1:]
with open(settings_path) as f:
    cfg = json.load(f)
hooks = cfg.setdefault('hooks', {})
changed = False
for ht in hook_types:
    arr = hooks.setdefault(ht, [])
    has = any(
        any('claudewatch-probe' in h.get('command', '') for h in entry.get('hooks', []))
        for entry in arr
    )
    if not has:
        arr.append({"matcher": "*", "hooks": [{"type": "command", "command": f"{probe} {ht}"}]})
        changed = True
        print(f"  + {ht}: 已添加")
if changed:
    with open(settings_path, 'w') as f:
        json.dump(cfg, f, indent=2, ensure_ascii=False)
    print("✓ settings.json 已更新")
else:
    print("✓ settings.json 已是最新（claudewatch hook 已存在）")
PY
}

install_launchd() {
  local plist="$HOME/Library/LaunchAgents/com.claudewatch.agent.plist"
  local cfg="$CFG_DIR/config.json"
  mkdir -p "$(dirname "$plist")"
  cat > "$plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>com.claudewatch.agent</string>
  <key>ProgramArguments</key>
  <array>
    <string>/usr/local/bin/claudewatch</string>
    <string>--config</string>
    <string>$cfg</string>
  </array>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>StandardOutPath</key>
  <string>$CFG_DIR/agent.out</string>
  <key>StandardErrorPath</key>
  <string>$CFG_DIR/agent.err</string>
</dict>
</plist>
PLIST
  launchctl unload "$plist" 2>/dev/null || true
  launchctl load "$plist"
  echo "✓ launchd 自启: $plist"
}

install_systemd() {
  if ! command -v systemctl >/dev/null 2>&1 || [[ -z "${XDG_RUNTIME_DIR:-}" ]]; then
    echo "⚠ systemd --user 不可用，跳过自启；可手动运行: $AGENT_DST --config $CFG_DIR/config.json &"
    return
  fi
  local unit="$HOME/.config/systemd/user/claudewatch.service"
  mkdir -p "$(dirname "$unit")"
  cat > "$unit" <<UNIT
[Unit]
Description=ClaudeWatch agent
After=network.target

[Service]
ExecStart=/usr/local/bin/claudewatch --config $CFG_DIR/config.json
Restart=on-failure

[Install]
WantedBy=default.target
UNIT
  systemctl --user daemon-reload
  systemctl --user enable --now claudewatch.service
  echo "✓ systemd 自启: $unit"
}

wsl_install_token() {
  # 从 Windows 宿主拷贝 token（动态取 Windows 用户名，不写死）
  local win_user win_token
  win_user="$(cmd.exe /c 'echo %USERNAME%' 2>/dev/null | tr -d '\r')"
  if [[ -z "$win_user" ]]; then
    echo "⚠ 无法探测 Windows 用户名，跳过 token 拷贝"
    return
  fi
  win_token="/mnt/c/Users/$win_user/AppData/Roaming/ClaudeWatch/token"
  if [[ -f "$win_token" ]]; then
    mkdir -p "$CFG_DIR"
    cp "$win_token" "$CFG_DIR/token"
    chmod 600 "$CFG_DIR/token"
    echo "✓ token 已从 Windows 拷贝 ($win_token)"
  else
    echo "⚠ 未找到 Windows 侧 token ($win_token)"
    echo "  请先在 Windows 运行 install.ps1，或手写 $CFG_DIR/token"
  fi
}

wsl_write_addr() {
  mkdir -p "$CFG_DIR"
  local gw
  gw="$(ip route show default | awk '{print $3; exit}')"
  if [[ -z "$gw" ]]; then
    echo "错误: 无法获取默认网关 IP" >&2
    exit 1
  fi
  echo "${gw}:${PORT}" > "$CFG_DIR/agent.addr"
  echo "✓ agent 地址: ${gw}:${PORT} → $CFG_DIR/agent.addr"
}

# ---- 分发 ----
if [[ "$IS_WSL" = 1 ]]; then
  install_bin "$PROBE_SRC" "$PROBE_DST"
  wsl_install_token
  wsl_write_addr
  register_hooks
  echo
  echo "=== 完成（WSL）==="
  echo "Probe:   $PROBE_DST"
  echo "Agent:   $(cat "$CFG_DIR/agent.addr" 2>/dev/null)"
  echo "配置:    $CFG_DIR/"
  echo "下一步: 在 Windows 侧运行 install.ps1 安装并启动 agent"
  exit 0
fi

# ---- 原生（macOS / Linux）：agent + probe ----
install_bin "$AGENT_SRC" "$AGENT_DST"
install_bin "$PROBE_SRC" "$PROBE_DST"
ensure_token
write_config
if [[ "$OS" == "Darwin" ]]; then
  install_launchd
else
  install_systemd
fi
register_hooks
echo
echo "=== 完成（原生 $OS）==="
echo "Agent:   $AGENT_DST (自启)"
echo "Probe:   $PROBE_DST"
echo "配置:    $CFG_DIR/"
echo "UI:      http://127.0.0.1:$PORT"
