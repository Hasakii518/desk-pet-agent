// Package transport 把已落库的 hook 事件翻译成 ESP32 通信帧，
// 经串口 / 蓝牙下行到设备。帧格式见 shared/protocol.md：每行一条 JSON。
//
// 设计要点：
//   - 下行纯推送。设备按 sessionId 维护内存，收到 session 帧整条覆盖，
//     收到 notify 帧只刷实时 state + 弹窗。设备永不主动请求刷新。
//   - Writer 接口隔离物理通道；Multi 扇出，单通道出错只记日志，绝不阻塞 ingest。
//   - 帧统一用 Frame 结构 + omitempty，序列化后保持小体积。
package transport

import "encoding/json"

// Frame 是下行帧的统一结构。t 区分类型，未用字段省略。
//
//   notify    —— 桌宠状态切换 + 通知弹窗（高频，每次 hook 一帧）
//   session   —— 会话详细内容快照（sessionId 全量替换设备内存）
//   heartbeat —— 链路保活
type Frame struct {
	T     string `json:"t"`
	Src   string `json:"src,omitempty"`
	SID   string `json:"sid,omitempty"`
	Name  string `json:"name,omitempty"`
	State string `json:"state,omitempty"`
	Title string `json:"title,omitempty"`
	Text  string `json:"text,omitempty"`

	// 仅 session 帧
	LastReply string `json:"lastReply,omitempty"`
	NextStep  string `json:"nextStep,omitempty"`
	History   []Msg  `json:"history,omitempty"`

	TS int64 `json:"ts"`
}

// Msg 是会话历史里的一条消息。u=true 用户侧，false Agent 侧。
// 字段名刻意短（u/x）省字节，串口帧友好。
type Msg struct {
	U bool   `json:"u"`
	X string `json:"x"`
}

// Writer 是物理通道抽象。Write 写一条已成型的帧行（含 \n，由 NewLine 产出）。
// 实现必须非阻塞友好：通道未连上时返回错误，调用方丢弃该帧即可。
type Writer interface {
	Write(frame []byte) error
	Close() error
}

// NewLine 把一帧 JSON 编码为单行（带 \n）。编码失败返回 nil。
func NewLine(f *Frame) []byte {
	b, err := json.Marshal(f)
	if err != nil {
		return nil
	}
	return append(b, '\n')
}

// Multi 扇出到多个 Writer。任一 Writer 出错只记一次日志（由调用方处理），
// 不影响其他 Writer，也不阻塞调用方。用于同时串口 + 蓝牙下发。
type Multi struct {
	writers []Writer
}

// NewMulti 构造扇出器。传入的 nil Writer 被跳过。
func NewMulti(writers ...Writer) *Multi {
	m := &Multi{}
	for _, w := range writers {
		if w != nil {
			m.writers = append(m.writers, w)
		}
	}
	return m
}

// Write 逐个写。任一出错返回第一个错误，但会继续尝试其余 Writer。
func (m *Multi) Write(frame []byte) error {
	var firstErr error
	for _, w := range m.writers {
		if err := w.Write(frame); err != nil && firstErr == nil {
			firstErr = err
		}
	}
	return firstErr
}

// Close 关闭全部 Writer。
func (m *Multi) Close() error {
	var firstErr error
	for _, w := range m.writers {
		if err := w.Close(); err != nil && firstErr == nil {
			firstErr = err
		}
	}
	return firstErr
}

// HasWriter 报告是否有可用通道（用于决定是否起心跳）。
func (m *Multi) HasWriter() bool { return len(m.writers) > 0 }
