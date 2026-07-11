# Makefile: 不依赖 source env.sh，直接用绝对路径调用 Go 1.23.12
GOROOT23 := $(HOME)/.g/versions/1.23.12
GO := $(GOROOT23)/bin/go
export GOROOT := $(GOROOT23)
export GOPATH := $(HOME)/go
NPM := npm

.PHONY: all build agent probe agent-windows probe-linux frontend test clean dev-agent dev-probe install-wsl deploy

all: frontend build

# 前端构建 (输出到 web/dist，由 agent 二进制 embed)
frontend:
	cd frontend && $(NPM) install && $(NPM) run build

# 本机构建 (Linux)
build: agent probe

agent: frontend
	$(GO) build -o bin/claudewatch ./cmd/claudewatch

probe:
	$(GO) build -o bin/claudewatch-probe ./cmd/claudewatch-probe

# 交叉编译 Windows agent (纯 Go 无 CGO)。前端必须先构建以供 embed。
# -H windowsgui: GUI subsystem，启动不弹 console 窗口
agent-windows: frontend
	GOOS=windows GOARCH=amd64 $(GO) build -ldflags "-H windowsgui" -o bin/claudewatch.exe ./cmd/claudewatch

# 交叉编译 Linux probe (跑在 WSL 内)
probe-linux:
	GOOS=linux GOARCH=amd64 $(GO) build -o bin/claudewatch-probe ./cmd/claudewatch-probe

test:
	$(GO) test ./...

clean:
	rm -rf bin/ web/dist/

# 开发: 起 agent (前端改动需另跑 npm run dev)
dev-agent:
	$(GO) run ./cmd/claudewatch --addr :7777

# 开发: 用模拟 stdin 测试 probe
dev-probe:
	@echo '{"hook_event_name":"PreToolUse","session_id":"test-session","cwd":"/tmp","tool_name":"Bash","tool_input":{"command":"ls"}}' | $(GO) run ./cmd/claudewatch-probe

# WSL 内安装 probe + 注册 hook
install-wsl:
	./install/install.sh

# 部署: 交叉编译 Windows agent 后 POST 给运行中的 agent 触发自更新
deploy: agent-windows
	@GW=$$(cat ~/.config/claudewatch/agent.addr 2>/dev/null) || { echo "未找到 agent.addr，先跑 make install-wsl"; exit 1; }; \
	TOKEN=$$(cat ~/.config/claudewatch/token 2>/dev/null); \
	echo "部署到 http://$$GW/api/update ..."; \
	curl -sS -X POST "http://$$GW/api/update" \
		-H "Authorization: Bearer $$TOKEN" \
		--data-binary @bin/claudewatch.exe \
		--max-time 30; \
	echo; echo "完成。agent 应已重启，浏览器刷新即可。"
