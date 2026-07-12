<script>
  let status = { connected: false, port: '', tx_frames: 0, rx_lines: 0, suspended: false }
  let events = []
  let follow = true        // 自动跟随最新日志（用户上翻阅读时自动关闭）
  let autoRefresh
  let dirFilter = ''       // '' = all, 'tx' = 下发, 'rx' = 设备上行
  let sendText = ''
  let sending = false
  let logEl

  function lastTs() {
    let m = 0
    for (const e of events) if (e.ts > m) m = e.ts
    return m
  }

  async function loadStatus() {
    try {
      const r = await fetch('/api/serial/status')
      status = await r.json()
    } catch (e) {}
  }

  // 首屏：拉最新 200 行
  async function loadInitial() {
    try {
      const r = await fetch('/api/serial/log?limit=200')
      events = await r.json()
    } catch (e) {}
  }

  // 增量：只拉 lastTs 之后的新行并追加，不重排可视区，方便上翻阅读旧日志
  async function poll() {
    try {
      const r = await fetch('/api/serial/log?since=' + lastTs() + '&limit=200')
      const fresh = await r.json()
      if (fresh.length) {
        const seen = new Set(events.map(e => e.ts + e.dir))
        const add = fresh.filter(e => !seen.has(e.ts + e.dir))
        if (add.length) {
          events = events.concat(add)
          if (events.length > 1000) events = events.slice(-1000)
        }
      }
    } catch (e) {}
  }

  loadStatus()
  loadInitial()
  autoRefresh = setInterval(() => { loadStatus(); poll() }, 2000)

  async function disconnect() {
    await fetch('/api/serial/disconnect', { method: 'POST' })
    loadStatus()
  }

  async function connect() {
    await fetch('/api/serial/connect', { method: 'POST' })
    loadStatus()
  }

  async function doSend() {
    const t = sendText.trim()
    if (!t || sending) return
    sending = true
    try {
      await fetch('/api/serial/send', { method: 'POST', body: t })
      sendText = ''
      poll()
    } catch (e) {}
    sending = false
  }

  function onKeydown(e) {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault()
      doSend()
    }
  }

  function fmtTime(ts) {
    const d = new Date(ts)
    return d.toLocaleTimeString()
  }

  function shorten(s, n) {
    if (!s) return ''
    return s.length > n ? s.slice(0, n) + '…' : s
  }

  $: filteredEvents = dirFilter ? (events || []).filter(e => e.dir === dirFilter) : (events || [])

  function nearBottom() {
    if (!logEl) return true
    return logEl.scrollHeight - logEl.scrollTop - logEl.clientHeight < 40
  }
  // 用户滚动时：贴底则继续跟随，上翻则暂停跟随（不再被拽回底部）
  function onScroll() {
    follow = nearBottom()
  }
  function jumpToBottom() {
    follow = true
    if (logEl) logEl.scrollTop = logEl.scrollHeight
  }
  // 仅当 follow 为真（用户没在上翻阅读）时，新数据到达才贴底
  $: if (logEl && follow && filteredEvents.length) {
    setTimeout(() => { if (logEl) logEl.scrollTop = logEl.scrollHeight }, 30)
  }
</script>

<div class="serial-tab">
  <!-- 状态条 + 按钮 -->
  <div class="serial-bar">
    <span class="dot" style="color:{(status.suspended ? '#888' : status.connected ? '#5EE9B0' : '#F87171')}"
          title={status.suspended ? 'suspended' : status.connected ? 'connected' : 'disconnected'}>
      ●
    </span>
    <span class="port">{status.port || '...'}</span>
    <span class="counter" title="frames sent">tx:{status.tx_frames}</span>
    <span class="counter" title="lines received">rx:{status.rx_lines}</span>
    {#if status.suspended}
      <span class="suspended-tag">SUSPENDED</span>
    {/if}
    <select class="dir-select" bind:value={dirFilter} title="filter by direction">
      <option value="">all</option>
      <option value="tx">TX ▼</option>
      <option value="rx">RX ▲</option>
    </select>
    <label class="monitor-toggle" title="auto-scroll to newest line">
      <input type="checkbox" bind:checked={follow} />
      <span class="monitor-label">follow</span>
    </label>
    <div class="spacer"></div>
    {#if status.connected}
      <button class="btn disconnect" on:click={disconnect}>Disconnect</button>
    {:else}
      <button class="btn connect" on:click={connect}>Connect</button>
    {/if}
  </div>

  <!-- 日志列表 -->
  <div class="log-list" bind:this={logEl} on:scroll={onScroll}>
    {#each filteredEvents as ev (ev.ts + ev.dir)}
      <div class="log-line" class:tx={ev.dir === 'tx'} class:rx={ev.dir === 'rx'}>
        <span class="ts">{fmtTime(ev.ts)}</span>
        <span class="dir">{ev.dir === 'tx' ? 'TX' : 'RX'}</span>
        <span class="content">{shorten(ev.content, 200)}</span>
      </div>
    {/each}
    {#if filteredEvents.length === 0}
      <div class="empty">{dirFilter ? 'No ' + dirFilter.toUpperCase() + ' events yet.' : 'No serial events yet.'}</div>
    {/if}
  </div>
  {#if !follow}
    <button class="jump-btn" on:click={jumpToBottom} title="jump to newest line">↓ 回到底部</button>
  {/if}

  <!-- 调试下发 -->
  <div class="send-bar">
    <input type="text" class="send-input"
           placeholder="Type a JSON frame then press Enter to send..."
           bind:value={sendText}
           on:keydown={onKeydown}
           disabled={sending || !status.connected} />
    <button class="btn send-btn"
            on:click={doSend}
            disabled={sending || !status.connected || !sendText.trim()}>
      {sending ? '...' : 'Send'}
    </button>
  </div>
</div>

<style>
  .serial-tab {
    display: flex;
    flex-direction: column;
    height: 100%;
    overflow: hidden;
    position: relative;
  }
  .serial-bar {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 8px 12px;
    border-bottom: 1px solid var(--border);
    font-family: var(--mono);
    font-size: 12px;
    flex-shrink: 0;
  }
  .dot { font-size: 14px; line-height: 1; }
  .port { color: var(--text); font-weight: 600; }
  .counter { color: var(--text-dim); }
  .suspended-tag {
    color: var(--yellow);
    font-size: 10px;
    padding: 1px 4px;
    border: 1px solid var(--yellow);
    border-radius: 3px;
  }
  .dir-select {
    background: var(--bg);
    color: var(--text-dim);
    border: 1px solid var(--border);
    padding: 1px 4px;
    border-radius: 3px;
    font-family: var(--mono);
    font-size: 10px;
    cursor: pointer;
  }
  .dir-select:focus { border-color: var(--accent); color: var(--text); }

  .monitor-toggle {
    display: flex;
    align-items: center;
    gap: 3px;
    cursor: pointer;
    color: var(--text-dim);
    font-size: 11px;
  }
  .monitor-toggle input { cursor: pointer; }
  .monitor-label { user-select: none; }
  .spacer { flex: 1; }
  .btn {
    padding: 3px 12px;
    font-size: 12px;
    font-family: var(--mono);
    border-radius: 3px;
    cursor: pointer;
    border: 1px solid var(--border);
    color: var(--text);
    background: transparent;
  }
  .btn:hover:not(:disabled) { background: var(--bg-hover); }
  .btn:disabled { opacity: 0.4; cursor: default; }
  .disconnect { border-color: var(--red); color: var(--red); }
  .disconnect:hover:not(:disabled) { background: rgba(248,113,113,0.1); }
  .connect { border-color: var(--green); color: var(--green); }
  .connect:hover:not(:disabled) { background: rgba(94,233,176,0.1); }

  .log-list {
    flex: 1;
    overflow-y: auto;
    font-family: var(--mono);
    font-size: 11px;
    padding: 4px 0;
  }
  .log-line {
    display: flex;
    gap: 6px;
    padding: 2px 12px;
    white-space: nowrap;
  }
  .log-line:hover { background: var(--bg-hover); }
  .log-line.rx { background: rgba(0,180,255,0.04); }
  .log-line.rx:hover { background: rgba(0,180,255,0.08); }
  .ts { color: var(--text-dim); width: 70px; flex-shrink: 0; }
  .dir {
    width: 22px;
    flex-shrink: 0;
    font-weight: 600;
    text-align: center;
  }
  .tx .dir { color: var(--green); }
  .rx .dir { color: var(--accent); }
  .content { color: var(--text); overflow: hidden; text-overflow: ellipsis; }

  .send-bar {
    display: flex;
    gap: 6px;
    padding: 8px 12px;
    border-top: 1px solid var(--border);
    flex-shrink: 0;
  }
  .send-input {
    flex: 1;
    background: var(--bg);
    border: 1px solid var(--border);
    color: var(--text);
    font-family: var(--mono);
    font-size: 11px;
    padding: 4px 8px;
    border-radius: 3px;
    outline: none;
  }
  .send-input:focus { border-color: var(--accent); }
  .send-input:disabled { opacity: 0.4; }
  .send-btn { padding: 3px 10px; }
  .send-btn:hover:not(:disabled) { background: var(--accent-dim); border-color: var(--accent); }

  .empty { color: var(--text-dim); padding: 12px; font-style: italic; }

  .jump-btn {
    position: absolute;
    right: 16px;
    bottom: 60px;
    padding: 4px 10px;
    font-size: 11px;
    font-family: var(--mono);
    border-radius: 12px;
    cursor: pointer;
    border: 1px solid var(--accent);
    color: var(--accent);
    background: var(--bg-elev);
    box-shadow: 0 1px 4px rgba(0,0,0,0.3);
  }
  .jump-btn:hover { background: var(--accent-dim); }
</style>
