# Deploy the freshly-built bridge: kill stale autostarted instance, replace the
# AppData install (so next reboot autostarts the new code), force COM9, launch
# the debug console build so logs are visible.
$ErrorActionPreference = 'Continue'

$repoBridge = 'C:\Users\armstrong\WorkBuddy\demo\desk-pet-agent\bridge'
$appDataExe = "$env:APPDATA\ClaudeWatch\claudewatch.exe"
$debugExe   = "$repoBridge\bin\claudewatch-debug.exe"
$prodExe    = "$repoBridge\bin\claudewatch.exe"

# 1. Persist COM9 as a User env var so autostart (and future shells) inherit it.
[Environment]::SetEnvironmentVariable('CLAUDEWATCH_SERIAL_PORT', 'COM9', 'User')
Write-Host "set user env CLAUDEWATCH_SERIAL_PORT=COM9"

# 2. Kill every running claudewatch instance (frees :7777 + AppData exe file lock).
Get-Process claudewatch* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 800
Write-Host "killed old claudewatch instances"

# 3. Replace the autostarted AppData install with the new production build.
if (Test-Path $prodExe) {
    Copy-Item $prodExe $appDataExe -Force
    Write-Host "replaced $appDataExe with new build"
}

# 4. Launch the debug console build with COM9 env (new window, logs visible).
$env:CLAUDEWATCH_SERIAL_PORT = 'COM9'
Start-Process -FilePath $debugExe
Write-Host "launched $debugExe (COM9)"
Start-Sleep -Seconds 3

# 5. Verify it's up and which port it grabbed.
try {
    $h = Invoke-RestMethod 'http://127.0.0.1:7777/healthz'
    Write-Host "healthz: $h"
    $logs = Invoke-RestMethod 'http://127.0.0.1:7777/api/logs?limit=8'
    foreach ($e in $logs) { Write-Host ("{0} {1} {2}" -f $e.ts, $e.level, $e.msg) }
} catch {
    Write-Host "verify failed: $($_.Exception.Message)"
}
