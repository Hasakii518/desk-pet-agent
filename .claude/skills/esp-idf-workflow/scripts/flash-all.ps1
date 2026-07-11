# ESP-IDF Build + Flash + Monitor (One-Click)
# Usage:
#   powershell -File flash-all.ps1                           # Build + Flash + Monitor (auto-detect port)
#   powershell -File flash-all.ps1 -Port COM3                # Specify port
#   powershell -File flash-all.ps1 -BuildOnly                # Build only
#   powershell -File flash-all.ps1 -NoMonitor                # Build + Flash, no monitor
#   powershell -File flash-all.ps1 -MonitorOnly -Port COM3   # Monitor only (no build/flash)
#   powershell -File flash-all.ps1 -Clean                    # fullclean before building
#
# Note: idf.py flash automatically builds the project first, so there's no need
# to run idf.py build separately in the default flow.

param(
    [string]$Port,           # COM port (auto-detect if not specified)
    [switch]$BuildOnly,      # Only build, no flash
    [switch]$NoMonitor,      # Skip serial monitor after flash
    [switch]$MonitorOnly,    # Only monitor (no build/flash)
    [switch]$Clean           # Run fullclean before build
)

$IDF_PATH = "C:\esp\v5.5.4\esp-idf"

# Check if IDF exists
if (-not (Test-Path "$IDF_PATH\export.ps1")) {
    Write-Error "ESP-IDF not found at $IDF_PATH"
    Write-Host "Please update `$IDF_PATH in this script to match your installation."
    exit 1
}

# Activate IDF environment
Write-Host "Activating ESP-IDF environment..." -ForegroundColor Cyan
. "$IDF_PATH\export.ps1" | Out-Null

# Verify project
if (-not (Test-Path "CMakeLists.txt")) {
    Write-Error "Not an ESP-IDF project directory. CMakeLists.txt not found."
    exit 1
}

$ProjectDir = Get-Location
Write-Host "Project: $ProjectDir" -ForegroundColor Cyan

# ---- Monitor Only Mode ----
if ($MonitorOnly) {
    if (-not $Port) { $Port = & "$PSScriptRoot\detect-port.ps1" }
    if (-not $Port) { Write-Error "Cannot determine port for monitor."; exit 1 }
    Write-Host "Starting monitor on $Port..." -ForegroundColor Green
    idf.py -p $Port monitor
    exit 0
}

# ---- Clean (optional) ----
if ($Clean) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    idf.py fullclean
}

# ---- Build Only Mode ----
if ($BuildOnly) {
    Write-Host "Building firmware..." -ForegroundColor Cyan
    idf.py build
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed! Check the output above for errors."
        exit $LASTEXITCODE
    }
    Write-Host "Build succeeded!" -ForegroundColor Green
    exit 0
}

# ---- Flash (auto-builds first, per official ESP-IDF behavior) ----
# Auto-detect port if not specified
if (-not $Port) {
    Write-Host "Auto-detecting ESP32 serial port..." -ForegroundColor Cyan
    $Port = & "$PSScriptRoot\detect-port.ps1"
    if (-not $Port) {
        Write-Error "Cannot auto-detect port. Please specify with -Port COMx"
        Write-Host ""
        Write-Host "To list available ports:"
        Write-Host "  powershell -File scripts\detect-port.ps1 -ListAll"
        exit 1
    }
}

# flash 命令会自动先构建再烧录（官方行为），无需单独 build
Write-Host "Building & flashing to $Port..." -ForegroundColor Cyan
idf.py -p $Port flash

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build or flash failed! Check the output above for errors."
    Write-Host ""
    Write-Host "Troubleshooting tips:"
    Write-Host "  1. Check if another program is using $Port (serial monitor, Arduino IDE, etc.)"
    Write-Host "  2. Hold BOOT button, press RST, then release BOOT"
    Write-Host "  3. Try a different USB cable (some are power-only)"
    Write-Host "  4. Run 'idf.py -p $Port erase-flash' to clear corrupted flash"
    exit $LASTEXITCODE
}
Write-Host "Flash succeeded!" -ForegroundColor Green

# ---- Monitor ----
if (-not $NoMonitor) {
    Write-Host "Starting serial monitor (Ctrl+] to exit)..." -ForegroundColor Cyan
    Write-Host "---" -ForegroundColor DarkGray
    idf.py -p $Port monitor
}

Write-Host "Done!" -ForegroundColor Green
