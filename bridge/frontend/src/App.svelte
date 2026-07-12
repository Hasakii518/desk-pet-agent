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

<div class="app">
  <aside class="sidebar">
    <div class="sidebar-header">
      <h1><span class="live-dot" class:hidden={!liveCount}></span>ClaudeWatch</h1>
      <span class="live-label">{liveCount ? 'live' : '...'}</span>
    </div>
    <Status {onToggleLogs} logsActive={view === 'logs'} {onToggleDoctor} doctorActive={view === 'doctor'} {onToggleSerial} serialActive={view === 'serial'} />
    <SessionsView {selected} onSelect={onSelect} />
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
</style>
