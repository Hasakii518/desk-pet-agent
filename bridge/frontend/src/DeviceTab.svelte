<script>
  // 设备连接页：BLE 扫描/连接 + WiFi 配网
  let bleStatus = null      // /api/ble/status 返回；null = 尚未加载
  let bleEnabled = true     // false = bridge 未以 --ble 启动（503）
  let serialStatus = null

  let scanning = false
  let devices = []
  let scanError = ''

  let ssid = ''
  let password = ''
  let showPassword = false
  let provisioning = false
  let provError = ''

  async function loadStatus() {
    try {
      const r = await fetch('/api/ble/status')
      if (r.status === 503) { bleEnabled = false; bleStatus = null; return }
      bleEnabled = true
      bleStatus = await r.json()
    } catch (e) {}
  }
  async function loadSerialStatus() {
    try {
      const r = await fetch('/api/serial/status')
      serialStatus = await r.json()
    } catch (e) {}
  }
  loadStatus()
  loadSerialStatus()
  setInterval(() => { loadStatus(); loadSerialStatus() }, 2000)

  async function scan() {
    if (scanning) return
    scanning = true
    scanError = ''
    try {
      const r = await fetch('/api/ble/scan')
      const j = await r.json()
      if (!r.ok) { scanError = j.error || j.message || ('scan failed: ' + r.status); devices = [] }
      else devices = j.devices || []
    } catch (e) { scanError = String(e) }
    scanning = false
  }

  async function connect(addr) {
    await fetch('/api/ble/connect', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ address: addr || '' })
    })
    setTimeout(loadStatus, 1000)
  }

  async function disconnect() {
    await fetch('/api/ble/disconnect', { method: 'POST' })
    setTimeout(loadStatus, 500)
  }

  async function provision() {
    if (provisioning || !ssid.trim()) return
    provisioning = true
    provError = ''
    try {
      const r = await fetch('/api/wifi/provision', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid: ssid.trim(), password })
      })
      if (!r.ok) provError = await r.text()
    } catch (e) { provError = String(e) }
    provisioning = false
    setTimeout(loadStatus, 500)
  }

  function rssiBars(rssi) {
    if (rssi == null) return '----'
    if (rssi >= -55) return '▂▄▆█'
    if (rssi >= -70) return '▂▄▆_'
    if (rssi >= -85) return '▂▄__'
    return '▂___'
  }
  function wifiStateText(w) {
    if (!w) return ''
    return { idle: '空闲', connecting: '连接中…', ok: '已连接', fail: '失败' }[w.s] || w.s
  }
  $: connected = bleStatus?.connected
  $: wifi = bleStatus?.wifi
</script>

<div class="device-tab">
  {#if !bleEnabled}
    <div class="notice">
      <h3>BLE 未启用</h3>
      <p>bridge 当前以串口模式运行。重启时加 <code>--ble</code> 即可启用蓝牙下发：</p>
      <pre>claudewatch --ble</pre>
      <p class="dim">注意：WSL2 无法访问蓝牙适配器，请在原生 Windows / macOS / Linux 上运行。</p>
      {#if serialStatus}
        <p class="dim">串口：{serialStatus.connected ? '已连接 ' + serialStatus.port : '未连接'}</p>
      {/if}
    </div>
  {:else}
    <!-- BLE 链路状态 -->
    <div class="card">
      <div class="card-title">
        <span class="dot" style="color:{connected ? 'var(--green)' : bleStatus?.suspended ? '#888' : 'var(--red)'}">●</span>
        蓝牙链路
        <div class="spacer"></div>
        {#if connected}
          <button class="btn danger" on:click={disconnect}>断开</button>
        {:else if bleStatus?.suspended}
          <button class="btn ok" on:click={() => connect('')}>恢复自动连接</button>
        {/if}
      </div>
      <div class="kv-grid">
        <span class="k">适配器</span>
        <span class="v">{bleStatus?.adapter_ok ? '就绪' : (bleStatus?.last_error ? '异常' : '初始化中…')}</span>
        {#if connected}
          <span class="k">设备</span><span class="v">{bleStatus.device}</span>
          <span class="k">地址</span><span class="v mono">{bleStatus.address}</span>
          <span class="k">信号</span><span class="v">{rssiBars(bleStatus.rssi)} {bleStatus.rssi} dBm</span>
          <span class="k">MTU</span><span class="v">{bleStatus.mtu}</span>
        {:else}
          <span class="k">状态</span>
          <span class="v">{bleStatus?.suspended ? '已暂停（不自动重连）' : '扫描中…'}</span>
        {/if}
        <span class="k">收发</span>
        <span class="v mono">tx:{bleStatus?.tx_frames ?? 0} rx:{bleStatus?.rx_lines ?? 0}</span>
        {#if bleStatus?.last_error}
          <span class="k">最近错误</span><span class="v err">{bleStatus.last_error}</span>
        {/if}
      </div>
    </div>

    <!-- 扫描 / 手动选设备 -->
    <div class="card">
      <div class="card-title">
        扫描设备
        <div class="spacer"></div>
        <button class="btn" on:click={scan} disabled={scanning}>
          {scanning ? '扫描中…' : '扫描 (4s)'}
        </button>
      </div>
      {#if scanError}<div class="err-line">{scanError}</div>{/if}
      {#if devices.length === 0 && !scanning}
        <div class="dim-line">未发现设备。确认设备已上电且广播名为 ClawdPet-*。</div>
      {/if}
      {#each devices as d (d.address)}
        <div class="dev-row">
          <span class="dev-name">{d.name}</span>
          <span class="dev-addr mono">{d.address}</span>
          <span class="dev-rssi">{rssiBars(d.rssi)} {d.rssi}</span>
          <div class="spacer"></div>
          <button class="btn ok" on:click={() => connect(d.address)}
                  disabled={connected && bleStatus.address === d.address}>
            {connected && bleStatus.address === d.address ? '已连接' : '连接'}
          </button>
        </div>
      {/each}
    </div>

    <!-- WiFi 配网 -->
    <div class="card">
      <div class="card-title">WiFi 配网</div>
      {#if !connected}
        <div class="dim-line">先连接设备蓝牙，再下发 WiFi 凭据。</div>
      {:else}
        <div class="form">
          <input class="input" type="text" placeholder="WiFi 名称 (SSID)" bind:value={ssid} maxlength="32" />
          <div class="pass-row">
            {#if showPassword}
              <input class="input" type="text"
                     placeholder="密码（开放网络留空）" bind:value={password} maxlength="64" />
            {:else}
              <input class="input" type="password"
                     placeholder="密码（开放网络留空）" bind:value={password} maxlength="64" />
            {/if}
            <button class="btn" on:click={() => showPassword = !showPassword}>
              {showPassword ? '隐藏' : '显示'}
            </button>
          </div>
          <button class="btn primary" on:click={provision} disabled={provisioning || !ssid.trim()}>
            {provisioning ? '下发中…' : '下发并连接'}
          </button>
          {#if provError}<div class="err-line">{provError}</div>{/if}
        </div>
        {#if wifi}
          <div class="wifi-result" class:ok={wifi.s === 'ok'} class:fail={wifi.s === 'fail'}>
            <span class="k">WiFi</span>
            <span class="v">{wifiStateText(wifi)}</span>
            {#if wifi.s === 'ok'}
              <span class="v mono">{wifi.ip}</span>
              <span class="v">{rssiBars(wifi.rssi)} {wifi.rssi} dBm</span>
            {:else if wifi.s === 'fail'}
              <span class="v err">原因码 {wifi.err}（检查密码/信号后重试）</span>
            {/if}
          </div>
        {/if}
      {/if}
    </div>
  {/if}
</div>

<style>
  .device-tab {
    height: 100%;
    overflow-y: auto;
    padding: 16px;
    display: flex;
    flex-direction: column;
    gap: 12px;
    max-width: 640px;
  }
  .card {
    border: 1px solid var(--border);
    border-radius: 8px;
    background: var(--bg-elev);
    padding: 12px 14px;
  }
  .card-title {
    display: flex;
    align-items: center;
    gap: 8px;
    font-weight: 600;
    font-size: 13px;
    margin-bottom: 10px;
  }
  .dot { font-size: 13px; }
  .spacer { flex: 1; }

  .kv-grid {
    display: grid;
    grid-template-columns: 72px 1fr;
    row-gap: 5px;
    font-size: 12px;
  }
  .kv-grid .k { color: var(--text-dim); }
  .kv-grid .v { color: var(--text); }
  .mono { font-family: var(--mono); }
  .err { color: var(--red); }

  .dev-row {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 6px 0;
    border-top: 1px solid var(--border);
    font-size: 12px;
  }
  .dev-name { color: var(--text); font-weight: 600; }
  .dev-addr { color: var(--text-dim); font-size: 11px; }
  .dev-rssi { color: var(--text-dim); font-family: var(--mono); font-size: 11px; }

  .form { display: flex; flex-direction: column; gap: 8px; }
  .pass-row { display: flex; gap: 6px; }
  .pass-row .input { flex: 1; }
  .input {
    background: var(--bg);
    border: 1px solid var(--border);
    color: var(--text);
    padding: 7px 10px;
    border-radius: 5px;
    font-size: 13px;
    outline: none;
  }
  .input:focus { border-color: var(--accent); }

  .btn {
    padding: 5px 14px;
    font-size: 12px;
    border-radius: 5px;
    cursor: pointer;
    border: 1px solid var(--border);
    color: var(--text);
    background: transparent;
  }
  .btn:hover:not(:disabled) { background: var(--bg-hover); }
  .btn:disabled { opacity: 0.4; cursor: default; }
  .btn.primary { border-color: var(--accent); color: var(--accent); align-self: flex-start; }
  .btn.primary:hover:not(:disabled) { background: var(--accent-dim); }
  .btn.ok { border-color: var(--green); color: var(--green); }
  .btn.danger { border-color: var(--red); color: var(--red); }

  .wifi-result {
    margin-top: 10px;
    display: flex;
    gap: 10px;
    align-items: baseline;
    font-size: 12px;
    border-top: 1px solid var(--border);
    padding-top: 8px;
  }
  .wifi-result .k { color: var(--text-dim); }
  .wifi-result.ok .v { color: var(--green); }
  .wifi-result.fail .v { color: var(--red); }

  .err-line { color: var(--red); font-size: 12px; margin-top: 6px; word-break: break-all; }
  .dim-line { color: var(--text-dim); font-size: 12px; }

  .notice {
    border: 1px solid var(--border);
    border-radius: 8px;
    background: var(--bg-elev);
    padding: 20px;
  }
  .notice h3 { margin: 0 0 10px; font-size: 15px; }
  .notice p { color: var(--text-dim); font-size: 13px; margin: 8px 0; }
  .notice pre {
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 5px;
    padding: 8px 12px;
    font-family: var(--mono);
    font-size: 12px;
    color: var(--accent);
  }
  .notice code { font-family: var(--mono); color: var(--accent); }
</style>
