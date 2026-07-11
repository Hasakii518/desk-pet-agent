package store

import (
	"context"
	"encoding/json"
	"fmt"

	"claudewatch/internal/protocol"
)

// SessionInfo 是 session 列表项，附带事件统计。
type SessionInfo struct {
	ID            string         `json:"id"`
	ProjectPath   string         `json:"project_path"`
	StartedAt     int64          `json:"started_at"`
	LastActiveAt  int64          `json:"last_active_at"`
	EventCount    int64          `json:"event_count"`
	DurationSec   int64          `json:"duration_sec"`
	ToolCallCount int64          `json:"tool_call_count"`
	TopTools      map[string]int `json:"top_tools"`
	Title         string         `json:"title"`
	Recap         string         `json:"recap"`
	Status        string         `json:"status"` // active | archived
}

// Stats 是全局统计。
type Stats struct {
	SessionCount int64 `json:"session_count"`
	EventCount   int64 `json:"event_count"`
}

func (s *Store) ListSessions(ctx context.Context, limit int) ([]SessionInfo, error) {
	if limit <= 0 {
		limit = 100
	}
	rows, err := s.db.QueryContext(ctx, `
SELECT s.id, s.project_path, s.started_at, s.last_active_at, s.title, s.recap, s.status,
       COALESCE((SELECT COUNT(*) FROM events e WHERE e.session_id = s.id), 0) AS event_count,
       COALESCE((SELECT COUNT(*) FROM events e WHERE e.session_id = s.id AND e.hook_type = 'PreToolUse'), 0) AS tool_call_count
FROM sessions s
ORDER BY s.last_active_at DESC
LIMIT ?`, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []SessionInfo
	for rows.Next() {
		var si SessionInfo
		if err := rows.Scan(&si.ID, &si.ProjectPath, &si.StartedAt, &si.LastActiveAt, &si.Title, &si.Recap, &si.Status, &si.EventCount, &si.ToolCallCount); err != nil {
			return nil, err
		}
		si.DurationSec = (si.LastActiveAt - si.StartedAt) / 1000
		out = append(out, si)
	}
	return out, rows.Err()
}

func (s *Store) GetSession(ctx context.Context, id string) (*SessionInfo, error) {
	var si SessionInfo
	err := s.db.QueryRowContext(ctx, `
SELECT s.id, s.project_path, s.started_at, s.last_active_at, s.title, s.recap, s.status,
       COALESCE((SELECT COUNT(*) FROM events e WHERE e.session_id = s.id), 0),
       COALESCE((SELECT COUNT(*) FROM events e WHERE e.session_id = s.id AND e.hook_type = 'PreToolUse'), 0)
FROM sessions s
WHERE s.id = ?`, id).Scan(&si.ID, &si.ProjectPath, &si.StartedAt, &si.LastActiveAt, &si.Title, &si.Recap, &si.Status, &si.EventCount, &si.ToolCallCount)
	if err != nil {
		return nil, fmt.Errorf("get session: %w", err)
	}
	si.DurationSec = (si.LastActiveAt - si.StartedAt) / 1000
	si.TopTools = s.topTools(ctx, id, 5)
	return &si, nil
}

// UpdateSessionMeta 更新 session 的 title/recap。
// 用 UPSERT：session 不存在时创建 stub（batch writer 之后会填充其他字段），
// 存在时只在非空时更新（避免覆盖已有 title/recap）。
func (s *Store) UpdateSessionMeta(ctx context.Context, id, title, recap string) error {
	_, err := s.db.ExecContext(ctx, `
INSERT INTO sessions(id, title, recap) VALUES(?, ?, ?)
ON CONFLICT(id) DO UPDATE SET
  title = CASE WHEN excluded.title != '' THEN excluded.title ELSE sessions.title END,
  recap = CASE WHEN excluded.recap != '' THEN excluded.recap ELSE sessions.recap END`, id, title, recap)
	return err
}

// SetSessionStatus 设置 session 状态 (active / archived)。
func (s *Store) SetSessionStatus(ctx context.Context, id, status string) error {
	_, err := s.db.ExecContext(ctx, `UPDATE sessions SET status = ? WHERE id = ?`, status, id)
	return err
}

func (s *Store) topTools(ctx context.Context, id string, limit int) map[string]int {
	rows, err := s.db.QueryContext(ctx, `
SELECT tool_name, COUNT(*) AS c FROM events
WHERE session_id = ? AND tool_name != '' AND hook_type = 'PreToolUse'
GROUP BY tool_name ORDER BY c DESC LIMIT ?`, id, limit)
	if err != nil {
		return nil
	}
	defer rows.Close()
	m := make(map[string]int)
	for rows.Next() {
		var name string
		var c int
		if rows.Scan(&name, &c) == nil {
			m[name] = c
		}
	}
	return m
}

// ListEvents 按 ts 升序返回某 session 的事件，limit<=0 默认 1000。
func (s *Store) ListEvents(ctx context.Context, sessionID string, limit, offset int) ([]protocol.Event, error) {
	if limit <= 0 {
		limit = 1000
	}
	if offset < 0 {
		offset = 0
	}
	rows, err := s.db.QueryContext(ctx, `
SELECT payload FROM events
WHERE session_id = ?
ORDER BY ts DESC, id DESC
LIMIT ? OFFSET ?`, sessionID, limit, offset)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []protocol.Event
	for rows.Next() {
		var payload string
		if err := rows.Scan(&payload); err != nil {
			return nil, err
		}
		var ev protocol.Event
		if err := json.Unmarshal([]byte(payload), &ev); err != nil {
			continue
		}
		out = append(out, ev)
	}
	return out, rows.Err()
}

func (s *Store) Stats(ctx context.Context) (*Stats, error) {
	var st Stats
	err := s.db.QueryRowContext(ctx, `
SELECT (SELECT COUNT(*) FROM sessions), (SELECT COUNT(*) FROM events)`).Scan(&st.SessionCount, &st.EventCount)
	if err != nil {
		return nil, err
	}
	return &st, nil
}
