// Package store 实现 SQLite 批写器与保留策略。
// 通过 channel 接收事件，后台 goroutine 批量落库。
package store

import (
	"context"
	"database/sql"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"time"

	"claudewatch/internal/metrics"
	"claudewatch/internal/protocol"

	_ "modernc.org/sqlite"
)

const (
	schema = `
CREATE TABLE IF NOT EXISTS sessions (
  id TEXT PRIMARY KEY,
  project_path TEXT NOT NULL DEFAULT '',
  started_at INTEGER NOT NULL DEFAULT 0,
  last_active_at INTEGER NOT NULL DEFAULT 0,
  title TEXT NOT NULL DEFAULT '',
  recap TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'active'
);
CREATE TABLE IF NOT EXISTS events (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  session_id TEXT NOT NULL,
  hook_type TEXT NOT NULL,
  ts INTEGER NOT NULL,
  tool_name TEXT NOT NULL DEFAULT '',
  payload TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_events_session_ts ON events(session_id, ts);
CREATE INDEX IF NOT EXISTS idx_events_ts ON events(ts);
`
)

// Store 是事件存储，封装 SQLite 连接与批写 goroutine。
type Store struct {
	db       *sql.DB
	in       chan protocol.Event
	cfg      Config
	hub      *Hub
	metrics  *metrics.Registry
	dbPath   string
}

// Config 控制批写与保留策略。
type Config struct {
	Path        string        // SQLite 文件路径
	BatchSize   int           // 每批最大事件数
	FlushPeriod time.Duration // 强制刷盘间隔
	Retention   time.Duration // 事件保留时长，0 表示永久
}

// DefaultConfig 返回适合本地观测的默认配置。
func DefaultConfig(path string) Config {
	return Config{
		Path:        path,
		BatchSize:   200,
		FlushPeriod: 200 * time.Millisecond,
		Retention:   30 * 24 * time.Hour,
	}
}

// Open 打开数据库、建表、启动批写与保留 goroutine。
// metrics 可为 nil（不收集指标）。
func Open(ctx context.Context, cfg Config, m *metrics.Registry) (*Store, error) {
	db, err := sql.Open("sqlite", cfg.Path+"?_pragma=journal_mode(WAL)&_pragma=synchronous(NORMAL)&_pragma=busy_timeout(5000)")
	if err != nil {
		return nil, fmt.Errorf("open sqlite: %w", err)
	}
	db.SetMaxOpenConns(1) // SQLite 写入串行，单连接避免锁竞争
	if _, err := db.Exec(schema); err != nil {
		db.Close()
		return nil, fmt.Errorf("apply schema: %w", err)
	}
	// 老库迁移：补 title/recap/status 列（列已存在会报错，忽略）
	for _, stmt := range []string{
		`ALTER TABLE sessions ADD COLUMN title TEXT NOT NULL DEFAULT ''`,
		`ALTER TABLE sessions ADD COLUMN recap TEXT NOT NULL DEFAULT ''`,
		`ALTER TABLE sessions ADD COLUMN status TEXT NOT NULL DEFAULT 'active'`,
	} {
		_, _ = db.Exec(stmt) // 忽略 "duplicate column" 错误
	}

	s := &Store{
		db:      db,
		in:      make(chan protocol.Event, 1024),
		cfg:     cfg,
		hub:     newHub(m),
		metrics: m,
		dbPath:  cfg.Path,
	}
	go s.batchLoop(ctx)
	if cfg.Retention > 0 {
		go s.retentionLoop(ctx)
	}
	return s, nil
}

// Hub 返回实时事件订阅 hub，供 WebSocket 推送使用。
func (s *Store) Hub() *Hub { return s.hub }

// Submit 投递一个事件到批写队列，并广播给实时订阅者。非阻塞，缓冲满则丢弃。
func (s *Store) Submit(ev protocol.Event) bool {
	s.hub.broadcast(ev)
	select {
	case s.in <- ev:
		if s.metrics != nil {
			s.metrics.IngestEvents.Add(1)
		}
		return true
	default:
		log.Printf("store: channel full, dropping event (session=%s hook=%s)", ev.SessionID, ev.HookEventName)
		if s.metrics != nil {
			s.metrics.IngestDropped.Add(1)
		}
		return false
	}
}

// DBSizeBytes 返回 DB 文件大小（含 WAL）。
func (s *Store) DBSizeBytes() int64 {
	var total int64
	for _, p := range []string{s.dbPath, s.dbPath + "-wal"} {
		if fi, err := os.Stat(p); err == nil {
			total += fi.Size()
		}
	}
	return total
}

// Close 关闭数据库。等待批写 goroutine 退出。
func (s *Store) Close() error {
	close(s.in)
	return s.db.Close()
}

func (s *Store) batchLoop(ctx context.Context) {
	batch := make([]protocol.Event, 0, s.cfg.BatchSize)
	ticker := time.NewTicker(s.cfg.FlushPeriod)
	defer ticker.Stop()

	flush := func() {
		if len(batch) == 0 {
			return
		}
		start := time.Now()
		n := len(batch)
		err := s.writeBatch(batch)
		dur := time.Since(start)
		if s.metrics != nil {
			s.metrics.StoreBatchWrites.Add(1)
			s.metrics.StoreEventsWritten.Add(int64(n))
			s.metrics.StoreLastFlushTs.Store(time.Now().UnixMilli())
			s.metrics.StoreLastFlushDurationMs.Store(dur.Milliseconds())
			if err != nil {
				s.metrics.StoreBatchWriteErrors.Add(1)
			}
		}
		if err != nil {
			log.Printf("store: write batch failed: %v", err)
		}
		batch = batch[:0]
	}

	for {
		// 更新队列深度 gauge
		if s.metrics != nil {
			s.metrics.StoreQueueDepth.Store(int64(len(s.in)))
		}
		select {
		case <-ctx.Done():
			flush()
			return
		case ev, ok := <-s.in:
			if !ok {
				flush()
				return
			}
			batch = append(batch, ev)
			if len(batch) >= s.cfg.BatchSize {
				flush()
			}
		case <-ticker.C:
			flush()
		}
	}
}

func (s *Store) writeBatch(batch []protocol.Event) error {
	tx, err := s.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()

	upsertSession, err := tx.Prepare(`INSERT INTO sessions(id, project_path, started_at, last_active_at)
VALUES(?, ?, ?, ?)
ON CONFLICT(id) DO UPDATE SET
  project_path = excluded.project_path,
  last_active_at = excluded.last_active_at`)
	if err != nil {
		return err
	}
	defer upsertSession.Close()

	insertEvent, err := tx.Prepare(`INSERT INTO events(session_id, hook_type, ts, tool_name, payload) VALUES(?, ?, ?, ?, ?)`)
	if err != nil {
		return err
	}
	defer insertEvent.Close()

	seen := make(map[string]struct{}, len(batch))
	for _, ev := range batch {
		if ev.SessionID == "" {
			continue
		}
		if _, ok := seen[ev.SessionID]; !ok {
			startedAt := ev.Ts
			if _, err := upsertSession.Exec(ev.SessionID, ev.Cwd, startedAt, ev.Ts); err != nil {
				return err
			}
			seen[ev.SessionID] = struct{}{}
		} else {
			// 仅刷新 last_active_at
			if _, err := tx.Exec(`UPDATE sessions SET last_active_at = ? WHERE id = ? AND ? > last_active_at`,
				ev.Ts, ev.SessionID, ev.Ts); err != nil {
				return err
			}
		}
		payload, err := json.Marshal(ev)
		if err != nil {
			payload = []byte("{}")
		}
		if _, err := insertEvent.Exec(ev.SessionID, ev.HookEventName, ev.Ts, ev.ToolName, string(payload)); err != nil {
			return err
		}
	}
	return tx.Commit()
}

func (s *Store) retentionLoop(ctx context.Context) {
	ticker := time.NewTicker(1 * time.Hour)
	defer ticker.Stop()
	s.cleanup()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			s.cleanup()
		}
	}
}

func (s *Store) cleanup() {
	if s.cfg.Retention <= 0 {
		return
	}
	cutoff := time.Now().Add(-s.cfg.Retention).UnixMilli()
	res, err := s.db.Exec(`DELETE FROM events WHERE ts < ?`, cutoff)
	if err != nil {
		log.Printf("store: retention cleanup failed: %v", err)
		return
	}
	deleted, _ := res.RowsAffected()
	// 清理无事件的孤儿 session
	if _, err := s.db.Exec(`DELETE FROM sessions WHERE id NOT IN (SELECT DISTINCT session_id FROM events)`); err != nil {
		log.Printf("store: orphan session cleanup failed: %v", err)
	}
	if s.metrics != nil {
		s.metrics.StoreRetentionRuns.Add(1)
		s.metrics.StoreRetentionDeleted.Add(deleted)
		s.metrics.StoreRetentionLastRunTs.Store(time.Now().UnixMilli())
	}
}
