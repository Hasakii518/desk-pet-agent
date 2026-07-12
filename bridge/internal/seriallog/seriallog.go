// Package seriallog 维护串口通信日志的环形缓冲：下行 TX JSON 帧 + 上行 RX
// 设备日志 / Command。供 web UI 查看"串口上发生了什么"。
package seriallog

import (
	"sync"
	"time"
)

// Event 是串口通信的一条记录。
type Event struct {
	Ts      int64  `json:"ts"`
	Dir     string `json:"dir"`     // "tx" 下行 / "rx" 上行
	Content string `json:"content"` // 单行 JSON 或设备日志
}

// Log 是定长环形缓冲，线程安全。
type Log struct {
	mu   sync.Mutex
	buf  []Event
	pos  int  // 下一个写入位置
	full bool // 是否已写过一轮
}

// New 创建容量为 cap 的环形缓冲。
func New(cap int) *Log {
	return &Log{buf: make([]Event, cap)}
}

// Add 追加一条记录。dir="tx" 或 "rx"。
func (l *Log) Add(dir, content string) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.buf[l.pos] = Event{Ts: time.Now().UnixMilli(), Dir: dir, Content: content}
	l.pos++
	if l.pos >= len(l.buf) {
		l.pos = 0
		l.full = true
	}
}

// Recent 返回最近的 limit 条记录，按时间升序。
func (l *Log) Recent(limit int) []Event {
	l.mu.Lock()
	defer l.mu.Unlock()

	n := l.pos
	if l.full {
		n = len(l.buf)
	}
	if limit <= 0 || limit > n {
		limit = n
	}

	out := make([]Event, limit)
	for i := 0; i < limit; i++ {
		idx := l.pos - limit + i
		if idx < 0 {
			idx += len(l.buf)
		}
		out[i] = l.buf[idx]
	}
	return out
}

// Since 返回 Ts 严格大于 sinceTs 的所有记录，按时间升序。
// 供前端增量追加：只拉取上次之后新产生的行，避免整段重排抢滚动。
func (l *Log) Since(sinceTs int64) []Event {
	l.mu.Lock()
	defer l.mu.Unlock()

	total := l.pos
	if l.full {
		total = len(l.buf)
	}

	out := make([]Event, 0, 16)
	// 从最旧一条按时间顺序遍历（满环时最旧在 l.pos），收集 ts > sinceTs
	for i := 0; i < total; i++ {
		idx := (l.pos + i) % len(l.buf)
		e := l.buf[idx]
		if e.Ts > sinceTs {
			out = append(out, e)
		}
	}
	return out
}

// Count 返回当前已缓存的记录数（最多 cap）。
func (l *Log) Count() int {
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.full {
		return len(l.buf)
	}
	return l.pos
}
