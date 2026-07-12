<script>
  let status = { connected: false, port: '', tx_frames: 0, rx_lines: 0, suspended: false }
  let events = []
  let autoRefresh
  let monitorOnly = false  // true = 只看设备上行（模拟 idf.py monitor）
  let sendText = ''
  let sending = false

  async function loadStatus() {
    try {
      const r = await fetch('/api/serial/status')
      status = await r.json()
    } catch (e) {}
  }

  async function loadEvents() {
    try {
      const r = await fetch('/api/serial/log?limit=200')
      events = await r.json()
    } catch (e) {}
  }

  loadStatus()
  loadEvents()
  autoRefresh = setInterval(() => { loadStatus(); loadEvents() }, 2000)

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
      loadEvents()
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

  $: filteredEvents = monitorOnly ? events.filter(e => e.dir === 'rx') : events

  let logEl
  $: if (logEl && filteredEvents.length) {
    const nearBottom = logEl.scrollHeight - logEl.scrollTop - logEl.clientHeight < 60
    if (nearBottom) setTimeout(() => logEl.scrollTop = logEl.scrollHeight, 50)
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
    <label class="monitor-toggle" title="show only device log lines (RX)">
      <input type="checkbox" bind:checked={monitorOnly} />
      <span class="monitor-label">monitor</span>
    </label>
    <div class="spacer"></div>
    {#if status.suspended}
      <button class="btn connect" on:click={connect}>Connect</button>
    {:else}
      <button class="btn disconnect" on:click={disconnect}>Disconnect</button>
    {/if}
  </div>

  <!-- 日志列表 -->
  <div class="log-list" bind:this={logEl}>
    {#each filteredEvents as ev (ev.ts + ev.dir)}
      <div class="log-line" class:tx={ev.dir === 'tx'} class:rx={ev.dir === 'rx'}>
        <span class="ts">{fmtTime(ev.ts)}</span>
        <span class="dir">{ev.dir === 'tx' ? 'TX' : 'RX'}</span>
        <span class="content">{shorten(ev.content, 200)}</span>
      </div>
    {/each}
    {#if filteredEvents.length === 0}
      <div class="empty">{monitorOnly ? 'No device output yet. Is the device running?' : 'No serial events yet.'}</div>
    {/if}
  </div>

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
</style>
