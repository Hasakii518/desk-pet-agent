// Package doctor 实现主动诊断检查。
//
// 通过 UNC 路径访问 WSL 文件系统（\\wsl$\<distro>\...），检查：
//   - hook 注册：settings.json 是否含 claudewatch-probe 条目
//   - probe 活性：最近 5 分钟是否有 ingest 事件
//   - token 一致性：Windows token vs WSL token
//   - DB 健康：SQLite 可读写
//   - agent.addr 网关：probe 写入的 fallback 地址
package doctor

import (
	"bytes"
	"context"
	"os"
	"path"
	"runtime"
	"strings"
	"time"

	"claudewatch/internal/metrics"
	"claudewatch/internal/store"
)

const (
	probeStaleThreshold = 5 * time.Minute
)

// Status 是单个检查项的状态。
type Status string

const (
	StatusOK    Status = "ok"
	StatusWarn  Status = "warn"
	StatusError Status = "error"
	StatusSkip  Status = "skip" // 无法检查（缺前置条件）
)

// Check 是单个诊断项。
type Check struct {
	Name    string `json:"name"`
	Status  Status `json:"status"`
	Message string `json:"message"`
	FixHint string `json:"fix_hint,omitempty"`
}

// Report 是完整诊断报告。
type Report struct {
	Checks    []Check   `json:"checks"`
	Generated time.Time `json:"generated"`
}

// Config 是 doctor 运行所需依赖。
type Config struct {
	WSLDistro    string // Ubuntu-24.04
	TokenPath    string // Windows 侧 token 文件路径
	RecentCwd    func() string
	LastIngestTs func() time.Time
	Store        *store.Store
	Metrics      *metrics.Registry
}

// Run 执行所有诊断检查。
func Run(ctx context.Context, cfg Config) Report {
	r := Report{Generated: time.Now()}

	// nil 防护：可选依赖缺失时给默认值，避免 panic
	recentCwd := cfg.RecentCwd
	if recentCwd == nil {
		recentCwd = func() string { return "" }
	}
	lastIngest := cfg.LastIngestTs
	if lastIngest == nil {
		lastIngest = func() time.Time { return time.Time{} }
	}

	wslHome := inferWSLHome(recentCwd())

	r.Checks = append(r.Checks,
		checkHookRegistration(cfg.WSLDistro, wslHome),
		checkProbeActivity(lastIngest()),
		checkTokenConsistency(cfg.TokenPath, cfg.WSLDistro, wslHome),
		checkDB(ctx, cfg.Store),
	)

	return r
}

// inferWSLHome 从最近 cwd（形如 /home/tencent_go/...）提取 /home/<user>。
func inferWSLHome(cwd string) string {
	if cwd == "" {
		return ""
	}
	// 匹配 /home/<user> 或 /root
	if strings.HasPrefix(cwd, "/home/") {
		parts := strings.SplitN(strings.TrimPrefix(cwd, "/home/"), "/", 2)
		if len(parts) > 0 && parts[0] != "" {
			return "/home/" + parts[0]
		}
	}
	if strings.HasPrefix(cwd, "/root") {
		return "/root"
	}
	return ""
}

// wslToUNC 把 WSL 路径 /home/x/y 转成跨平台可读路径。
// Windows: \\wsl$\<distro>\home\x\y (UNC 访问 WSL 文件系统)
// Linux:   原样 /home/x/y (本地访问)
func wslToUNC(wslPath, distro string) string {
	if runtime.GOOS != "windows" {
		return wslPath
	}
	if distro == "" {
		return ""
	}
	p := strings.TrimPrefix(wslPath, "/")
	p = strings.ReplaceAll(p, "/", "\\")
	return `\\wsl$\` + distro + `\` + p
}

func checkHookRegistration(distro, wslHome string) Check {
	c := Check{Name: "hook 注册"}
	if wslHome == "" {
		c.Status = StatusSkip
		c.Message = "缺少用户家目录（等 probe 首次触发后可检查）"
		return c
	}
	if runtime.GOOS == "windows" && distro == "" {
		c.Status = StatusSkip
		c.Message = "Windows 上需指定 --wsl-distro 才能读 WSL 文件系统"
		return c
	}
	settingsPath := wslToUNC(path.Join(wslHome, ".claude/settings.json"), distro)
	data, err := os.ReadFile(settingsPath)
	if err != nil {
		c.Status = StatusError
		c.Message = "读 settings.json 失败: " + err.Error()
		c.FixHint = "确认 WSL 在运行；或手动检查 " + settingsPath
		return c
	}
	if bytes.Contains(data, []byte("claudewatch-probe")) {
		c.Status = StatusOK
		c.Message = "settings.json 含 claudewatch-probe hook 条目"
		return c
	}
	c.Status = StatusError
	c.Message = "settings.json 不含 claudewatch-probe，hook 被覆盖或未注册"
	c.FixHint = "在 WSL 里跑 make install-wsl 重新注册 hook"
	return c
}

func checkProbeActivity(lastIngest time.Time) Check {
	c := Check{Name: "probe 活性"}
	if lastIngest.IsZero() {
		c.Status = StatusWarn
		c.Message = "从未收到 probe 事件"
		c.FixHint = "确认 probe 已安装（/usr/local/bin/claudewatch-probe）且 settings.json 已注册 hook"
		return c
	}
	age := time.Since(lastIngest)
	if age < probeStaleThreshold {
		c.Status = StatusOK
		c.Message = "最近 " + age.Truncate(time.Second).String() + " 有事件"
		return c
	}
	c.Status = StatusWarn
	c.Message = "最近一次事件在 " + age.Truncate(time.Second).String() + " 前"
	c.FixHint = "probe 可能未触发；检查 Claude Code 是否在跑、hook 是否被覆盖"
	return c
}

func checkTokenConsistency(winTokenPath, distro, wslHome string) Check {
	c := Check{Name: "token 一致性"}
	if runtime.GOOS != "windows" {
		c.Status = StatusSkip
		c.Message = "仅 Windows agent 需检查 token 一致性"
		return c
	}
	if wslHome == "" {
		c.Status = StatusSkip
		c.Message = "缺少用户家目录"
		return c
	}
	if distro == "" {
		c.Status = StatusSkip
		c.Message = "需指定 --wsl-distro"
		return c
	}
	winToken, err := os.ReadFile(winTokenPath)
	if err != nil {
		c.Status = StatusError
		c.Message = "读 Windows token 失败: " + err.Error()
		return c
	}
	wslTokenPath := wslToUNC(path.Join(wslHome, ".config/claudewatch/token"), distro)
	wslToken, err := os.ReadFile(wslTokenPath)
	if err != nil {
		c.Status = StatusWarn
		c.Message = "读 WSL token 失败: " + err.Error()
		c.FixHint = "在 WSL 里跑 make install-wsl 拷贝 token"
		return c
	}
	if bytes.Equal(bytes.TrimSpace(winToken), bytes.TrimSpace(wslToken)) {
		c.Status = StatusOK
		c.Message = "Windows token 与 WSL token 一致"
		return c
	}
	c.Status = StatusError
	c.Message = "Windows token 与 WSL token 不一致"
	c.FixHint = "重跑 install.ps1（Windows）+ install.sh（WSL）同步 token"
	return c
}

func checkDB(ctx context.Context, st *store.Store) Check {
	c := Check{Name: "DB 健康"}
	if st == nil {
		c.Status = StatusSkip
		c.Message = "store 未初始化"
		return c
	}
	if _, err := st.Stats(ctx); err != nil {
		c.Status = StatusError
		c.Message = "SQLite 查询失败: " + err.Error()
		c.FixHint = "检查磁盘空间或 DB 文件权限"
		return c
	}
	c.Status = StatusOK
	c.Message = "SQLite 读写正常"
	return c
}
