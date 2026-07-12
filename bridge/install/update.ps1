# update.ps1 - upgrade the deployed claudewatch agent
#
# Usage: run in Windows PowerShell
#   powershell -ExecutionPolicy Bypass -File install/update.ps1
#
# Flow: taskkill forced kill -> copy new exe (fall back to rename trick if locked) -> re-run install.ps1 to start new agent

$ErrorActionPreference = 'Continue'

Write-Host '=== ClaudeWatch agent upgrade ===' -ForegroundColor Cyan

# 1. taskkill all claudewatch.exe (cross-session)
$kill = & taskkill /F /IM claudewatch.exe 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "[OK] killed running agent(s)"
    Start-Sleep -Seconds 2
} else {
    Write-Host "[OK] no running agent (taskkill exit $LASTEXITCODE)"
}

# 2. copy new exe from repo bin/ (path derived from script location, not hardcoded)
$src = Join-Path $PSScriptRoot '..\bin\claudewatch.exe'
$dst = Join-Path $env:APPDATA 'ClaudeWatch\claudewatch.exe'
if (-not (Test-Path $src)) {
    Write-Host "[X]  source not found: $src" -ForegroundColor Red
    Write-Host '     build first: make agent-windows'
    exit 1
}

# 2a. direct copy
$copied = $false
try {
    Copy-Item $src $dst -Force -ErrorAction Stop
    Write-Host "[OK] copied new exe"
    $copied = $true
} catch {
    Write-Host "[!]  direct copy failed (file locked), trying rename trick..."
    # 2b. rename the running exe to .old, then copy the new one
    $old = "$dst.old"
    if (Test-Path $old) { Remove-Item $old -Force -ErrorAction SilentlyContinue }
    try {
        Rename-Item $dst $old -ErrorAction Stop
        Copy-Item $src $dst -Force -ErrorAction Stop
        Write-Host "[OK] renamed old exe to .old and copied new one"
        $copied = $true
        # try to delete .old (may still be locked; ignore failure)
        Start-Sleep -Seconds 1
        Remove-Item $old -Force -ErrorAction SilentlyContinue
    } catch {
        Write-Host "[X]  rename+copy also failed: $_" -ForegroundColor Red
        exit 1
    }
}

if (-not $copied) { exit 1 }

# 3. re-run install.ps1 (reuse token / firewall / shortcut, start new agent)
Write-Host ''
& "$PSScriptRoot\install.ps1"
