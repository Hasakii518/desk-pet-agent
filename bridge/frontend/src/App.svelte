<script>
  import SessionsView from './SessionsView.svelte'
  import DetailView from './DetailView.svelte'
  import Status from './Status.svelte'
  import LogsView from './LogsView.svelte'
  import DoctorView from './DoctorView.svelte'
  import SerialTab from './SerialTab.svelte'
  import PermissionBubble from './PermissionBubble.svelte'

  let selected = null
  let liveCount = 0
  let ws = null
  let view = 'session'
  let permissions = []
  let sidebarCollapsed = false
  let sidebarWidth = 280          // expanded sidebar width, adjustable via drag
  function toggleSidebar() { sidebarCollapsed = !sidebarCollapsed }

  // sidebar resize via drag handle
  function startResize(e) {
    const startX = e.clientX
    const startW = sidebarWidth
    function onMove(ev) {
      sidebarWidth = Math.max(120, Math.min(500, startW + ev.clientX - startX))
    }
    function onUp() {
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
      document.body.style.cursor = ''
      document.body.style.userSelect = ''
    }
    window.addEventListener('mousemove', onMove)
    window.addEventListener('mouseup', onUp)
    document.body.style.cursor = 'col-resize'
    document.body.style.userSelect = 'none'
  }
  function resetWidth() { sidebarWidth = 280 }

  function connectWS() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws'
    ws = new WebSocket(`${proto}://${location.host}/ws`)
    ws.onopen = () => liveCount = 1
    ws.onclose = () => { liveCount = 0; setTimeout(connectWS, 1000) }
    ws.onmessage = (e) => {
      const msg = JSON.parse(e.data)
      if (msg.type === 'event') {
        const ev = msg.data
        if (selected && ev.session_id === selected.id) {
          selected.liveEvent = ev
        }
        window.dispatchEvent(new CustomEvent('cw-event', { detail: ev }))
      } else if (msg.type === 'permission') {
        permissions = [...permissions, msg.data]
      }
    }
  }

  connectWS()

  async function resolvePermission(id, decision) {
    permissions = permissions.filter(p => p.id !== id)
    try {
      await fetch(`/api/permission/${id}/decision`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ decision })
      })
    } catch (e) {}
  }

  // 全局热键 Ctrl+Shift+Y/N
  function onKeydown(e) {
    if (e.ctrlKey && e.shiftKey && permissions.length > 0) {
      if (e.key === 'Y' || e.key === 'y') {
        resolvePermission(permissions[0].id, 'allow')
        e.preventDefault()
      } else if (e.key === 'N' || e.key === 'n') {
        resolvePermission(permissions[0].id, 'deny')
        e.preventDefault()
      }
    }
  }
  window.addEventListener('keydown', onKeydown)

  function onSelect(s) { selected = s; view = 'session' }
  function onToggleLogs() { view = view === 'logs' ? 'session' : 'logs' }
  function onToggleDoctor() { view = view === 'doctor' ? 'session' : 'doctor' }
  function onToggleSerial() { view = view === 'serial' ? 'session' : 'serial' }
</script>

<div class="app" style:grid-template-columns={sidebarCollapsed ? '32px 1fr' : sidebarWidth + 'px 1fr'}>
  <aside class="sidebar" class:collapsed={sidebarCollapsed}>
    <button class="sidebar-toggle" on:click={toggleSidebar}
            title={sidebarCollapsed ? 'expand sidebar' : 'collapse sidebar'}>
      {sidebarCollapsed ? '☰' : '☰'}<span class="toggle-label">{sidebarCollapsed ? '' : ' hide'}</span>
    </button>
    {#if !sidebarCollapsed}
    <div class="sidebar-header">
      <h1><span class="live-dot" class:hidden={!liveCount}></span>ClaudeWatch</h1>
      <span class="live-label">{liveCount ? 'live' : '...'}</span>
    </div>
    <Status {onToggleLogs} logsActive={view === 'logs'} {onToggleDoctor} doctorActive={view === 'doctor'} {onToggleSerial} serialActive={view === 'serial'} />
    <SessionsView {selected} onSelect={onSelect} />
    <div class="sidebar-resize-handle" on:mousedown={startResize} on:dblclick={resetWidth} title="drag to resize, double-click to reset"></div>
    {/if}
  </aside>
  <main class="detail">
    {#if view === 'logs'}
      <LogsView />
    {:else if view === 'doctor'}
      <DoctorView />
    {:else if view === 'serial'}
      <SerialTab />
    {:else if selected}
      <DetailView session={selected} />
    {:else}
      <div class="empty">Select a session from the left, or click "logs"/"doctor".</div>
    {/if}
  </main>
</div>

<PermissionBubble {permissions} onResolve={resolvePermission} />

<style>
  .hidden { display: none; }
  .sidebar-toggle {
    position: absolute;
    top: 8px;
    right: 6px;
    z-index: 2;
    height: 24px;
    padding: 0 6px 0 5px;
    font-size: 13px;
    line-height: 1;
    border-radius: 4px;
    border: 1px solid var(--border);
    background: var(--bg-elev);
    color: var(--text-dim);
    cursor: pointer;
    display: flex;
    align-items: center;
    gap: 3px;
  }
  .sidebar-toggle:hover { color: var(--text); background: var(--bg-hover); }
  .toggle-label { font-size: 10px; font-family: var(--mono); }
  .sidebar.collapsed .sidebar-toggle { position: static; margin: 6px auto 0; padding: 0 4px; }
  .sidebar.collapsed .toggle-label { display: none; }

  .sidebar-resize-handle {
    position: absolute;
    top: 0; right: -3px;
    width: 6px; height: 100%;
    cursor: col-resize;
    z-index: 10;
  }
  .sidebar-resize-handle:hover { background: var(--accent-dim); }
</style>
