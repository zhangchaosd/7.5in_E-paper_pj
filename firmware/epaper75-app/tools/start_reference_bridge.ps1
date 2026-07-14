param(
    [string]$BridgeRoot = $env:EPAPER75_REFERENCE_BRIDGE_ROOT,

    [ValidateRange(1, 65535)]
    [int]$Port = 8765
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($BridgeRoot)) {
    throw 'Pass -BridgeRoot or set EPAPER75_REFERENCE_BRIDGE_ROOT to an external esp8266-ai checkout.'
}

$bridgeProject = Join-Path $BridgeRoot 'windows-app\AIClockBridge'
$bridgeExe = Get-ChildItem -LiteralPath (Join-Path $bridgeProject 'bin\Release') `
    -Recurse -Filter 'AIClockBridge.exe' -File -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\publish\\' } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if ($null -eq $bridgeExe) {
    throw "Reference Bridge has not been built in Release mode: $bridgeProject"
}

$listener = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($null -ne $listener) {
    $existing = Get-Process -Id $listener.OwningProcess -ErrorAction SilentlyContinue
    try {
        $status = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/status" -TimeoutSec 5
        if ($null -eq $status.ts -or $null -eq $status.claude -or $null -eq $status.codex) {
            throw 'Unexpected status schema.'
        }
    } catch {
        throw "Port $Port belongs to PID $($listener.OwningProcess) but does not provide the Bridge /status contract."
    }
    Write-Host "Reference Bridge is already listening (PID $($listener.OwningProcess), process $($existing.ProcessName))."
    exit 0
}

$process = Start-Process -FilePath $bridgeExe.FullName -WorkingDirectory $bridgeExe.DirectoryName `
    -WindowStyle Hidden -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds(10)
do {
    Start-Sleep -Milliseconds 250
    $listener = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue |
        Where-Object { $_.OwningProcess -eq $process.Id } |
        Select-Object -First 1
} while ($null -eq $listener -and [DateTime]::UtcNow -lt $deadline -and
         -not $process.HasExited)

if ($process.HasExited) {
    throw "Reference Bridge exited with code $($process.ExitCode)."
}
if ($null -eq $listener) {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    throw "Reference Bridge did not listen on port $Port within 10 seconds."
}

$status = Invoke-RestMethod -Uri "http://127.0.0.1:$Port/status" -TimeoutSec 5
Write-Host "Reference Bridge started: PID $($process.Id), port $Port"
Write-Host "Claude=$($status.claude.status), Codex=$($status.codex.status)"
