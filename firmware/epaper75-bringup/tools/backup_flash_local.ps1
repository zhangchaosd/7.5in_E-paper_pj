param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM\d+$')]
    [string]$Port,

    [ValidateRange(115200, 921600)]
    [int]$Baud = 460800,

    [string]$Esptool = '',

    [string]$BackupDirectory = $env:EPAPER75_BACKUP_DIR
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BackupDirectory)) {
    $BackupDirectory = Join-Path $projectRoot 'backups'
}

if ([string]::IsNullOrWhiteSpace($Esptool)) {
    $toolsRoot = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\esp32\tools\esptool_py'
    $candidate = Get-ChildItem -LiteralPath $toolsRoot -Recurse -Filter 'esptool.exe' -File -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if ($null -eq $candidate) {
        throw "Unable to locate esptool.exe below $toolsRoot"
    }
    $Esptool = $candidate.FullName
}

if (-not (Test-Path -LiteralPath $Esptool)) {
    throw "esptool not found: $Esptool"
}

$chipInfo = (& $Esptool --chip esp32 --port $Port chip-id 2>&1 | Out-String)
Write-Host $chipInfo
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to identify ESP32. Check the power switch, USB data cable and automatic download circuit.'
}

$flashInfo = (& $Esptool --chip esp32 --port $Port flash-id 2>&1 | Out-String)
Write-Host $flashInfo
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to identify SPI flash.'
}
if ($flashInfo -notmatch '(?i)(8MB|64Mbit)') {
    throw 'Flash was not positively identified as 8MB. Refusing to read a presumed 8MB range.'
}

New-Item -ItemType Directory -Force -Path $BackupDirectory | Out-Null
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outputPath = Join-Path $BackupDirectory "factory-original-$timestamp-$Port-8mb.bin"
if (Test-Path -LiteralPath $outputPath) {
    throw "Refusing to overwrite existing backup: $outputPath"
}

& $Esptool --chip esp32 --port $Port --baud $Baud read-flash 0x000000 0x800000 $outputPath
if ($LASTEXITCODE -ne 0) {
    throw 'Flash backup failed.'
}

$file = Get-Item -LiteralPath $outputPath
if ($file.Length -ne 8388608) {
    throw "Backup size is $($file.Length) bytes, expected 8388608. Do not flash the device."
}

$hash = Get-FileHash -LiteralPath $outputPath -Algorithm SHA256
$manifestPath = Join-Path $BackupDirectory 'SHA256SUMS.txt'
Add-Content -LiteralPath $manifestPath -Value "$($hash.Hash)  $($file.Name)" -Encoding ascii
Write-Host "Backup complete: $outputPath"
Write-Host "Size: $($file.Length) bytes"
Write-Host "SHA256: $($hash.Hash)"
