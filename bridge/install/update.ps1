# update.ps1 — 升级已部署的 claudewatch agent
#
# 用法: 在 Windows PowerShell 中执行
#   powershell -ExecutionPolicy Bypass -File install/update.ps1
#
# 流程: taskkill 强杀 → 拷新 exe (失败则 rename 旧的再拷) → 重跑 install.ps1 起新 agent

$ErrorActionPreference = 'Continue'

Write-Host '=== ClaudeWatch agent upgrade ===' -ForegroundColor Cyan

# 1. taskkill 强杀所有 claudewatch.exe (跨会话)
$kill = & taskkill /F /IM claudewatch.exe 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "[OK] killed running agent(s)"
    Start-Sleep -Seconds 2
} else {
    Write-Host "[OK] no running agent (taskkill exit $LASTEXITCODE)"
}

# 2. 从 WSL 拷新 exe
$src = "\\wsl$\Ubuntu-24.04\home\tencent_go\projects\personal\claudewatch\bin\claudewatch.exe"
$dst = Join-Path $env:APPDATA 'ClaudeWatch\claudewatch.exe'
if (-not (Test-Path $src)) {
    Write-Host "[X]  source not found: $src" -ForegroundColor Red
    Write-Host '     build first in WSL: make agent-windows'
    exit 1
}

# 2a. 直接拷
$copied = $false
try {
    Copy-Item $src $dst -Force -ErrorAction Stop
    Write-Host "[OK] copied new exe"
    $copied = $true
} catch {
    Write-Host "[!]  direct copy failed (file locked), trying rename trick..."
    # 2b. rename 运行中的 exe 到 .old，再拷新的
    $old = "$dst.old"
    if (Test-Path $old) { Remove-Item $old -Force -ErrorAction SilentlyContinue }
    try {
        Rename-Item $dst $old -ErrorAction Stop
        Copy-Item $src $dst -Force -ErrorAction Stop
        Write-Host "[OK] renamed old exe to .old and copied new one"
        $copied = $true
        # 尝试删 .old (可能仍被占用，失败就算了)
        Start-Sleep -Seconds 1
        Remove-Item $old -Force -ErrorAction SilentlyContinue
    } catch {
        Write-Host "[X]  rename+copy also failed: $_" -ForegroundColor Red
        exit 1
    }
}

if (-not $copied) { exit 1 }

# 3. 重跑 install.ps1 (复用 token/防火墙/快捷方式，启动新 agent)
Write-Host ''
& "$PSScriptRoot\install.ps1"
