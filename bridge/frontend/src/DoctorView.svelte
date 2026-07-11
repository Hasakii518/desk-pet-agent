<script>
  let report = null
  let loading = false
  let timer = null

  async function load() {
    loading = true
    try {
      const r = await fetch('/api/doctor')
      report = await r.json()
    } catch (e) {}
    loading = false
  }

  load()
  timer = setInterval(load, 10000)
  // 清理
  import { onDestroy } from 'svelte'
  onDestroy(() => clearInterval(timer))

  function statusColor(s) {
    return { ok: 'var(--green)', warn: 'var(--yellow)', error: 'var(--red)', skip: 'var(--text-dim)' }[s] || 'var(--text-dim)'
  }
  function statusLabel(s) {
    return { ok: 'OK', warn: 'WARN', error: 'ERROR', skip: 'SKIP' }[s] || s
  }
  function fmtTime(ts) {
    return new Date(ts).toLocaleTimeString('zh-CN', { hour12: false })
  }
  $: hasError = report?.checks.some(c => c.status === 'error')
  $: hasWarn = report?.checks.some(c => c.status === 'warn')
</script>

<div class="doctor-view">
  <div class="doctor-header">
    <h2>诊断</h2>
    <span class="generated" title={report?.generated}>
      {report ? '生成于 ' + fmtTime(report.generated) : (loading ? '加载中...' : '...')}
    </span>
    <button on:click={load} disabled={loading}>刷新</button>
  </div>

  {#if report}
    <div class="summary">
      总体:
      {#if hasError}<span class="badge error">{report.checks.filter(c=>c.status==='error').length} 错误</span>{/if}
      {#if hasWarn}<span class="badge warn">{report.checks.filter(c=>c.status==='warn').length} 警告</span>{/if}
      {#if !hasError && !hasWarn}<span class="badge ok">全部正常</span>{/if}
    </div>

    <div class="checks">
      {#each report.checks as c (c.name)}
        <div class="check">
          <div class="status-dot" style="background:{statusColor(c.status)}"></div>
          <div class="check-body">
            <div class="check-name">
              {c.name}
              <span class="status-tag" style="color:{statusColor(c.status)}">{statusLabel(c.status)}</span>
            </div>
            <div class="check-msg">{c.message}</div>
            {#if c.fix_hint}
              <div class="check-fix">修复: {c.fix_hint}</div>
            {/if}
          </div>
        </div>
      {/each}
    </div>
  {:else}
    <div class="empty">{loading ? '加载中...' : '无数据'}</div>
  {/if}
</div>

<style>
  .doctor-view { padding: 18px 22px; }
  .doctor-header { display: flex; align-items: center; gap: 12px; margin-bottom: 14px; }
  .doctor-header h2 { margin: 0; font-size: 16px; }
  .generated { color: var(--text-dim); font-size: 12px; font-family: var(--mono); }
  .doctor-header button { margin-left: auto; }
  .summary { margin-bottom: 16px; font-size: 13px; display: flex; align-items: center; gap: 8px; }
  .badge { padding: 2px 8px; border-radius: 3px; font-size: 11px; font-family: var(--mono); }
  .badge.ok { background: rgba(94,194,122,0.15); color: var(--green); }
  .badge.warn { background: rgba(227,179,65,0.15); color: var(--yellow); }
  .badge.error { background: rgba(239,107,107,0.15); color: var(--red); }
  .checks { display: flex; flex-direction: column; gap: 10px; }
  .check { display: grid; grid-template-columns: 12px 1fr; gap: 12px; padding: 10px; background: var(--bg-elev); border-radius: 4px; }
  .status-dot { width: 10px; height: 10px; border-radius: 50%; margin-top: 4px; }
  .check-name { font-size: 13px; font-weight: 600; display: flex; align-items: center; gap: 8px; }
  .status-tag { font-size: 10px; font-family: var(--mono); font-weight: 400; }
  .check-msg { font-size: 12px; color: var(--text); margin-top: 4px; word-break: break-all; }
  .check-fix { font-size: 11px; color: var(--accent); margin-top: 6px; padding: 4px 8px; background: var(--bg); border-left: 2px solid var(--accent); border-radius: 0 3px 3px 0; }
  .empty { padding: 40px; text-align: center; color: var(--text-dim); }
</style>
