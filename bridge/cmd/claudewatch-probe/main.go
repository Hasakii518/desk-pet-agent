// claudewatch-probe: Claude Code hook 探针。
//
// 由 Claude Code 在 hook 中调用，从 stdin 读取 hook JSON payload，
// 加上时间戳后 POST 给 agent。Stop 事件时本地读 transcript 提取
// title/recap 一起发给 agent。任何错误都静默退出，绝不阻塞 Claude Code。
package main

import (
	"bytes"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"strconv"
	"time"

	"claudewatch/internal/transcript"
)
var _ = json.Marshal

func main() {
	body, err := io.ReadAll(os.Stdin)
	if err != nil || len(body) == 0 {
		return
	}

	var m map[string]any
	if err := json.Unmarshal(body, &m); err != nil {
		return
	}
	m["ts"] = time.Now().UnixMilli()

	// SessionStart/Stop 事件：本地读 transcript 提取 title/recap，塞进 payload 让 agent 落库。
	// SessionStart 时 transcript 可能还没 title，Stop 时最完整。
	switch m["hook_event_name"] {
	case "SessionStart", "Stop":
		if tp, ok := m["transcript_path"].(string); ok && tp != "" {
			if res, err := transcript.Parse(tp); err == nil {
				if res.Title != "" {
					m["cw_title"] = res.Title
				}
				if res.Recap != "" {
					m["cw_recap"] = res.Recap
				}
			}
		}
	}

	out, err := json.Marshal(m)
	if err != nil {
		return
	}

	addr := loadAddr()
	token := loadToken()

	// PreToolUse 走 /permission 同步等决策；其他 hook 走 /ingest 立即返回
	hookEvent, _ := m["hook_event_name"].(string)
	endpoint := "/ingest"
	timeout := 2 * time.Second
	if hookEvent == "PreToolUse" {
		endpoint = "/permission"
		timeout = 35 * time.Second // agent 30s 超时 + 余量
	}

	req, err := http.NewRequest("POST", "http://"+addr+endpoint, bytes.NewReader(out))
	if err != nil {
		return
	}
	req.Header.Set("Content-Type", "application/json")
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}

	client := &http.Client{Timeout: timeout}
	resp, err := client.Do(req)
	if err != nil {
		// PreToolUse 网络失败 → deny（安全默认）
		if hookEvent == "PreToolUse" {
			os.Exit(2)
		}
		return
	}
	defer resp.Body.Close()

	if hookEvent == "PreToolUse" {
		// 解析 {decision: allow|deny}，设退出码
		var result struct {
			Decision string `json:"decision"`
		}
		json.NewDecoder(resp.Body).Decode(&result)
		if result.Decision == "deny" {
			os.Exit(2)
		}
		os.Exit(0)
	}
	io.Copy(io.Discard, resp.Body)
}

// loadAddr 优先级: CLAUDEWATCH_ADDR 环境变量 > 动态网关 IP > ~/.config/claudewatch/agent.addr > 127.0.0.1:7777
// 动态网关：每次启动 ip route show default 算，适配 Windows 重启后 vSwitch IP 变化
func loadAddr() string {
	if a := os.Getenv("CLAUDEWATCH_ADDR"); a != "" {
		return a
	}
	if gw := discoverGateway(); gw != "" {
		return gw + ":7777"
	}
	if b, err := os.ReadFile(os.Getenv("HOME") + "/.config/claudewatch/agent.addr"); err == nil {
		s := string(bytes.TrimSpace(b))
		if s != "" {
			return s
		}
	}
	return "127.0.0.1:7777"
}

// discoverGateway 从 /proc/net/route 或 ip route 解析默认网关 IP
func discoverGateway() string {
	// 优先 /proc/net/route（无外部依赖）
	data, err := os.ReadFile("/proc/net/route")
	if err == nil {
		for _, line := range bytes.Split(data, []byte("\n"))[1:] {
			fields := bytes.Fields(line)
			if len(fields) >= 3 && bytes.Equal(fields[1], []byte("00000000")) {
				// 网关在 field[2]，小端 hex
				hex := fields[2]
				if len(hex) == 8 {
					ip := hexToIP(string(hex))
					if ip != "" && ip != "0.0.0.0" {
						return ip
					}
				}
			}
		}
	}
	// fallback: ip route（需要 ip 命令）
	out, err := exec.Command("ip", "route", "show", "default").Output()
	if err == nil {
		for _, field := range bytes.Fields(out) {
			s := string(field)
			if net.ParseIP(s) != nil {
				return s
			}
		}
	}
	return ""
}

func hexToIP(hex string) string {
	// /proc/net/route 网关是小端 hex，如 "0100a8c0" = 192.168.0.1
	if len(hex) != 8 {
		return ""
	}
	b := make([]byte, 4)
	for i := 0; i < 4; i++ {
		v, err := strconv.ParseUint(hex[i*2:i*2+2], 16, 8)
		if err != nil {
			return ""
		}
		b[3-i] = byte(v)
	}
	return net.IP(b).String()
}

// loadToken 优先级: CLAUDEWATCH_TOKEN 环境变量 > ~/.config/claudewatch/token
func loadToken() string {
	if t := os.Getenv("CLAUDEWATCH_TOKEN"); t != "" {
		return t
	}
	if b, err := os.ReadFile(os.Getenv("HOME") + "/.config/claudewatch/token"); err == nil {
		return string(bytes.TrimSpace(b))
	}
	return ""
}
