package protocol

import "encoding/json"

// Event 是 probe 发给 agent、agent 落库的核心数据结构。
// 字段覆盖 Claude Code 各 hook 类型的已知负载；未识别字段保留在 Payload。
type Event struct {
	Ts            int64           `json:"ts"`
	HookEventName string          `json:"hook_event_name"`
	SessionID     string          `json:"session_id"`
	TranscriptPath string         `json:"transcript_path,omitempty"`
	Cwd           string          `json:"cwd,omitempty"`
	Source        string          `json:"source,omitempty"`

	// 工具相关 (PreToolUse / PostToolUse)
	ToolName      string          `json:"tool_name,omitempty"`
	ToolInput     json.RawMessage `json:"tool_input,omitempty"`
	ToolResponse  json.RawMessage `json:"tool_response,omitempty"`

	// UserPromptSubmit
	Prompt string `json:"prompt,omitempty"`

	// Notification
	Message string `json:"message,omitempty"`

	// probe 在 Stop 事件时从 transcript 提取并附带，agent 直接落库
	Title string `json:"cw_title,omitempty"`
	Recap string `json:"cw_recap,omitempty"`

	// 兜底：其他未识别字段
	Payload json.RawMessage `json:"payload,omitempty"`
}
