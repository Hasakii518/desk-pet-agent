# install.ps1 — Windows 本体安装 claudewatch agent
#
# 用法: 在 Windows PowerShell (普通用户) 中执行
#   powershell -ExecutionPolicy Bypass -File install.ps1
#
# 功能:
#   1. 部署 claudewatch.exe 到 %APPDATA%\ClaudeWatch\
#   2. 生成/复用 token
#   3. 添加防火墙规则 (允许 WSL 子网入站 7777)
#   4. 添加开机自启 (启动文件夹快捷方式)
#   5. 同步 token 到 WSL 文件系统 (供 probe 读取)

$ErrorActionPreference = 'Stop'

$InstallDir = Join-Path $env:APPDATA 'ClaudeWatch'
$ExePath = Join-Path $InstallDir 'claudewatch.exe'
$TokenPath = Join-Path $InstallDir 'token'
$Port = 7777

# WSL 路径 (Ubuntu + 当前用户)
$WslDistro = 'Ubuntu'
$WslUser = 'shuohan'
$WslTokenPath = "\\wsl$\$WslDistro\home\$WslUser\.config\claudewatch\token"

function Write-OK { param([string]$msg) Write-Host "[OK] $msg" -ForegroundColor Green }
function Write-Warn2 { param([string]$msg) Write-Host "[!]  $msg" -ForegroundColor Yellow }
function Write-Err2 { param([string]$msg) Write-Host "[X]  $msg" -ForegroundColor Red }

Write-Host '=== ClaudeWatch agent install ===' -ForegroundColor Cyan

# 1. 部署 exe
$repoExe = Join-Path $PSScriptRoot '..\bin\claudewatch.exe'
if (-not (Test-Path $repoExe)) {
    $wslExe = "\\wsl$\$WslDistro\home\$WslUser\projects\personal\claudewatch\bin\claudewatch.exe"
    if (Test-Path $wslExe) {
        $repoExe = $wslExe
    } else {
        Write-Err2 'claudewatch.exe not found'
        Write-Host 'Please run in WSL first: make agent-windows'
        exit 1
    }
}

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Copy-Item $repoExe $ExePath -Force
Write-OK "agent deployed: $ExePath"

# 2. 生成 token (若不存在)
if (-not (Test-Path $TokenPath)) {
    $bytes = New-Object byte[] 32
    [System.Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    $token = ($bytes | ForEach-Object { $_.ToString('x2') }) -join ''
    Set-Content -Path $TokenPath -Value $token -NoNewline
    Write-OK 'new token generated'
} else {
    Write-OK 'reuse existing token'
}

# 3. 防火墙：不需要专门规则
# Windows 对 WSL vEthernet (Hyper-V 虚拟交换机) 流量内置放行，WSL → Windows 入站不挡。
# 靠 token 强鉴权防护。如需额外加固，管理员可手动加规则：
#   New-NetFirewallRule -DisplayName 'ClaudeWatch WSL' -Direction Inbound -LocalPort $Port -Protocol TCP -Action Allow -RemoteAddress 192.168.160.0/20 -Profile Any
Write-OK "firewall: relying on WSL vEthernet built-in passthrough + token auth"

# 4. 开机自启 (启动文件夹快捷方式)
$StartupDir = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Startup'
$LnkPath = Join-Path $StartupDir 'ClaudeWatch.lnk'
$shell = New-Object -ComObject WScript.Shell
$lnk = $shell.CreateShortcut($LnkPath)
$lnk.TargetPath = $ExePath
$lnk.Arguments = "--addr 0.0.0.0:$Port"
$lnk.WindowStyle = 7
$lnk.WorkingDirectory = $InstallDir
$lnk.Save()
Write-OK "startup shortcut: $LnkPath"

# 5. 同步 token 到 WSL
$wslCfgDir = "\\wsl$\$WslDistro\home\$WslUser\.config\claudewatch"
New-Item -ItemType Directory -Force -Path $wslCfgDir | Out-Null
Copy-Item $TokenPath $WslTokenPath -Force
Write-OK "token synced to WSL: $WslTokenPath"

# 6. 立即启动 agent (若未运行)
$running = Get-Process -Name 'claudewatch' -ErrorAction SilentlyContinue
if (-not $running) {
    $token = (Get-Content $TokenPath).Trim()
    # 探测默认 WSL distro 名，传给 agent 用于 doctor UNC 访问
    $distro = $null
    try {
        $raw = wsl.exe -l -q 2>$null
        $distros = $raw | ForEach-Object { ($_ -replace "`0","").Trim() } | Where-Object { $_ -ne '' }
        $distro = $distros | Select-Object -First 1
    } catch {}
    $args = @('--addr', "0.0.0.0:$Port", '--token', $token)
    if ($distro) {
        $args += @('--wsl-distro', $distro)
        Write-OK "detected WSL distro: $distro"
    } else {
        Write-Warn2 "could not detect WSL distro; doctor UNC checks will be skipped"
    }
    Start-Process -FilePath $ExePath `
        -ArgumentList $args `
        -WindowStyle Hidden `
        -RedirectStandardError (Join-Path $InstallDir 'agent.err') `
        -RedirectStandardOutput (Join-Path $InstallDir 'agent.out')
    Start-Sleep -Seconds 1
    Write-OK 'agent started'
} else {
    Write-OK "agent already running (PID $($running.Id))"
}

Write-Host ''
Write-Host '=== done ===' -ForegroundColor Cyan
Write-Host "install dir: $InstallDir"
Write-Host "port:        $Port"
Write-Host "token:       $TokenPath"
Write-Host "logs:        $InstallDir\agent.out and agent.err"
Write-Host ''
Write-Host 'Next: run install.sh inside WSL to set up the probe, then open http://127.0.0.1:7777'
