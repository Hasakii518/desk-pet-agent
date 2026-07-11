<script>
  export let onToggleLogs
  export let logsActive
  export let onToggleDoctor
  export let doctorActive

  let status = null
  let ws = null

  async function load() {
    try {
      const r = await fetch('/api/status')
      status = await r.json()
    } catch (e) {}
  }

  load()
  setInterval(load, 2000)

  function fmtUptime(sec) {
    if (sec == null) return '...'
    if (sec < 60) return `${Math.floor(sec)}s`
    if (sec < 3600) return `${Math.floor(sec/60)}m`
    if (sec < 86400) return `${Math.floor(sec/3600)}h`
    return `${Math.floor(sec/86400)}d`
  }

  function fmtSize(b) {
    if (b == null) return ''
    if (b < 1024) return `${b}B`
    if (b < 1024*1024) return `${(b/1024).toFixed(1)}KB`
    if (b < 1024*1024*1024) return `${(b/1024/1024).toFixed(1)}MB`
    return `${(b/1024/1024/1024).toFixed(2)}GB`
  }

  // 健康判断
  $: health = status
    ? (status.store_batch_write_errors > 0 ? 'bad'
       : status.ingest_dropped > 0 ? 'warn'
       : 'ok')
    : 'unknown'
  $: healthColor = { ok: 'var(--green)', warn: 'var(--yellow)', bad: 'var(--red)', unknown: 'var(--text-dim)' }[health]
</script>

<div class="status-bar">
  <div class="health" style="color:{healthColor}" title="health: {health}">
    ●
  </div>
  <div class="pill" title="uptime">
    <span class="k">up</span>
    <span class="v">{fmtUptime(status?.uptime_sec)}</span>
  </div>
  <div class="pill" title="events received">
    <span class="k">ev</span>
    <span class="v">{status?.ingest_events ?? '...'}</span>
  </div>
  <div class="pill" title="store queue depth">
    <span class="k">q</span>
    <span class="v">{status?.store_queue_depth ?? '...'}</span>
  </div>
  <div class="pill" title="WS subscribers">
    <span class="k">ws</span>
    <span class="v">{status?.hub_subscribers ?? '...'}</span>
  </div>
  <div class="pill" title="DB size">
    <span class="k">db</span>
    <span class="v">{fmtSize(status?.db_size_bytes)}</span>
  </div>
  <button class="logs-btn" class:active={doctorActive} on:click={onToggleDoctor} title="toggle diagnostics">
    doctor
  </button>
  <button class="logs-btn" class:active={logsActive} on:click={onToggleLogs} title="toggle logs viewer">
    logs
  </button>
</div>

<style>
  .status-bar {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 6px 10px;
    border-bottom: 1px solid var(--border);
    font-family: var(--mono);
    font-size: 11px;
  }
  .health { font-size: 14px; line-height: 1; }
  .pill {
    display: flex;
    gap: 3px;
    align-items: baseline;
    color: var(--text-dim);
  }
  .pill .v { color: var(--text); }
  .logs-btn {
    margin-left: auto;
    padding: 2px 8px;
    font-size: 11px;
    font-family: var(--mono);
    background: transparent;
    border: 1px solid var(--border);
    color: var(--text-dim);
    cursor: pointer;
    border-radius: 3px;
  }
  .logs-btn:hover { background: var(--bg-hover); color: var(--text); }
  .logs-btn.active { background: var(--accent-dim); color: var(--text); border-color: var(--accent); }
</style>
