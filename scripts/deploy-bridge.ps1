# Deploy the freshly-built bridge on the dev machine:
#   - kill any running claudewatch instance (frees :7777 + AppData exe lock)
#   - replace the autostarted AppData install with the new production build
#   - launch the debug console build (logs visible) on the configured serial port
#
# Paths are derived from this script's location - no hardcoded username.
# Serial port comes from $env:CLAUDEWATCH_SERIAL_PORT, else config.json, else 'COM9'.
$ErrorActionPreference = 'Continue'

$repoRoot    = Split-Path $PSScriptRoot -Parent
$repoBridge  = Join-Path $repoRoot 'bridge'
$appDataExe  = Join-Path $env:APPDATA 'ClaudeWatch\claudewatch.exe'
$debugExe    = Join-Path $repoBridge 'bin\claudewatch-debug.exe'
$prodExe     = Join-Path $repoBridge 'bin\claudewatch.exe'

# Serial port: env > config.json > default COM9
$port = $env:CLAUDEWATCH_SERIAL_PORT
if (-not $port) {
  $cfg = Join-Path $env:APPDATA 'ClaudeWatch\config.json'
  if (Test-Path $cfg) {
    try { $port = (Get-Content $cfg -Raw | ConvertFrom-Json).serialPort } catch { }
  }
}
if (-not $port) { $port = 'COM9' }

# 1. Persist the chosen serial port as a User env var (autostart + future shells inherit it)
[Environment]::SetEnvironmentVariable('CLAUDEWATCH_SERIAL_PORT', $port, 'User')
Write-Host "serial port: $port (persisted as user env CLAUDEWATCH_SERIAL_PORT)"

# 2. Kill every running claudewatch instance (frees :7777 + AppData exe file lock)
Get-Process claudewatch* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 800
Write-Host "killed old claudewatch instances"

# 3. Replace the autostarted AppData install with the new production build
if (Test-Path $prodExe) {
  Copy-Item $prodExe $appDataExe -Force
  Write-Host "replaced $appDataExe with new build"
}

# 4. Launch the debug console build with the chosen port (new window, logs visible)
if (Test-Path $debugExe) {
  $env:CLAUDEWATCH_SERIAL_PORT = $port
  Start-Process -FilePath $debugExe
  Write-Host "launched $debugExe (port $port)"
} else {
  Write-Host "debug build not found at $debugExe; launch the production exe manually if needed"
}
Start-Sleep -Seconds 3

# 5. Verify it's up and which port it grabbed
try {
  $h = Invoke-RestMethod 'http://127.0.0.1:7777/healthz'
  Write-Host "healthz: $h"
  $logs = Invoke-RestMethod 'http://127.0.0.1:7777/api/logs?limit=8'
  foreach ($e in $logs) { Write-Host ("{0} {1} {2}" -f $e.ts, $e.level, $e.msg) }
} catch {
  Write-Host "verify failed: $($_.Exception.Message)"
}
