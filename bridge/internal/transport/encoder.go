// encoder.go —— hook 事件 → ESP32 通信帧的状态机。
//
// probe 落库的每个 protocol.Event 经此翻译成 1~2 条 Frame 下发：
//   - 每条 hook 至少产一条 notify（桌宠状态切换 + 可选弹窗）
//   - SessionStart / Stop / SessionEnd 额外产一条 session 快照（带 name/lastReply，
//     设备按 sid 整条覆盖内存）
//
// state 取值对齐固件 pet_state_t（小写）。disconnected 不在此产生（设备本地推导）。
// 源目前固定 claude-code；ev.Source 非空时以它为准。
package transport

import (
	"encoding/json"
	"fmt"
	"sync"

	"claudewatch/internal/protocol"
)

const srcDefault = "claude-code"

// lastToolBrief 缓存每个 session 最近一次 PreToolUse 的命令摘要，
// 供 PostToolUse 弹窗回显"哪个命令做完了"。
var lastToolBrief sync.Map // sessionID → string

// 帧文本截断阈值（串口帧友好）。
const (
	maxText = 40   // 单帧 <250B，USB 64B 包 ≤4 包，零碎片化
	maxName = 48
)

// FramesFor 把一个 hook 事件翻译成下行帧。返回 nil 表示该事件不下发。
func FramesFor(ev protocol.Event) []Frame {
	state, popup := mapHook(ev)
	if state == "" {
		return nil
	}
	src := ev.Source
	if src == "" {
		src = srcDefault
	}

	var frames []Frame

	// notify：状态切换 + 弹窗（高频，每条 hook 必发）
	nf := Frame{
		T:     "notify",
		Src:   src,
		SID:   ev.SessionID,
		Name:  ev.Title,
		State: state,
		Title: popup.title,
		Text:  popup.text,
		TS:    ev.Ts,
	}
	frames = append(frames, nf)

	// session 快照：仅生命周期事件下发（低频），避免高频触发
	// ui_nav_rebuild_sessions 删建全部会话页导致 UI 闪/丢帧。
	if ev.HookEventName == "SessionStart" || ev.HookEventName == "Stop" || ev.HookEventName == "SessionEnd" {
		frames = append(frames, sessionFrame(ev, src, state))
	}
	return frames
}

// popup 是 notify 帧附带的弹窗内容。
type popup struct{ title, text string }

// mapHook 把 hook 类型映射到桌宠状态 + 弹窗。state 空表示不下发。
// PreToolUse 弹窗提取 tool_input 中的实际命令；PostToolUse 仅简短确认。
func mapHook(ev protocol.Event) (string, popup) {
	switch ev.HookEventName {
	case "SessionStart":
		return "thinking", popup{title: "Session", text: "Agent started working"}
	case "UserPromptSubmit":
		return "typing", popup{} // 用户自己的输入不弹窗
	case "PreToolUse":
		brief := toolBrief(ev.ToolName, ev.ToolInput)
		lastToolBrief.Store(ev.SessionID, brief)
		return "building", popup{title: ev.ToolName, text: brief}
	case "PostToolUse":
		if v, ok := lastToolBrief.Load(ev.SessionID); ok {
			return "building", popup{title: ev.ToolName, text: v.(string) + " ✓"}
		}
		return "building", popup{title: ev.ToolName, text: "Done"}
	case "Notification":
		return "notification", popup{title: "Notification", text: truncate(ev.Message, maxText)}
	case "Stop":
		return "happy", popup{title: "Done", text: truncate(ev.Recap, maxText)}
	case "SessionEnd":
		return "idle", popup{title: "Session Ended", text: "Conversation finished"}
	default:
		return "", popup{}
	}
}

// toolBrief 从 tool_input JSON 提取可读摘要：Bash→command、Edit/Write/Read→file_path。
func toolBrief(toolName string, raw json.RawMessage) string {
	if len(raw) == 0 {
		return "Running " + toolName + "…"
	}
	var m map[string]any
	if json.Unmarshal(raw, &m) != nil {
		return "Running " + toolName + "…"
	}
	// Bash：提取 command 字段
	if cmd, ok := m["command"]; ok {
		return truncate(fmt.Sprint(cmd), maxText)
	}
	// Edit / Write / Read：提取 file_path
	if fp, ok := m["file_path"]; ok {
		return toolName + " " + truncate(fmt.Sprint(fp), maxText-len(toolName)-1)
	}
	if fp, ok := m["filePath"]; ok {
		return toolName + " " + truncate(fmt.Sprint(fp), maxText-len(toolName)-1)
	}
	return "Running " + toolName + "…"
}

// sessionFrame 组装一条会话快照帧。设备按 sid 整条覆盖内存。
// name 只在 ev.Title 非空时才带（SessionStart/Stop 时 probe 从 transcript 提取），
// 其余事件不带 name，设备端不覆盖已有的名。
func sessionFrame(ev protocol.Event, src, state string) Frame {
	sf := Frame{
		T:         "session",
		Src:       src,
		SID:       ev.SessionID,
		State:     state,
		LastReply: truncate(ev.Recap, maxText),
		TS:        ev.Ts,
	}
	if ev.Title != "" {
		sf.Name = truncate(ev.Title, maxName)
	}
	return sf
}

// truncate 截断到 n 字符（按 rune），超长补省略号；空串原样返回。
func truncate(s string, n int) string {
	if s == "" {
		return ""
	}
	r := []rune(s)
	if len(r) <= n {
		return s
	}
	return string(r[:n-1]) + "…"
}
