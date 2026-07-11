#!/usr/bin/env bash
# install.sh — WSL 内安装 claudewatch probe + 注册全局 hook
#
# 用法: ./install.sh
# 假设 agent 已在 Windows 侧通过 install.ps1 安装并运行。
set -euo pipefail

PROBE_SRC="$(cd "$(dirname "$0")"/.. && pwd)/bin/claudewatch-probe"
PROBE_DST="${1:-/usr/local/bin/claudewatch-probe}"
CFG_DIR="$HOME/.config/claudewatch"
# Claude Code 配置目录: 优先 CLAUDE_CONFIG_DIR (tme-claude 用 ~/.tme-claude)，否则探测
if [[ -n "${CLAUDE_CONFIG_DIR:-}" ]]; then
  CLAUDE_DIR="$CLAUDE_CONFIG_DIR"
elif [[ -d "$HOME/.tme-claude" ]]; then
  CLAUDE_DIR="$HOME/.tme-claude"
else
  CLAUDE_DIR="$HOME/.claude"
fi
SETTINGS="$CLAUDE_DIR/settings.json"
# 纯观测模式: 不注册 PreToolUse。该 hook 在本 agent 里是同步审批闸门，
# 每次工具调用都阻塞等 UI 决策、30s 超时默认 deny，会锁死 Claude Code。
# 如需审批闸门，手动把 PreToolUse 加进下面数组并全程开着浏览器审批。
HOOK_TYPES=(PostToolUse UserPromptSubmit Stop SubagentStop Notification SessionStart SessionEnd PreCompact)

# 守卫: 不要用 sudo 跑本脚本。写 /usr/local/bin 时内部会按需 sudo。
# sudo 会让 $HOME 变成 /root，导致配置和 settings.json 写错位置。
if [[ $EUID -eq 0 ]]; then
  echo "错误: 请勿用 sudo 运行本脚本。" >&2
  echo "脚本写 /usr/local/bin 时会内部调用 sudo，其余操作必须在普通用户 HOME 下进行。" >&2
  echo "正确用法: ./install.sh" >&2
  exit 1
fi

echo "=== ClaudeWatch probe 安装 ==="

# 1. 部署 probe
if [[ ! -x "$PROBE_SRC" ]]; then
  echo "错误: 找不到 probe 二进制 $PROBE_SRC"
  echo "请先在项目根目录执行: make probe-linux"
  exit 1
fi
if [[ -w "$(dirname "$PROBE_DST")" ]]; then
  cp "$PROBE_SRC" "$PROBE_DST"
  chmod +x "$PROBE_DST"
else
  echo "需要 sudo 写入 $PROBE_DST"
  sudo cp "$PROBE_SRC" "$PROBE_DST"
  sudo chmod +x "$PROBE_DST"
fi
echo "✓ probe 已安装到 $PROBE_DST"

# 2. 写 agent 地址 (WSL 网关 IP)
mkdir -p "$CFG_DIR"
GW=$(ip route show default | awk '{print $3; exit}')
if [[ -z "$GW" ]]; then
  echo "错误: 无法获取默认网关 IP"
  exit 1
fi
echo "${GW}:7777" > "$CFG_DIR/agent.addr"
echo "✓ agent 地址: ${GW}:7777 → $CFG_DIR/agent.addr"

# 3. token (从 Windows 侧拷贝; 由 install.ps1 生成)
TOKEN_SRC="/mnt/c/Users/armstrong/AppData/Roaming/ClaudeWatch/token"
if [[ -f "$TOKEN_SRC" ]]; then
  cp "$TOKEN_SRC" "$CFG_DIR/token"
  chmod 600 "$CFG_DIR/token"
  echo "✓ token 已从 Windows 拷贝"
else
  echo "⚠ 未找到 Windows 侧 token ($TOKEN_SRC)"
  echo "  请先在 Windows 运行 install.ps1，或手写 $CFG_DIR/token"
fi

# 4. 注册全局 hook (增量合并，不覆盖已有)
mkdir -p "$CLAUDE_DIR"
if [[ ! -f "$SETTINGS" ]]; then
  echo '{}' > "$SETTINGS"
fi
cp "$SETTINGS" "$SETTINGS.bak.$(date +%s)"
echo "✓ settings.json 已备份"

python3 - "$SETTINGS" "$PROBE_DST" "${HOOK_TYPES[@]}" <<'PY'
import json, sys, os
settings_path, probe, *hook_types = sys.argv[1:]
with open(settings_path, 'r') as f:
    cfg = json.load(f)

hooks = cfg.setdefault('hooks', {})
changed = False
for ht in hook_types:
    arr = hooks.setdefault(ht, [])
    # 检查是否已存在 claudewatch-probe 条目
    has = any(
        any('claudewatch-probe' in h.get('command', '') for h in entry.get('hooks', []))
        for entry in arr
    )
    if not has:
        arr.append({
            "matcher": "*",
            "hooks": [{"type": "command", "command": f"{probe} {ht}"}]
        })
        changed = True
        print(f"  + {ht}: 已添加")

if changed:
    with open(settings_path, 'w') as f:
        json.dump(cfg, f, indent=2, ensure_ascii=False)
    print("✓ settings.json 已更新")
else:
    print("✓ settings.json 已是最新（claudewatch hook 已存在）")
PY

echo
echo "=== 完成 ==="
echo "Probe: $PROBE_DST"
echo "Agent: $(cat "$CFG_DIR/agent.addr")"
echo "配置:  $CFG_DIR/"
echo
echo "下一步: 在 Windows 侧运行 install.ps1 安装 agent"
