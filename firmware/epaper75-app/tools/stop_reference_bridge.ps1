param(
    [ValidateRange(1, 65535)]
    [int]$Port = 8765
)

$ErrorActionPreference = 'Stop'
$listener = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($null -eq $listener) {
    Write-Host "Nothing is listening on port $Port."
    exit 0
}

$process = Get-Process -Id $listener.OwningProcess -ErrorAction Stop
if ($process.ProcessName -ne 'AIClockBridge') {
    throw "Port $Port belongs to $($process.ProcessName), not AIClockBridge; refusing to stop it."
}

Stop-Process -Id $process.Id
Write-Host "Stopped AIClockBridge PID $($process.Id)."
