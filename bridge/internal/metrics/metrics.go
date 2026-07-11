// Package metrics 提供 agent 内部指标收集。
// 所有计数器使用 atomic 操作，并发安全。
package metrics

import (
	"sync/atomic"
	"time"
)

// Registry 持有所有指标。零值可用，但建议通过 NewRegistry 构造。
type Registry struct {
	startTime time.Time
	dbPath    string

	// ingest
	IngestRequests     atomic.Int64
	IngestEvents       atomic.Int64
	IngestDropped      atomic.Int64
	IngestAuthFailures atomic.Int64
	IngestBadRequests  atomic.Int64

	// store
	StoreBatchWrites        atomic.Int64
	StoreBatchWriteErrors   atomic.Int64
	StoreEventsWritten      atomic.Int64
	StoreRetentionRuns      atomic.Int64
	StoreRetentionDeleted   atomic.Int64
	StoreQueueDepth         atomic.Int64
	StoreLastFlushTs        atomic.Int64 // unix milli
	StoreLastFlushDurationMs atomic.Int64
	StoreRetentionLastRunTs atomic.Int64

	// hub
	HubSubscribers atomic.Int64
	HubBroadcasts  atomic.Int64
	HubDrops       atomic.Int64
}

func NewRegistry(dbPath string) *Registry {
	return &Registry{
		startTime: time.Now(),
		dbPath:    dbPath,
	}
}

// Snapshot 是 /api/status 的响应结构。
type Snapshot struct {
	StartTime time.Time `json:"start_time"`
	UptimeSec float64   `json:"uptime_sec"`
	DBPath    string    `json:"db_path"`
	DBSizeBytes int64   `json:"db_size_bytes"`

	IngestRequests     int64 `json:"ingest_requests"`
	IngestEvents       int64 `json:"ingest_events"`
	IngestDropped      int64 `json:"ingest_dropped"`
	IngestAuthFailures int64 `json:"ingest_auth_failures"`
	IngestBadRequests  int64 `json:"ingest_bad_requests"`

	StoreBatchWrites         int64 `json:"store_batch_writes"`
	StoreBatchWriteErrors    int64 `json:"store_batch_write_errors"`
	StoreEventsWritten       int64 `json:"store_events_written"`
	StoreRetentionRuns       int64 `json:"store_retention_runs"`
	StoreRetentionDeleted    int64 `json:"store_retention_deleted"`
	StoreQueueDepth          int64 `json:"store_queue_depth"`
	StoreLastFlushTs         int64 `json:"store_last_flush_ts"`
	StoreLastFlushDurationMs int64 `json:"store_last_flush_duration_ms"`
	StoreRetentionLastRunTs  int64 `json:"store_retention_last_run_ts"`

	HubSubscribers int64 `json:"hub_subscribers"`
	HubBroadcasts  int64 `json:"hub_broadcasts"`
	HubDrops       int64 `json:"hub_drops"`
}

func (r *Registry) Snapshot(dbSize int64) Snapshot {
	return Snapshot{
		StartTime:                r.startTime,
		UptimeSec:                time.Since(r.startTime).Seconds(),
		DBPath:                   r.dbPath,
		DBSizeBytes:              dbSize,
		IngestRequests:           r.IngestRequests.Load(),
		IngestEvents:             r.IngestEvents.Load(),
		IngestDropped:            r.IngestDropped.Load(),
		IngestAuthFailures:       r.IngestAuthFailures.Load(),
		IngestBadRequests:        r.IngestBadRequests.Load(),
		StoreBatchWrites:         r.StoreBatchWrites.Load(),
		StoreBatchWriteErrors:    r.StoreBatchWriteErrors.Load(),
		StoreEventsWritten:       r.StoreEventsWritten.Load(),
		StoreRetentionRuns:       r.StoreRetentionRuns.Load(),
		StoreRetentionDeleted:    r.StoreRetentionDeleted.Load(),
		StoreQueueDepth:          r.StoreQueueDepth.Load(),
		StoreLastFlushTs:         r.StoreLastFlushTs.Load(),
		StoreLastFlushDurationMs: r.StoreLastFlushDurationMs.Load(),
		StoreRetentionLastRunTs:  r.StoreRetentionLastRunTs.Load(),
		HubSubscribers:           r.HubSubscribers.Load(),
		HubBroadcasts:            r.HubBroadcasts.Load(),
		HubDrops:                 r.HubDrops.Load(),
	}
}
