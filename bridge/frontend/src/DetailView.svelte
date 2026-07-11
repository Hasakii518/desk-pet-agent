<script>
  export let session

  let events = []
  let info = null
  let expanded = new Set()

  $: if (session) { expanded = new Set(); loadAll(session.id) }
  $: if (session && session.liveEvent) {
    events = [session.liveEvent, ...events]
    session.liveEvent = null
  }

  async function loadAll(id) {
    try {
      const [infoR, evR] = await Promise.all([
        fetch(`/api/sessions/${id}`),
        fetch(`/api/sessions/${id}/events?limit=5000`),
      ])
      info = await infoR.json()
      events = await evR.json()
    } catch (e) {}
  }

  function fmtTs(ts) {
    if (!ts) return ''
    const d = new Date(ts)
    return d.toLocaleTimeString('zh-CN', { hour12: false }) + '.' + String(d.getMilliseconds()).padStart(3,'0')
  }

  function toggleExpand(idx) {
    const ns = new Set(expanded)
    if (ns.has(idx)) ns.delete(idx); else ns.add(idx)
    expanded = ns
  }

  function summarize(ev) {
    switch (ev.hook_event_name) {
      case 'PreToolUse':
      case 'PostToolUse': {
        const parts = [ev.tool_name]
        const inp = ev.tool_input || {}
        if (inp.command) parts.push(inp.command)
        else if (inp.file_path) parts.push(inp.file_path)
        else if (inp.pattern) parts.push('/' + inp.pattern + '/')
        else if (inp.path) parts.push(inp.path)
        else if (inp.url) parts.push(inp.url)
        else if (inp.prompt) parts.push('“' + inp.prompt.slice(0,60) + '”')
        else if (inp.subagent_type) parts.push('subagent=' + inp.subagent_type)
        else if (Object.keys(inp).length) parts.push(JSON.stringify(inp).slice(0, 80))
        if (ev.hook_event_name === 'PostToolUse' && ev.tool_response) {
          const r = ev.tool_response
          if (r.exit_code !== undefined) parts.push(`exit=${r.exit_code}`)
          if (r.stdout) parts.push(`→ ${r.stdout.slice(0,60)}`)
        }
        return parts.join(' · ')
      }
      case 'UserPromptSubmit':
        return ev.prompt ? ev.prompt.slice(0, 200) : ''
      case 'SessionStart':
      case 'SessionEnd':
        return ev.source ? `source=${ev.source}` : ''
      case 'Notification':
        return ev.message || ''
      default:
        return ''
    }
  }

  function prettyJSON(obj) {
    if (!obj) return ''
    try { return JSON.stringify(obj, null, 2) } catch { return String(obj) }
  }

  function fmtDur(sec) {
    if (!sec) return '0s'
    if (sec < 60) return `${sec}s`
    if (sec < 3600) return `${Math.floor(sec/60)}m${sec%60}s`
    return `${Math.floor(sec/3600)}h${Math.floor((sec%3600)/60)}m`
  }

  function topToolsStr(m) {
    if (!m) return ''
    return Object.entries(m).sort((a,b) => b[1]-a[1]).map(([t,c]) => `${t}(${c})`).join(' · ')
  }
</script>

<div class="detail-header">
  <h2>{info ? (info.title || info.project_path || info.id) : session.id}</h2>
  {#if info}
    <div class="path-line">{info.project_path}</div>
    <div class="stats">
      {info.event_count} events · {info.tool_call_count} tool calls · {fmtDur(info.duration_sec)} ·
      started {new Date(info.started_at).toLocaleString('zh-CN', {hour12:false})}
      {#if info.status === 'archived'}<span class="archived-tag"> · 已完成</span>{/if}
    </div>
    {#if info.top_tools && Object.keys(info.top_tools).length}
      <div class="stats">tools: {topToolsStr(info.top_tools)}</div>
    {/if}
    {#if info.recap}
      <div class="recap">
        <span class="recap-label">recap</span>
        <span class="recap-text">{info.recap}</span>
      </div>
    {/if}
  {/if}
</div>

<div class="events">
  {#if events.length === 0}
    <div class="empty">No events.</div>
  {/if}
  {#each events as ev, i (i + '-' + ev.ts)}
    <div class="event-row" class:expanded={expanded.has(i)}>
      <div class="event" on:click={() => toggleExpand(i)} role="button" tabindex="0">
        <div class="ts">{fmtTs(ev.ts)}</div>
        <div class="hook hook-{ev.hook_event_name}">{ev.hook_event_name}</div>
        <div class="body">{summarize(ev)}</div>
        <div class="expand-hint">{expanded.has(i) ? '▼' : '▶'}</div>
      </div>
      {#if expanded.has(i)}
        <div class="event-detail">
          {#if ev.tool_input}
            <div class="kv"><span class="k">tool_input</span>
              <pre>{prettyJSON(ev.tool_input)}</pre>
            </div>
          {/if}
          {#if ev.tool_response}
            <div class="kv"><span class="k">tool_response</span>
              <pre>{prettyJSON(ev.tool_response)}</pre>
            </div>
          {/if}
          {#if ev.prompt}
            <div class="kv"><span class="k">prompt</span>
              <pre>{ev.prompt}</pre>
            </div>
          {/if}
          {#if ev.message}
            <div class="kv"><span class="k">message</span>
              <pre>{ev.message}</pre>
            </div>
          {/if}
          {#if !ev.tool_input && !ev.tool_response && !ev.prompt && !ev.message}
            <div class="kv empty-detail">
              {#if ['PreToolUse','PostToolUse','UserPromptSubmit','Notification'].includes(ev.hook_event_name)}
                (旧版 agent 未捕获入参 — 此事件在 tool_input 字段加入前存储)
              {:else}
                no payload
              {/if}
            </div>
          {/if}
        </div>
      {/if}
    </div>
  {/each}
</div>

<style>
  .detail-header h2 { margin: 0 0 4px; font-size: 15px; word-break: break-all; }
  .path-line { color: var(--text-dim); font-family: var(--mono); font-size: 11px; margin-bottom: 6px; word-break: break-all; }
  .stats { color: var(--text-dim); font-size: 12px; margin-top: 3px; }
  .archived-tag { color: var(--yellow); }
  .recap {
    margin-top: 10px;
    padding: 8px 10px;
    background: var(--bg-elev);
    border-left: 2px solid var(--accent);
    border-radius: 0 4px 4px 0;
    font-size: 12px;
    line-height: 1.6;
  }
  .recap-label {
    display: inline-block;
    color: var(--accent);
    font-family: var(--mono);
    font-size: 10px;
    text-transform: uppercase;
    margin-right: 8px;
    vertical-align: top;
  }
  .recap-text { color: var(--text); }
  .event-row { border-bottom: 1px solid var(--border); }
  .event {
    display: grid;
    grid-template-columns: 90px 130px 1fr 16px;
    gap: 10px;
    padding: 6px 0;
    font-size: 13px;
    cursor: pointer;
    align-items: start;
  }
  .event:hover { background: var(--bg-hover); }
  .event .ts { color: var(--text-dim); font-family: var(--mono); font-size: 11px; }
  .event .hook { font-family: var(--mono); font-size: 12px; }
  .event .body { font-family: var(--mono); font-size: 12px; word-break: break-all; }
  .expand-hint { color: var(--text-dim); font-size: 10px; }
  .event-detail { padding: 8px 0 10px 230px; background: var(--bg); }
  .kv { margin-bottom: 6px; }
  .kv .k { display: inline-block; color: var(--accent); font-family: var(--mono); font-size: 11px; margin-bottom: 3px; }
  .kv pre {
    margin: 0; padding: 8px;
    background: var(--bg-elev); border: 1px solid var(--border); border-radius: 3px;
    font-family: var(--mono); font-size: 12px;
    white-space: pre-wrap; word-break: break-all;
    max-height: 400px; overflow-y: auto;
  }
  .empty-detail { color: var(--text-dim); font-size: 11px; }
</style>
