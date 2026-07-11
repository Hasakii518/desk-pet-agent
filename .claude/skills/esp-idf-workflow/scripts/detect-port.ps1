# ESP32 Serial Port Detector
# Scans available COM ports and identifies likely ESP32 devices
# Usage: powershell -File detect-port.ps1
# Returns: Single COM port string (e.g., "COM3"), or exits with error if none found

param(
    [switch]$ListAll  # Show all ports instead of auto-selecting
)

$ports = [System.IO.Ports.SerialPort]::GetPortNames()

if (-not $ports -or $ports.Count -eq 0) {
    Write-Error "No serial ports detected. Please check USB connection."
    exit 1
}

if ($ListAll) {
    Write-Host "Available serial ports:"
    foreach ($port in $ports) {
        Write-Host "  $port"
    }

    # Try to get detailed info
    $details = Get-CimInstance Win32_PnPEntity | Where-Object {
        $_.Name -match 'COM|Serial|CP210|CH340|CH341|FTDI|Silicon|USB'
    }
    if ($details) {
        Write-Host "`nDetailed device info:"
        foreach ($d in $details) {
            Write-Host "  [$($d.DeviceID)] $($d.Name)"
        }
    }
    exit 0
}

# Auto-detect: prefer ESP32 common USB-UART chips
$knownChips = @('CP210', 'CH340', 'CH341', 'FTDI', 'Silicon', 'ESP32')
$espPort = $null

# Try to match by device description
$allDevices = Get-CimInstance Win32_PnPEntity | Where-Object {
    $_.Name -match 'COM\d+'
}

foreach ($chip in $knownChips) {
    $match = $allDevices | Where-Object { $_.Name -match $chip } | Select-Object -First 1
    if ($match -and $match.Name -match '\((COM\d+)\)') {
        $espPort = $Matches[1]
        Write-Host "Detected ESP32 device ($chip): $espPort"
        break
    }
}

# Fallback: if only one port exists, use it
if (-not $espPort) {
    if ($ports.Count -eq 1) {
        $espPort = $ports[0]
        Write-Host "Only one serial port found, using: $espPort"
    } else {
        Write-Host "WARNING: Multiple ports found, cannot auto-detect ESP32."
        Write-Host "Available ports: $($ports -join ', ')"
        Write-Host ""
        Write-Host "Please specify the port manually. To list details, run:"
        Write-Host "  powershell -File detect-port.ps1 -ListAll"
        exit 1
    }
}

# Output just the port name for script consumption
Write-Output $espPort
