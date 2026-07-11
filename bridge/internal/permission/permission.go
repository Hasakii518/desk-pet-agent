// Package permission 管理 PreToolUse 同步权限审批。
//
// agent 收到 PreToolUse 事件后创建 Permission，通过 WS 推给 UI，
// 阻塞等 UI 返回 decision (allow/deny)。30s 超时自动 deny。
package permission

import (
	"sync"
	"time"

	"github.com/google/uuid"
)

const defaultTimeout = 30 * time.Second

// Permission 是一次权限请求。
type Permission struct {
	ID        string    `json:"id"`
	SessionID string    `json:"session_id"`
	ToolName  string    `json:"tool_name"`
	ToolInput any       `json:"tool_input,omitempty"`
	CreatedAt int64     `json:"created_at"`
	ch        chan string // decision: "allow" / "deny"
}

// Registry 管理所有待决策的 permission。
type Registry struct {
	mu   sync.Mutex
	pend map[string]*Permission
}

func NewRegistry() *Registry {
	return &Registry{pend: make(map[string]*Permission)}
}

// Create 创建权限请求，返回 Permission。ch 用于等 decision。
func (r *Registry) Create(sessionID, toolName string, toolInput any) *Permission {
	p := &Permission{
		ID:        uuid.NewString(),
		SessionID: sessionID,
		ToolName:  toolName,
		ToolInput: toolInput,
		CreatedAt: time.Now().UnixMilli(),
		ch:        make(chan string, 1),
	}
	r.mu.Lock()
	r.pend[p.ID] = p
	r.mu.Unlock()
	return p
}

// Resolve 提交决策，唤醒等待者。返回 false 表示 permission 不存在或已决策。
func (r *Registry) Resolve(id, decision string) bool {
	r.mu.Lock()
	p, ok := r.pend[id]
	if !ok {
		r.mu.Unlock()
		return false
	}
	delete(r.pend, id)
	r.mu.Unlock()
	select {
	case p.ch <- decision:
	default:
	}
	return true
}

// Wait 阻塞等 decision，超时返回 "deny"。
func (p *Permission) Wait() string {
	select {
	case d := <-p.ch:
		return d
	case <-time.After(defaultTimeout):
		return "deny"
	}
}

// List 返回所有待决策的 permission 快照（供 UI 初始化）。
func (r *Registry) List() []*Permission {
	r.mu.Lock()
	defer r.mu.Unlock()
	out := make([]*Permission, 0, len(r.pend))
	for _, p := range r.pend {
		out = append(out, p)
	}
	return out
}

// CleanupExpired 清理超时未决策的 permission（deny + 删除）。
// 由 agent 定期调用，防止内存泄漏。
func (r *Registry) CleanupExpired() []string {
	r.mu.Lock()
	defer r.mu.Unlock()
	var expired []string
	now := time.Now().UnixMilli()
	for id, p := range r.pend {
		if now-p.CreatedAt > defaultTimeout.Milliseconds() {
			delete(r.pend, id)
			select {
			case p.ch <- "deny":
			default:
			}
			expired = append(expired, id)
		}
	}
	return expired
}
