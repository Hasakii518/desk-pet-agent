<script>
  export let selected
  export let onSelect

  let sessions = []
  let refreshTimer = null
  let showArchived = false

  async function load() {
    try {
      const r = await fetch('/api/sessions?limit=200')
      sessions = await r.json()
    } catch (e) {}
  }

  function scheduleLoad() {
    if (refreshTimer) return
    refreshTimer = setTimeout(() => { refreshTimer = null; load() }, 200)
  }

  load()
  window.addEventListener('cw-event', scheduleLoad)
  setInterval(load, 5000)

  $: activeSessions = (sessions || []).filter(s => s.status !== 'archived')
  $: archivedSessions = (sessions || []).filter(s => s.status === 'archived')

  async function archive(id, e) {
    e.stopPropagation()
    try {
      await fetch(`/api/sessions/${id}/archive`, { method: 'POST' })
      load()
    } catch (e) {}
  }
  async function unarchive(id, e) {
    e.stopPropagation()
    try {
      await fetch(`/api/sessions/${id}/unarchive`, { method: 'POST' })
      load()
    } catch (e) {}
  }

  function fmtRel(ts) {
    if (!ts) return ''
    const s = Math.floor((Date.now() - ts) / 1000)
    if (s < 60) return `${s}s`
    if (s < 3600) return `${Math.floor(s/60)}m`
    if (s < 86400) return `${Math.floor(s/3600)}h`
    return `${Math.floor(s/86400)}d`
  }
  function fmtDur(sec) {
    if (!sec) return ''
    if (sec < 60) return `${sec}s`
    if (sec < 3600) return `${Math.floor(sec/60)}m`
    return `${Math.floor(sec/3600)}h`
  }
  function titleOf(s) {
    return s.title || s.project_path.split('/').filter(Boolean).pop() || s.id.slice(0,8)
  }
</script>

<div class="session-list">
  {#if activeSessions.length === 0 && archivedSessions.length === 0}
    <div class="empty" style="padding:20px">No sessions yet.</div>
  {/if}

  {#if activeSessions.length > 0}
    <div class="group-label">进行中 ({activeSessions.length})</div>
  {/if}
  {#each activeSessions as s (s.id)}
    <div class="session-item"
         class:active={selected && selected.id === s.id}
         on:click={() => onSelect(s)}
         role="button"
         tabindex="0">
      <div class="main-row">
        <div class="path" title={s.project_path}>{titleOf(s)}</div>
        <button class="x-btn" on:click={(e) => archive(s.id, e)} title="归档">×</button>
      </div>
      <div class="meta">
        <span>{s.event_count}ev</span>
        <span>{s.tool_call_count}tools</span>
        <span>{fmtDur(s.duration_sec)}</span>
        <span>{fmtRel(s.last_active_at)}</span>
      </div>
    </div>
  {/each}

  {#if archivedSessions.length > 0}
    <div class="group-label clickable" on:click={() => showArchived = !showArchived} role="button" tabindex="0">
      已完成 ({archivedSessions.length}) {showArchived ? '▼' : '▶'}
    </div>
    {#if showArchived}
      {#each archivedSessions as s (s.id)}
        <div class="session-item archived"
             class:active={selected && selected.id === s.id}
             on:click={() => onSelect(s)}
             role="button"
             tabindex="0">
          <div class="main-row">
            <div class="path" title={s.project_path}>{titleOf(s)}</div>
            <button class="x-btn" on:click={(e) => unarchive(s.id, e)} title="恢复">↺</button>
          </div>
          <div class="meta">
            <span>{s.event_count}ev</span>
            <span>{fmtDur(s.duration_sec)}</span>
            <span>{fmtRel(s.last_active_at)}</span>
          </div>
        </div>
      {/each}
    {/if}
  {/if}
</div>

<style>
  .group-label {
    padding: 6px 14px 4px;
    color: var(--text-dim);
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    border-top: 1px solid var(--border);
    background: var(--bg);
  }
  .group-label.clickable { cursor: pointer; user-select: none; }
  .group-label.clickable:hover { background: var(--bg-hover); }
  .session-item.archived { opacity: 0.6; }
  .main-row { display: flex; justify-content: space-between; align-items: center; gap: 6px; }
  .x-btn {
    background: transparent;
    border: none;
    color: var(--text-dim);
    cursor: pointer;
    font-size: 14px;
    padding: 0 4px;
    line-height: 1;
  }
  .x-btn:hover { color: var(--text); background: var(--bg-hover); border-radius: 3px; }
</style>
