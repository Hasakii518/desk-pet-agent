<script>
  let logs = []
  let ws = null
  let levelFilter = ''  // '' = all
  let autoScroll = true
  let container

  function connect() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws'
    ws = new WebSocket(`${proto}://${location.host}/ws/logs`)
    ws.onopen = () => { /* connected */ }
    ws.onclose = () => setTimeout(connect, 1000)
    ws.onmessage = (e) => {
      const entry = JSON.parse(e.data)
      logs = [...logs, entry]
      if (logs.length > 500) logs = logs.slice(-500)
      if (autoScroll) scrollToBottom()
    }
  }

  async function loadRecent() {
    try {
      const r = await fetch('/api/logs?limit=200')
      logs = await r.json()
      if (autoScroll) setTimeout(scrollToBottom, 50)
    } catch (e) {}
  }

  function scrollToBottom() {
    if (container) container.scrollTop = container.scrollHeight
  }

  // 用户滚动时：贴底则继续自动滚动，上翻则暂停（不被新日志拽回底部）
  function nearBottom() {
    if (!container) return true
    return container.scrollHeight - container.scrollTop - container.clientHeight < 40
  }
  function onScroll() {
    autoScroll = nearBottom()
  }

  loadRecent()
  connect()

  $: filtered = levelFilter ? logs.filter(l => l.level === levelFilter) : logs

  function fmtTs(ts) {
    const d = new Date(ts)
    return d.toLocaleTimeString('zh-CN', { hour12: false }) + '.' + String(d.getMilliseconds()).padStart(3,'0')
  }

  function levelClass(l) { return `lvl-${l}` }
</script>

<div class="logs-view">
  <div class="logs-toolbar">
    <select bind:value={levelFilter}>
      <option value="">all levels</option>
      <option value="DEBUG">DEBUG</option>
      <option value="INFO">INFO</option>
      <option value="WARN">WARN</option>
      <option value="ERROR">ERROR</option>
    </select>
    <label><input type="checkbox" bind:checked={autoScroll}> auto-scroll</label>
    <span class="count">{filtered.length} shown / {logs.length} total</span>
  </div>
  <div class="logs-list" bind:this={container} on:scroll={onScroll}>
    {#each filtered as e (e.ts + '-' + e.msg.slice(0,20))}
      <div class="log-line {levelClass(e.level)}">
        <span class="ts">{fmtTs(e.ts)}</span>
        <span class="level">{e.level}</span>
        <span class="msg">{e.msg}</span>
      </div>
    {/each}
    {#if filtered.length === 0}
      <div class="empty">No logs.</div>
    {/if}
  </div>
</div>

<style>
  .logs-view {
    display: flex;
    flex-direction: column;
    height: 100%;
  }
  .logs-toolbar {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 6px 0 10px;
    border-bottom: 1px solid var(--border);
    margin-bottom: 8px;
    font-size: 12px;
  }
  .logs-toolbar select {
    background: var(--bg-elev);
    color: var(--text);
    border: 1px solid var(--border);
    padding: 2px 6px;
    border-radius: 3px;
    font: inherit;
    font-size: 12px;
  }
  .logs-toolbar label { color: var(--text-dim); display: flex; align-items: center; gap: 4px; }
  .logs-toolbar .count { color: var(--text-dim); margin-left: auto; font-family: var(--mono); font-size: 11px; }
  .logs-list {
    flex: 1;
    overflow-y: auto;
    font-family: var(--mono);
    font-size: 12px;
  }
  .log-line {
    display: grid;
    grid-template-columns: 100px 60px 1fr;
    gap: 8px;
    padding: 2px 0;
    line-height: 1.5;
  }
  .log-line .ts { color: var(--text-dim); }
  .log-line .level { font-weight: 600; }
  .log-line .msg { word-break: break-all; white-space: pre-wrap; }
  .lvl-DEBUG .level { color: var(--text-dim); }
  .lvl-INFO .level { color: var(--accent); }
  .lvl-WARN .level { color: var(--yellow); }
  .lvl-WARN .msg { color: var(--yellow); }
  .lvl-ERROR .level { color: var(--red); }
  .lvl-ERROR .msg { color: var(--red); }
</style>
