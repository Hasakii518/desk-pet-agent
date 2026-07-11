// Package logbuf 提供固定容量的日志环形缓冲。
// 通过 io.Writer 接入标准 log 包，所有 log.Printf 输出同时进缓冲。
// 也提供带 level 的显式日志方法。
package logbuf

import (
	"bytes"
	"io"
	"sync"
	"time"
)

type Level string

const (
	LevelDebug Level = "DEBUG"
	LevelInfo  Level = "INFO"
	LevelWarn  Level = "WARN"
	LevelError Level = "ERROR"
)

// Entry 是一条日志。
type Entry struct {
	Ts    int64  `json:"ts"`     // unix milli
	Level Level  `json:"level"`
	Msg   string `json:"msg"`
}

// Buffer 是线程安全的环形日志缓冲。
type Buffer struct {
	mu      sync.Mutex
	entries []Entry
	cap     int
	head    int // 下一个写入位置
	size    int // 当前条数
	subs    map[chan Entry]struct{}
}

// New 创建容量为 cap 的缓冲。cap<=0 默认 1000。
func New(capacity int) *Buffer {
	if capacity <= 0 {
		capacity = 1000
	}
	return &Buffer{
		entries: make([]Entry, capacity),
		cap:     capacity,
		subs:    make(map[chan Entry]struct{}),
	}
}

// Write 实现 io.Writer，解析 log 标准格式 "2006/01/02 15:04:05 msg" 为 Entry。
// 无法识别格式的行作为 INFO 级别原始文本。
func (b *Buffer) Write(p []byte) (int, error) {
	msg := string(bytes.TrimRight(p, "\n"))
	// 尝试剥离 log 标准前缀 "2006/01/02 15:04:05 "
	if len(msg) >= 20 && msg[4] == '/' && msg[7] == '/' && msg[10] == ' ' && msg[13] == ':' {
		msg = msg[20:]
	}
	b.append(LevelInfo, msg)
	return len(p), nil
}

// Log 显式写入一条带级别的日志。
func (b *Buffer) Log(level Level, msg string) {
	b.append(level, msg)
}

func (b *Buffer) append(level Level, msg string) {
	e := Entry{Ts: time.Now().UnixMilli(), Level: level, Msg: msg}
	b.mu.Lock()
	b.entries[b.head] = e
	b.head = (b.head + 1) % b.cap
	if b.size < b.cap {
		b.size++
	}
	subs := make([]chan Entry, 0, len(b.subs))
	for ch := range b.subs {
		subs = append(subs, ch)
	}
	b.mu.Unlock()

	// 非阻塞广播给订阅者
	for _, ch := range subs {
		select {
		case ch <- e:
		default:
		}
	}
}

// Recent 返回最近 n 条日志，按时间升序。level 为空则不过滤。
// since > 0 时只返回 ts > since 的条目。
func (b *Buffer) Recent(level Level, limit int, since int64) []Entry {
	if limit <= 0 {
		limit = 200
	}
	b.mu.Lock()
	defer b.mu.Unlock()

	out := make([]Entry, 0, limit)
	// 从最旧开始遍历
	start := b.head - b.size
	if start < 0 {
		start += b.cap
	}
	for i := 0; i < b.size; i++ {
		idx := (start + i) % b.cap
		e := b.entries[idx]
		if since > 0 && e.Ts <= since {
			continue
		}
		if level != "" && e.Level != level {
			continue
		}
		out = append(out, e)
		if len(out) >= limit {
			break
		}
	}
	return out
}

// Subscribe 返回一个 channel，新日志会推送到此。调用方负责 Unsubscribe。
func (b *Buffer) Subscribe() chan Entry {
	ch := make(chan Entry, 64)
	b.mu.Lock()
	b.subs[ch] = struct{}{}
	b.mu.Unlock()
	return ch
}

func (b *Buffer) Unsubscribe(ch chan Entry) {
	b.mu.Lock()
	delete(b.subs, ch)
	b.mu.Unlock()
	close(ch)
}

// MultiWriter 返回同时写入 dst 和 buffer 的 writer，用于 log.SetOutput。
func MultiWriter(dst io.Writer, b *Buffer) io.Writer {
	return io.MultiWriter(dst, b)
}
