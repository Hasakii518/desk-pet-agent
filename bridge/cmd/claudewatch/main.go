// claudewatch: agent 主程序。
//
// HTTP server 监听 :7777：
//   POST /ingest            — probe 投递事件
//   GET  /api/sessions      — 列出 session
//   GET  /api/sessions/{id} — session 详情
//   GET  /api/sessions/{id}/events — session 事件流
//   GET  /api/stats         — 全局统计
//   GET  /ws                 — WebSocket 实时事件推送
//   GET  /                   — 前端静态文件（后续接入）
package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/coder/websocket"

	"claudewatch/internal/doctor"
	"claudewatch/internal/logbuf"
	"claudewatch/internal/metrics"
	"claudewatch/internal/permission"
	"claudewatch/internal/protocol"
	"claudewatch/internal/store"
	"claudewatch/internal/update"
	"claudewatch/web"
)

func defaultDBPath() string {
	home, err := os.UserHomeDir()
	if err != nil {
		return "claudewatch.db"
	}
	return filepath.Join(home, ".local", "share", "claudewatch", "events.db")
}

// recentProbeState 记录最近一次 probe 事件的 cwd + ts，供 doctor 诊断用
type recentProbeState struct {
	mu        sync.Mutex
	cwd       string
	lastTs    int64
}

func (r *recentProbeState) update(cwd string, ts int64) {
	r.mu.Lock()
	r.cwd = cwd
	r.lastTs = ts
	r.mu.Unlock()
}

func (r *recentProbeState) snapshot() (string, time.Time) {
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.lastTs == 0 {
		return "", time.Time{}
	}
	return r.cwd, time.UnixMilli(r.lastTs)
}

func main() {
	addr := flag.String("addr", ":7777", "listen address")
	token := flag.String("token", os.Getenv("CLAUDEWATCH_TOKEN"), "auth token (default: $CLAUDEWATCH_TOKEN)")
	dbPath := flag.String("db", "", "SQLite path (default: ~/.local/share/claudewatch/events.db)")
	wslDistro := flag.String("wsl-distro", os.Getenv("WSL_DISTRO_NAME"), "WSL distro name (for UNC path access, e.g. Ubuntu-24.04)")
	flag.Parse()

	if *dbPath == "" {
		*dbPath = defaultDBPath()
	}
	if err := os.MkdirAll(filepath.Dir(*dbPath), 0o755); err != nil {
		log.Fatalf("mkdir db dir: %v", err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	logBuf := logbuf.New(1000)
	log.SetOutput(logbuf.MultiWriter(os.Stderr, logBuf))
	log.Printf("claudewatch starting (db=%s)", *dbPath)

	m := metrics.NewRegistry(*dbPath)
	st, err := store.Open(ctx, store.DefaultConfig(*dbPath), m)
	if err != nil {
		log.Fatalf("open store: %v", err)
	}
	defer st.Close()

	recentProbe := &recentProbeState{}
	permReg := permission.NewRegistry()

	// 定期检查 settings.json hook 注册状态，变化时记日志（#22）
	go hookWatchdogLoop(ctx, *wslDistro, *token, recentProbe, logBuf)
	// 定期清理超时未决策的 permission（#23）
	go permissionCleanupLoop(ctx, permReg, logBuf)

	mux := http.NewServeMux()
	mux.HandleFunc("/ingest", handleIngest(st, *token, m, logBuf, recentProbe))
	mux.HandleFunc("/permission", handlePermission(st, *token, m, logBuf, recentProbe, permReg))
	mux.HandleFunc("/api/sessions", handleListSessions(st))
	mux.HandleFunc("/api/sessions/", handleSessionDetail(st))
	mux.HandleFunc("/api/stats", handleStats(st))
	mux.HandleFunc("/api/status", handleStatus(m, st))
	mux.HandleFunc("/api/doctor", handleDoctor(*wslDistro, *token, st, recentProbe))
	mux.HandleFunc("/api/logs", handleLogs(logBuf))
	mux.HandleFunc("/api/permission/", handlePermissionDecision(permReg, logBuf))
	mux.HandleFunc("/api/update", handleUpdate(*token, logBuf))
	mux.HandleFunc("/ws", handleWS(st))
	mux.HandleFunc("/ws/logs", handleWSLogs(logBuf))
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("ok"))
	})
	mux.HandleFunc("/", handleStatic())

	srv := &http.Server{
		Addr:         *addr,
		Handler:      mux,
		ReadTimeout:  120 * time.Second, // /api/update 上传 ~15MB exe
		WriteTimeout: 30 * time.Second,
	}

	go func() {
		log.Printf("claudewatch agent listening on %s (db=%s)", *addr, *dbPath)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("listen: %v", err)
		}
	}()

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	<-sig
	log.Printf("shutting down...")
	shutCtx, shutCancel := context.WithTimeout(ctx, 3*time.Second)
	defer shutCancel()
	srv.Shutdown(shutCtx)
}

func handleIngest(st *store.Store, token string, m *metrics.Registry, lb *logbuf.Buffer, rp *recentProbeState) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		m.IngestRequests.Add(1)
		if token != "" && r.Header.Get("Authorization") != "Bearer "+token {
			m.IngestAuthFailures.Add(1)
			lb.Log(logbuf.LevelWarn, "ingest: unauthorized request")
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		var ev protocol.Event
		if err := json.NewDecoder(r.Body).Decode(&ev); err != nil {
			m.IngestBadRequests.Add(1)
			lb.Log(logbuf.LevelWarn, "ingest: bad request: "+err.Error())
			http.Error(w, "bad request: "+err.Error(), http.StatusBadRequest)
			return
		}
		rp.update(ev.Cwd, ev.Ts)
		st.Submit(ev)
		// probe 在 Stop 事件时本地读 transcript，把 title/recap 塞进 payload。
		// agent 直接落库，不碰文件系统。
		if ev.Title != "" || ev.Recap != "" {
			ctx2, cancel2 := context.WithTimeout(r.Context(), 3*time.Second)
			if err := st.UpdateSessionMeta(ctx2, ev.SessionID, ev.Title, ev.Recap); err != nil {
				lb.Log(logbuf.LevelWarn, "ingest: update session meta: "+err.Error())
			}
			cancel2()
		}
		// SessionEnd 事件：Claude 端标记 session 结束，自动归档
		if ev.HookEventName == "SessionEnd" {
			ctx2, cancel2 := context.WithTimeout(r.Context(), 3*time.Second)
			if err := st.SetSessionStatus(ctx2, ev.SessionID, "archived"); err != nil {
				lb.Log(logbuf.LevelWarn, "ingest: auto-archive session "+ev.SessionID+": "+err.Error())
			}
			cancel2()
		}
		w.WriteHeader(http.StatusOK)
	}
}

func handleLogs(lb *logbuf.Buffer) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		level := logbuf.Level(r.URL.Query().Get("level"))
		limit, _ := strconv.Atoi(r.URL.Query().Get("limit"))
		since, _ := strconv.ParseInt(r.URL.Query().Get("since"), 10, 64)
		entries := lb.Recent(level, limit, since)
		writeJSON(w, entries)
	}
}

func handleUpdate(token string, lb *logbuf.Buffer) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		if token != "" && r.Header.Get("Authorization") != "Bearer "+token {
			lb.Log(logbuf.LevelWarn, "update: unauthorized")
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}

		selfExe, err := os.Executable()
		if err != nil {
			http.Error(w, "cannot resolve self exe: "+err.Error(), http.StatusInternalServerError)
			return
		}
		tmpPath := update.TempPath(selfExe)
		f, err := os.Create(tmpPath)
		if err != nil {
			http.Error(w, "create temp file: "+err.Error(), http.StatusInternalServerError)
			return
		}
		n, err := io.Copy(f, r.Body)
		f.Close()
		if err != nil {
			os.Remove(tmpPath)
			http.Error(w, "write temp file: "+err.Error(), http.StatusInternalServerError)
			return
		}
		// Linux 需要可执行权限；Windows 不看 x 位，无副作用
		if err := os.Chmod(tmpPath, 0o755); err != nil {
			lb.Log(logbuf.LevelWarn, "update: chmod temp file: "+err.Error())
		}
		lb.Log(logbuf.LevelInfo, fmt.Sprintf("update: received new exe (%d bytes), triggering restart", n))
		w.WriteHeader(http.StatusOK)
		w.Write([]byte("updating"))

		if err := update.Trigger(tmpPath, selfExe, os.Args[1:]); err != nil {
			lb.Log(logbuf.LevelError, "update: trigger failed: "+err.Error())
			return
		}
		// 给响应留时间发出，然后退出
		go func() {
			time.Sleep(500 * time.Millisecond)
			lb.Log(logbuf.LevelInfo, "update: exiting for restart")
			os.Exit(0)
		}()
	}
}

func handleWSLogs(lb *logbuf.Buffer) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		c, err := websocket.Accept(w, r, &websocket.AcceptOptions{
			InsecureSkipVerify: true,
		})
		if err != nil {
			return
		}
		defer c.Close(websocket.StatusNormalClosure, "")

		ch := lb.Subscribe()
		defer lb.Unsubscribe(ch)

		ctx := c.CloseRead(r.Context())
		for {
			select {
			case <-ctx.Done():
				return
			case e, ok := <-ch:
				if !ok {
					return
				}
				data, err := json.Marshal(e)
				if err != nil {
					continue
				}
				if err := c.Write(ctx, websocket.MessageText, data); err != nil {
					return
				}
			}
		}
	}
}

func handleStatus(m *metrics.Registry, st *store.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		snap := m.Snapshot(st.DBSizeBytes())
		writeJSON(w, snap)
	}
}

// handlePermission 处理 PreToolUse 同步审批：创建 Permission + WS 推 + 阻塞等 decision。
// probe 调此端点，根据返回的 decision 设退出码。
func handlePermission(st *store.Store, token string, m *metrics.Registry, lb *logbuf.Buffer, rp *recentProbeState, reg *permission.Registry) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		m.IngestRequests.Add(1)
		if token != "" && r.Header.Get("Authorization") != "Bearer "+token {
			m.IngestAuthFailures.Add(1)
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		var ev protocol.Event
		if err := json.NewDecoder(r.Body).Decode(&ev); err != nil {
			m.IngestBadRequests.Add(1)
			http.Error(w, "bad request: "+err.Error(), http.StatusBadRequest)
			return
		}
		rp.update(ev.Cwd, ev.Ts)
		st.Submit(ev)

		// 创建 permission，WS 推给 UI，阻塞等 decision
		p := reg.Create(ev.SessionID, ev.ToolName, ev.ToolInput)
		st.Hub().BroadcastAny(p)
		lb.Log(logbuf.LevelInfo, "permission: 请求审批 "+ev.ToolName+" (id="+p.ID[:8]+")")

		decision := p.Wait()
		lb.Log(logbuf.LevelInfo, "permission: 决策 "+decision+" (id="+p.ID[:8]+")")
		writeJSON(w, map[string]string{"decision": decision})
	}
}

// handlePermissionDecision 处理 UI 提交的 Allow/Deny。
// 路径: /api/permission/{id}/decision
func handlePermissionDecision(reg *permission.Registry, lb *logbuf.Buffer) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodPost {
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		id := r.URL.Path[len("/api/permission/"):]
		if rest, ok := stripSuffix(id, "/decision"); ok {
			var body struct {
				Decision string `json:"decision"`
			}
			if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
				http.Error(w, "bad request", http.StatusBadRequest)
				return
			}
			if body.Decision != "allow" && body.Decision != "deny" {
				http.Error(w, "decision must be allow or deny", http.StatusBadRequest)
				return
			}
			if !reg.Resolve(rest, body.Decision) {
				http.Error(w, "permission not found or expired", http.StatusNotFound)
				return
			}
			writeJSON(w, map[string]string{"ok": "1"})
			return
		}
		http.NotFound(w, r)
	}
}

// permissionCleanupLoop 定期清理超时未决策的 permission。
func permissionCleanupLoop(ctx context.Context, reg *permission.Registry, lb *logbuf.Buffer) {
	ticker := time.NewTicker(10 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			expired := reg.CleanupExpired()
			if len(expired) > 0 {
				lb.Log(logbuf.LevelWarn, fmt.Sprintf("permission: %d 个超时未决策，自动 deny", len(expired)))
			}
		}
	}
}

func handleDoctor(wslDistro, tokenPath string, st *store.Store, rp *recentProbeState) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		cwd, lastTs := rp.snapshot()
		// tokenPath: %APPDATA%\ClaudeWatch\token (Windows) 或默认
		tp := tokenPath
		if tp == "" {
			tp = filepath.Join(os.Getenv("APPDATA"), "ClaudeWatch", "token")
		}
		cfg := doctor.Config{
			WSLDistro:    wslDistro,
			TokenPath:    tp,
			RecentCwd:    func() string { return cwd },
			LastIngestTs: func() time.Time { return lastTs },
			Store:        st,
		}
		report := doctor.Run(r.Context(), cfg)
		writeJSON(w, report)
	}
}

// hookWatchdogLoop 每 30s 检查 settings.json 的 hook 注册状态，变化时记日志。
func hookWatchdogLoop(ctx context.Context, wslDistro, tokenPath string, rp *recentProbeState, lb *logbuf.Buffer) {
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()
	var lastStatus doctor.Status
	check := func() {
		cwd, lastTs := rp.snapshot()
		tp := tokenPath
		if tp == "" {
			tp = filepath.Join(os.Getenv("APPDATA"), "ClaudeWatch", "token")
		}
		cfg := doctor.Config{
			WSLDistro:    wslDistro,
			TokenPath:    tp,
			RecentCwd:    func() string { return cwd },
			LastIngestTs: func() time.Time { return lastTs },
		}
		report := doctor.Run(context.Background(), cfg)
		for _, c := range report.Checks {
			if c.Name != "hook 注册" {
				continue
			}
			if c.Status != lastStatus {
				if lastStatus != "" {
					lvl := logbuf.LevelInfo
					if c.Status == "error" {
						lvl = logbuf.LevelWarn
					}
					lb.Log(lvl, "hook-watchdog: "+c.Name+" 状态变化 "+string(lastStatus)+" -> "+string(c.Status)+": "+c.Message)
				}
				lastStatus = c.Status
			}
			return
		}
	}
	check() // 启动时跑一次
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			check()
		}
	}
}

func handleListSessions(st *store.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		limit, _ := strconv.Atoi(r.URL.Query().Get("limit"))
		sessions, err := st.ListSessions(r.Context(), limit)
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		writeJSON(w, sessions)
	}
}

func handleSessionDetail(st *store.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		id := r.URL.Path[len("/api/sessions/"):]
		if id == "" {
			http.Error(w, "missing id", http.StatusBadRequest)
			return
		}
		// /api/sessions/{id}/events
		if rest, ok := stripSuffix(id, "/events"); ok {
			limit, _ := strconv.Atoi(r.URL.Query().Get("limit"))
			offset, _ := strconv.Atoi(r.URL.Query().Get("offset"))
			events, err := st.ListEvents(r.Context(), rest, limit, offset)
			if err != nil {
				http.Error(w, err.Error(), http.StatusInternalServerError)
				return
			}
			writeJSON(w, events)
			return
		}
		// /api/sessions/{id}/archive | /api/sessions/{id}/unarchive
		if rest, ok := stripSuffix(id, "/archive"); ok {
			if r.Method != http.MethodPost {
				http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
				return
			}
			if err := st.SetSessionStatus(r.Context(), rest, "archived"); err != nil {
				http.Error(w, err.Error(), http.StatusInternalServerError)
				return
			}
			writeJSON(w, map[string]string{"status": "archived"})
			return
		}
		if rest, ok := stripSuffix(id, "/unarchive"); ok {
			if r.Method != http.MethodPost {
				http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
				return
			}
			if err := st.SetSessionStatus(r.Context(), rest, "active"); err != nil {
				http.Error(w, err.Error(), http.StatusInternalServerError)
				return
			}
			writeJSON(w, map[string]string{"status": "active"})
			return
		}
		si, err := st.GetSession(r.Context(), id)
		if err != nil {
			http.Error(w, err.Error(), http.StatusNotFound)
			return
		}
		writeJSON(w, si)
	}
}

func handleStats(st *store.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		stats, err := st.Stats(r.Context())
		if err != nil {
			http.Error(w, err.Error(), http.StatusInternalServerError)
			return
		}
		writeJSON(w, stats)
	}
}

func handleWS(st *store.Store) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		c, err := websocket.Accept(w, r, &websocket.AcceptOptions{
			InsecureSkipVerify: true, // 允许任意 origin，本地观测用
		})
		if err != nil {
			return
		}
		defer c.Close(websocket.StatusNormalClosure, "")

		ch := st.Hub().Subscribe()
		defer st.Hub().Unsubscribe(ch)

		ctx := c.CloseRead(r.Context())
		for {
			select {
			case <-ctx.Done():
				return
			case msg, ok := <-ch:
				if !ok {
					return
				}
				// 包装成 {type, data} 协议：event / permission
				var wrapped any
				switch v := msg.(type) {
				case protocol.Event:
					wrapped = map[string]any{"type": "event", "data": v}
				case *permission.Permission:
					wrapped = map[string]any{"type": "permission", "data": v}
				default:
					continue
				}
				data, err := json.Marshal(wrapped)
				if err != nil {
					continue
				}
				if err := c.Write(ctx, websocket.MessageText, data); err != nil {
					return
				}
			}
		}
	}
}

func stripSuffix(s, suffix string) (string, bool) {
	if len(s) >= len(suffix) && s[len(s)-len(suffix):] == suffix {
		return s[:len(s)-len(suffix)], true
	}
	return s, false
}

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(v)
}

func handleStatic() http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		p := r.URL.Path
		if p == "/" {
			p = "/index.html"
		}
		data, err := web.Files.ReadFile("dist" + p)
		if err != nil {
			// /assets/ 下找不到的资源直接 404，不能 fallback 成 index.html
			// 否则浏览器缓存旧 HTML 引用旧 JS hash 时，会拿到 HTML 当 JS 执行 → 黑屏
			if strings.HasPrefix(p, "/assets/") {
				http.NotFound(w, r)
				return
			}
			// SPA fallback：其他未匹配路径返回 index.html
			data, err = web.Files.ReadFile("dist/index.html")
			if err != nil {
				http.NotFound(w, r)
				return
			}
			w.Header().Set("Content-Type", "text/html; charset=utf-8")
			w.Header().Set("Cache-Control", "no-cache")
			w.Write(data)
			return
		}
		switch {
		case strings.HasSuffix(p, ".html"):
			w.Header().Set("Content-Type", "text/html; charset=utf-8")
			w.Header().Set("Cache-Control", "no-cache")
		case strings.HasSuffix(p, ".js"):
			w.Header().Set("Content-Type", "application/javascript; charset=utf-8")
			w.Header().Set("Cache-Control", "public, max-age=86400")
		case strings.HasSuffix(p, ".css"):
			w.Header().Set("Content-Type", "text/css; charset=utf-8")
			w.Header().Set("Cache-Control", "public, max-age=86400")
		}
		w.Write(data)
	}
}
