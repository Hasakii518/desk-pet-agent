package store

import (
	"sync"

	"claudewatch/internal/metrics"
)

// Hub 维护实时事件订阅者，Submit 时广播。
// channel 类型为 any，支持推 event、permission 等多种消息。
type Hub struct {
	mu          sync.RWMutex
	subscribers map[chan any]struct{}
	metrics     *metrics.Registry
}

func newHub(m *metrics.Registry) *Hub {
	return &Hub{subscribers: make(map[chan any]struct{}), metrics: m}
}

// Subscribe 返回一个事件 channel，调用方负责 Unsubscribe。
func (h *Hub) Subscribe() chan any {
	ch := make(chan any, 64)
	h.mu.Lock()
	h.subscribers[ch] = struct{}{}
	if h.metrics != nil {
		h.metrics.HubSubscribers.Add(1)
	}
	h.mu.Unlock()
	return ch
}

func (h *Hub) Unsubscribe(ch chan any) {
	h.mu.Lock()
	delete(h.subscribers, ch)
	if h.metrics != nil {
		h.metrics.HubSubscribers.Add(-1)
	}
	h.mu.Unlock()
	close(ch)
}

// broadcast 非阻塞广播，缓冲满则丢弃该订阅者的事件。
func (h *Hub) broadcast(ev any) {
	h.mu.RLock()
	defer h.mu.RUnlock()
	for ch := range h.subscribers {
		select {
		case ch <- ev:
			if h.metrics != nil {
				h.metrics.HubBroadcasts.Add(1)
			}
		default:
			if h.metrics != nil {
				h.metrics.HubDrops.Add(1)
			}
		}
	}
}

// BroadcastAny 导出版本，供外部（如 permission 模块）推送非 Event 消息。
func (h *Hub) BroadcastAny(v any) { h.broadcast(v) }
