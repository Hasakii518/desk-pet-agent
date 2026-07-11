<script>
  export let permissions = []
  export let onResolve

  function summarize(toolName, input) {
    if (!input || typeof input !== 'object') return ''
    if (toolName === 'Bash' && input.command) return input.command.slice(0, 200)
    if ((toolName === 'Write' || toolName === 'Read' || toolName === 'Edit') && input.file_path) return input.file_path
    if (input.pattern) return '/' + input.pattern + '/'
    if (input.path) return input.path
    try { return JSON.stringify(input).slice(0, 150) } catch { return '' }
  }
</script>

{#if permissions.length > 0}
  <div class="permission-stack">
    {#each permissions as p (p.id)}
      <div class="bubble">
        <div class="bubble-header">
          <span class="tool-name">{p.tool_name || 'unknown'}</span>
          <span class="countdown" title="30s 超时自动 deny">⏱</span>
        </div>
        <div class="bubble-body">{summarize(p.tool_name, p.tool_input)}</div>
        <div class="bubble-actions">
          <button class="btn-allow" on:click={() => onResolve(p.id, 'allow')}>Allow (Ctrl+Shift+Y)</button>
          <button class="btn-deny" on:click={() => onResolve(p.id, 'deny')}>Deny (Ctrl+Shift+N)</button>
        </div>
      </div>
    {/each}
  </div>
{/if}

<style>
  .permission-stack {
    position: fixed;
    bottom: 16px;
    right: 16px;
    display: flex;
    flex-direction: column-reverse;
    gap: 8px;
    z-index: 1000;
    max-width: 380px;
  }
  .bubble {
    background: var(--bg-elev);
    border: 1px solid var(--border);
    border-left: 3px solid var(--yellow);
    border-radius: 4px;
    padding: 10px 12px;
    box-shadow: 0 4px 12px rgba(0,0,0,0.3);
    animation: slideIn 0.2s ease;
  }
  @keyframes slideIn { from { transform: translateX(20px); opacity: 0; } to { transform: translateX(0); opacity: 1; } }
  .bubble-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px; }
  .tool-name { font-family: var(--mono); font-size: 13px; color: var(--yellow); font-weight: 600; }
  .countdown { font-size: 12px; color: var(--text-dim); }
  .bubble-body {
    font-family: var(--mono); font-size: 12px; color: var(--text);
    background: var(--bg); padding: 6px 8px; border-radius: 3px;
    word-break: break-all; max-height: 120px; overflow-y: auto;
    margin-bottom: 8px;
  }
  .bubble-actions { display: flex; gap: 8px; }
  .btn-allow, .btn-deny {
    flex: 1; padding: 5px 10px; border-radius: 3px; cursor: pointer;
    font: inherit; font-size: 12px; border: 1px solid var(--border);
  }
  .btn-allow { background: rgba(94,194,122,0.15); color: var(--green); border-color: var(--green); }
  .btn-allow:hover { background: rgba(94,194,122,0.3); }
  .btn-deny { background: rgba(239,107,107,0.15); color: var(--red); border-color: var(--red); }
  .btn-deny:hover { background: rgba(239,107,107,0.3); }
</style>
