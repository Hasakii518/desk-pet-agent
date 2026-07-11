// Package transcript 解析 Claude Code 的 transcript JSONL 文件，
// 提取 session 标题（ai-title）和最后一条 assistant 文字回复（作为 recap）。
package transcript

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"strings"
)

const maxRecapLen = 800

// Result 是解析结果。
type Result struct {
	Title string // 最后一条 ai-title；没有则 fallback 到第一条 user prompt
	Recap string // 最后一条 away_summary；没有则 fallback 到最后一条 assistant 文字
}

type entry struct {
	Type     string          `json:"type"`
	Subtype  string          `json:"subtype"`
	AITitle  string          `json:"aiTitle"`
	Content  string          `json:"content"`
	Message  json.RawMessage `json:"message"`
}

const maxTitleLen = 80

type message struct {
	Content json.RawMessage `json:"content"`
}

// textBlock 是 assistant content 数组里 type=text 的块
type textBlock struct {
	Type string `json:"type"`
	Text string `json:"text"`
}

// Parse 读取 transcript 文件，提取 title 和 recap。
// 文件不存在或解析失败返回空 Result + error。
func Parse(path string) (Result, error) {
	f, err := os.Open(path)
	if err != nil {
		return Result{}, fmt.Errorf("open transcript: %w", err)
	}
	defer f.Close()

	var res Result
	var lastAssistantText string
	var firstUserPrompt string
	scanner := bufio.NewScanner(f)
	scanner.Buffer(make([]byte, 0, 64*1024), 4*1024*1024) // transcript 行可能很长
	for scanner.Scan() {
		line := scanner.Bytes()
		if len(line) == 0 {
			continue
		}
		var e entry
		if err := json.Unmarshal(line, &e); err != nil {
			continue
		}
		switch e.Type {
		case "ai-title":
			if e.AITitle != "" {
				res.Title = e.AITitle
			}
		case "system":
			// away_summary: Claude Code 离席归来自动生成的 recap
			if e.Subtype == "away_summary" && e.Content != "" {
				res.Recap = truncate(e.Content, maxRecapLen)
			}
		case "assistant":
			text := extractAssistantText(e.Message)
			if text != "" {
				lastAssistantText = text
			}
		case "user":
			if firstUserPrompt == "" {
				if t := extractUserText(e.Message); t != "" {
					firstUserPrompt = truncate(t, maxTitleLen)
				}
			}
		}
	}
	// fallback: 没有 ai-title，用第一条 user prompt
	if res.Title == "" && firstUserPrompt != "" {
		res.Title = firstUserPrompt
	}
	// fallback: 没有 away_summary，用最后一条 assistant 文字
	if res.Recap == "" && lastAssistantText != "" {
		res.Recap = truncate(lastAssistantText, maxRecapLen)
	}
	return res, scanner.Err()
}

// extractUserText 从 user message.content 提取文本。
// user 消息 content 通常是 string，也可能是 []block。
func extractUserText(msgRaw json.RawMessage) string {
	if len(msgRaw) == 0 {
		return ""
	}
	var m message
	if err := json.Unmarshal(msgRaw, &m); err != nil {
		return ""
	}
	if len(m.Content) == 0 {
		return ""
	}
	var s string
	if err := json.Unmarshal(m.Content, &s); err == nil {
		return s
	}
	return extractAssistantText(msgRaw) // list 情况复用 assistant 的提取逻辑
}

// extractAssistantText 从 message.content 里提取所有 text block 拼接。
// content 可能是 string 或 []block。
func extractAssistantText(msgRaw json.RawMessage) string {
	if len(msgRaw) == 0 {
		return ""
	}
	var m message
	if err := json.Unmarshal(msgRaw, &m); err != nil {
		return ""
	}
	if len(m.Content) == 0 {
		return ""
	}

	// content 可能是 string
	var s string
	if err := json.Unmarshal(m.Content, &s); err == nil {
		return s
	}

	// 或 []block
	var blocks []json.RawMessage
	if err := json.Unmarshal(m.Content, &blocks); err != nil {
		return ""
	}
	var parts []string
	for _, b := range blocks {
		var tb textBlock
		if json.Unmarshal(b, &tb) == nil && tb.Type == "text" && tb.Text != "" {
			parts = append(parts, tb.Text)
		}
	}
	return strings.Join(parts, "\n")
}

func truncate(s string, n int) string {
	r := []rune(s)
	if len(r) <= n {
		return s
	}
	return string(r[:n]) + "…"
}
