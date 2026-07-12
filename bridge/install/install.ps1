# install.ps1 - ClaudeWatch bridge installer (Windows host)
#
# Usage (normal user PowerShell):
#   powershell -ExecutionPolicy Bypass -File install.ps1
#   powershell -ExecutionPolicy Bypass -File install.ps1 -ProbeTarget wsl
#   powershell -ExecutionPolicy Bypass -File install.ps1 -ProbeTarget windows
#   powershell -ExecutionPolicy Bypass -File install.ps1 -ClaudeConfigDir "C:\Users\armstrong\.claude"
#
# What it does:
#   1. Deploy agent (claudewatch.exe) to %APPDATA%\ClaudeWatch\, generate token, write config.json
#   2. Add Startup shortcut (autostart on login)
#   3. Install probe (auto-detect prefers WSL when available), register Claude Code hooks
#   4. Start the agent
#
# No hardcoded username / distro name; all paths are derived from the current user.

[CmdletBinding()]
param(
  [ValidateSet('auto', 'windows', 'wsl')]
  [string]$ProbeTarget = 'auto',
  [string]$ClaudeConfigDir = ''
)

$ErrorActionPreference = 'Stop'

$User       = $env:USERNAME
$HomeDir       = $env:USERPROFILE
$InstallDir = Join-Path $env:APPDATA 'ClaudeWatch'
$ExePath    = Join-Path $InstallDir 'claudewatch.exe'
$TokenPath  = Join-Path $InstallDir 'token'
$ConfigPath = Join-Path $InstallDir 'config.json'
$Port       = 7777
$RepoBin    = Join-Path $PSScriptRoot '..\bin'

function Write-OK   { param([string]$m) Write-Host "[OK] $m" -ForegroundColor Green }
function Write-Warn { param([string]$m) Write-Host "[!]  $m" -ForegroundColor Yellow }
function Write-Err  { param([string]$m) Write-Host "[X]  $m" -ForegroundColor Red }

# Windows path -> WSL path (C:\a\b -> /mnt/c/a/b)
function ConvertTo-WslPath($windowsPath) {
  $p = [string]$windowsPath
  $p = $p.Replace('\', '/')
  if ($p -match '^([A-Za-z]):(.*)$') {
    return '/mnt/' + $Matches[1].ToLower() + $Matches[2]
  }
  return $p
}

# Register Claude Code hooks (native Windows)
function Register-ClaudeHooks {
  param(
    [string]$ProbePath,
    [string]$ClaudeConfigDir = ''
  )

  if ($ClaudeConfigDir) {
    $claudeDir = $ClaudeConfigDir
  } else {
    $claudeDir = Join-Path $HomeDir '.claude'
    if (Test-Path (Join-Path $HomeDir '.tme-claude')) { $claudeDir = Join-Path $HomeDir '.tme-claude' }
  }
  $settings = Join-Path $claudeDir 'settings.json'
  New-Item -ItemType Directory -Force -Path $claudeDir | Out-Null
  if (-not (Test-Path $settings)) { Set-Content -Path $settings -Value '{}' -NoNewline }
  Copy-Item $settings "$settings.bak" -Force

  $hookTypes = @('PostToolUse', 'UserPromptSubmit', 'Stop', 'SubagentStop', 'Notification', 'SessionStart', 'SessionEnd', 'PreCompact')
  $cmd = """$ProbePath"""
  $json = Get-Content $settings -Raw | ConvertFrom-Json
  if (-not (Get-Member -InputObject $json -Name 'hooks' -MemberType NoteProperty)) {
    $json | Add-Member -NotePropertyName 'hooks' -NotePropertyValue ([PSCustomObject]@{})
  }
  $changed = $false
  foreach ($ht in $hookTypes) {
    if (-not (Get-Member -InputObject $json.hooks -Name $ht -MemberType NoteProperty)) {
      $json.hooks | Add-Member -NotePropertyName $ht -NotePropertyValue @()
    }
    $has = $false
    foreach ($entry in $json.hooks.$ht) {
      if ($entry.hooks) {
        foreach ($h in $entry.hooks) {
          if ($h.command -and $h.command -like '*claudewatch-probe*') { $has = $true }
        }
      }
    }
    if (-not $has) {
      $newEntry = [PSCustomObject]@{ matcher = '*'; hooks = @([PSCustomObject]@{ type = 'command'; command = "$cmd $ht" }) }
      $json.hooks.$ht += $newEntry
      $changed = $true
      Write-OK "  + $ht"
    }
  }
  if ($changed) {
    $json | ConvertTo-Json -Depth 10 | Set-Content -Path $settings -NoNewline
    Write-OK "hooks written: $settings"
  } else {
    Write-OK "hooks already up to date"
  }
}

Write-Host "=== ClaudeWatch install (user: $User) ===" -ForegroundColor Cyan

# 1. Deploy agent
$repoExe = Join-Path $RepoBin 'claudewatch.exe'
if (-not (Test-Path $repoExe)) {
  Write-Err "claudewatch.exe not found, run 'make release' first"
  exit 1
}
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item $repoExe $ExePath -Force
Write-OK "agent deployed: $ExePath"

# 2. token (generate + persist if missing; agent and probe share this file)
if (-not (Test-Path $TokenPath)) {
  $bytes = New-Object byte[] 32
  [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
  $token = ($bytes | ForEach-Object { $_.ToString('x2') }) -join ''
  Set-Content -Path $TokenPath -Value $token -NoNewline
  Write-OK "generated new token"
} else {
  Write-OK "reusing existing token"
}

# 3. config.json (token is NOT written into config to avoid plaintext leakage; agent reads the token file)
$config = @{ addr = ":$Port"; serialPort = 'auto' } | ConvertTo-Json -Compress
Set-Content -Path $ConfigPath -Value $config -NoNewline
Write-OK "config written: $ConfigPath"

# 4. Startup shortcut (autostart, passes --config)
$StartupDir = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Startup'
$LnkPath = Join-Path $StartupDir 'ClaudeWatch.lnk'
$shell = New-Object -ComObject WScript.Shell
$lnk = $shell.CreateShortcut($LnkPath)
$lnk.TargetPath = $ExePath
$lnk.Arguments = "--config `"$ConfigPath`""
$lnk.WindowStyle = 7
$lnk.WorkingDirectory = $InstallDir
$lnk.Save()
Write-OK "autostart: $LnkPath"

# 5. probe target detection
$probeTarget = $ProbeTarget
if ($probeTarget -eq 'auto') {
  $wslOk = $false
  try {
    wsl.exe -l -q 2>$null | Out-Null
    if ($LASTEXITCODE -eq 0) { $wslOk = $true }
  } catch { $wslOk = $false }
  # prefer WSL when available (Claude Code commonly runs there); override with -ProbeTarget windows
  if ($wslOk) { $probeTarget = 'wsl' } else { $probeTarget = 'windows' }
}
Write-Host "probe target: $probeTarget"

if ($probeTarget -eq 'wsl') {
  $distro = $null
  try {
    $raw = wsl.exe -l -q 2>$null
    $distros = $raw | ForEach-Object { ($_ -replace "`0", "").Trim() } | Where-Object { $_ -ne '' }
    $distro = $distros | Select-Object -First 1
  } catch { }
  if (-not $distro) { Write-Err "no WSL distro detected, cannot install probe"; exit 1 }
  Write-OK "WSL distro: $distro"
  $repoBridgeWsl = ConvertTo-WslPath (Split-Path $PSScriptRoot -Parent)
  $wslCmd = "cd '$repoBridgeWsl' && ./install/install.sh"
  if ($ClaudeConfigDir) {
    $wslCfg = ConvertTo-WslPath $ClaudeConfigDir
    $wslCmd = "export CLAUDE_CONFIG_DIR='$wslCfg'; $wslCmd"
  }
  Write-Host "running install.sh inside WSL($distro) ..." -ForegroundColor Cyan
  wsl.exe -d $distro -- bash -c $wslCmd
  if ($LASTEXITCODE -ne 0) {
    Write-Warn "install.sh inside WSL returned non-zero. You can run it manually in WSL: cd <repo>/bridge && ./install/install.sh"
  } else {
    Write-OK "WSL probe + hooks installed"
  }
} else {
  $probeBin = Join-Path $RepoBin 'claudewatch-probe.exe'
  if (-not (Test-Path $probeBin)) {
    Write-Err "claudewatch-probe.exe not found, run 'make release' first"
    exit 1
  }
  $probeDir = Join-Path $env:LOCALAPPDATA 'ClaudeWatch'
  New-Item -ItemType Directory -Force -Path $probeDir | Out-Null
  $probePath = Join-Path $probeDir 'claudewatch-probe.exe'
  Copy-Item $probeBin $probePath -Force
  Write-OK "probe deployed: $probePath"
  # token lives in %APPDATA%\ClaudeWatch\token (same place as agent); probe reads it directly, no copy needed
  Register-ClaudeHooks -ProbePath $probePath -ClaudeConfigDir $ClaudeConfigDir
}

# 6. Start agent (if not running)
$running = Get-Process -Name 'claudewatch' -ErrorAction SilentlyContinue
if (-not $running) {
  Start-Process -FilePath $ExePath -ArgumentList "--config `"$ConfigPath`"" -WindowStyle Hidden `
    -RedirectStandardError (Join-Path $InstallDir 'agent.err') `
    -RedirectStandardOutput (Join-Path $InstallDir 'agent.out')
  Start-Sleep -Seconds 1
  Write-OK "agent started"
} else {
  Write-OK "agent already running (PID $($running.Id))"
}

Write-Host ''
Write-Host '=== done ===' -ForegroundColor Cyan
Write-Host "open in browser: http://127.0.0.1:$Port"
