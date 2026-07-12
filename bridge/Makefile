# Makefile — ClaudeWatch bridge 构建
#
# Go 工具链默认 ~/.g/versions/1.23.12，可用环境变量覆盖：
#   make GO=/usr/local/go/bin/go ...
#   GOROOT=/usr/local/go make release
#
# 前端必须先用 `frontend` 构建，产物 web/dist 由 agent 二进制 //go:embed 内嵌。

GOROOT ?= $(HOME)/.g/versions/1.23.12
GO     ?= $(GOROOT)/bin/go
export GOROOT := $(GOROOT)
export GOPATH := $(HOME)/go
NPM    ?= npm

.PHONY: all build agent probe frontend test clean dev-agent dev-probe \
        install-wsl deploy release \
        agent-windows agent-linux agent-darwin agent-darwin-arm64 \
        probe-windows probe-linux probe-darwin probe-darwin-arm64

all: frontend build

# 前端构建（输出到 web/dist，由 agent 二进制 embed）
frontend:
	cd frontend && $(NPM) install && $(NPM) run build

# 本机（当前 GOOS）构建
build: agent probe

# ---- agent：跨平台 ----
agent: frontend
	$(GO) build -o bin/claudewatch ./cmd/claudewatch

agent-windows: frontend
	GOOS=windows GOARCH=amd64 $(GO) build -ldflags "-H windowsgui" -o bin/claudewatch.exe ./cmd/claudewatch

agent-linux: frontend
	GOOS=linux GOARCH=amd64 $(GO) build -o bin/claudewatch-linux ./cmd/claudewatch

agent-darwin: frontend
	GOOS=darwin GOARCH=amd64 $(GO) build -o bin/claudewatch-darwin ./cmd/claudewatch

agent-darwin-arm64: frontend
	GOOS=darwin GOARCH=arm64 $(GO) build -o bin/claudewatch-darwin-arm64 ./cmd/claudewatch

# ---- probe：跨平台（跑在 Claude Code 所在平台）----
probe:
	$(GO) build -o bin/claudewatch-probe ./cmd/claudewatch-probe

probe-windows:
	GOOS=windows GOARCH=amd64 $(GO) build -o bin/claudewatch-probe.exe ./cmd/claudewatch-probe

probe-linux:
	GOOS=linux GOARCH=amd64 $(GO) build -o bin/claudewatch-probe ./cmd/claudewatch-probe

probe-darwin:
	GOOS=darwin GOARCH=amd64 $(GO) build -o bin/claudewatch-probe-darwin ./cmd/claudewatch-probe

probe-darwin-arm64:
	GOOS=darwin GOARCH=arm64 $(GO) build -o bin/claudewatch-probe-darwin-arm64 ./cmd/claudewatch-probe

# 发布包：前端 + 全平台 agent/probe 二进制（对方无需 Go/Node，跑对应安装脚本即可）
release: frontend agent-windows agent-linux agent-darwin agent-darwin-arm64 probe-windows probe-linux probe-darwin probe-darwin-arm64

test:
	$(GO) test ./...

clean:
	rm -rf bin/ web/dist/

# 开发：起 agent（前端改动需另跑 npm run dev）
dev-agent:
	$(GO) run ./cmd/claudewatch --addr :7777

# 开发：用模拟 stdin 测试 probe
dev-probe:
	@echo '{"hook_event_name":"PreToolUse","session_id":"test-session","cwd":"/tmp","tool_name":"Bash","tool_input":{"command":"ls"}}' | $(GO) run ./cmd/claudewatch-probe

# WSL 内安装 probe + 注册 hook
install-wsl:
	./install/install.sh

# 部署：交叉编译 Windows agent 后 POST 给运行中的 agent 触发自更新
deploy: agent-windows
	@GW=$$(cat ~/.config/claudewatch/agent.addr 2>/dev/null); \
	if [ -z "$$GW" ]; then echo "未找到 agent.addr，先跑 make install-wsl"; exit 1; fi; \
	TOKEN=$$(cat ~/.config/claudewatch/token 2>/dev/null); \
	echo "部署到 http://$$GW/api/update ..."; \
	curl -sS -X POST "http://$$GW/api/update" \
		-H "Authorization: Bearer $$TOKEN" \
		--data-binary @bin/claudewatch.exe \
		--max-time 30; \
	echo; echo "完成。agent 应已重启，浏览器刷新即可。"
