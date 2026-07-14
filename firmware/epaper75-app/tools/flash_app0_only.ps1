param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^COM\d+$')]
    [string]$Port,

    [Parameter(Mandatory = $true)]
    [switch]$VerifiedFactoryBackup,

    [ValidateRange(115200, 921600)]
    [int]$Baud = 460800,

    [string]$Esptool = '',
    [string]$AppBinary = '',
    [string]$BackupDirectory = $env:EPAPER75_BACKUP_DIR
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$firmwareRoot = Split-Path -Parent $projectRoot

if (-not $VerifiedFactoryBackup) {
    throw 'Refusing to flash without -VerifiedFactoryBackup.'
}
if ([string]::IsNullOrWhiteSpace($AppBinary)) {
    $AppBinary = Join-Path $projectRoot 'dist\epaper75-p1-app.bin'
}
if (-not (Test-Path -LiteralPath $AppBinary)) {
    throw "P1 application binary not found: $AppBinary"
}
if ((Get-Item -LiteralPath $AppBinary).Length -gt 0x250000) {
    throw 'Application is larger than the app0 partition. Refusing to flash.'
}

$checksumFile = Join-Path $projectRoot 'dist\SHA256SUMS.txt'
if (-not (Test-Path -LiteralPath $checksumFile)) {
    throw "Build checksum manifest not found: $checksumFile"
}
$appName = Split-Path -Leaf $AppBinary
$manifestLine = Get-Content -LiteralPath $checksumFile |
    Where-Object { $_ -match "^([0-9A-Fa-f]{64})\s+$([regex]::Escape($appName))$" } |
    Select-Object -First 1
if ($null -eq $manifestLine) {
    throw "No SHA256 manifest entry for $appName"
}
$expectedAppHash = ($manifestLine -split '\s+')[0].ToUpperInvariant()
$actualAppHash = (Get-FileHash -LiteralPath $AppBinary -Algorithm SHA256).Hash
if ($actualAppHash -ne $expectedAppHash) {
    throw 'Application SHA256 does not match dist\SHA256SUMS.txt.'
}

# Verify that the two independent full-flash rollback images are still present
# and unchanged.  This is stronger than accepting the confirmation switch by
# itself and prevents a stale path from defeating the safety boundary.
if ([string]::IsNullOrWhiteSpace($BackupDirectory)) {
    $BackupDirectory = Join-Path $firmwareRoot 'epaper75-bringup\backups'
}
$backupManifest = Join-Path $BackupDirectory 'SHA256SUMS.txt'
if (-not (Test-Path -LiteralPath $backupManifest)) {
    throw "Factory backup manifest not found: $backupManifest"
}
$verifiedBackupCount = 0
$seenBackupPaths = @{}
foreach ($line in Get-Content -LiteralPath $backupManifest) {
    if ($line -notmatch '^([0-9A-Fa-f]{64})\s+(.+)$') { continue }
    $expectedHash = $Matches[1].ToUpperInvariant()
    $backupPath = Join-Path $BackupDirectory $Matches[2]
    if (-not (Test-Path -LiteralPath $backupPath)) {
        throw "Factory backup listed but missing: $backupPath"
    }
    $resolvedBackupPath = (Resolve-Path -LiteralPath $backupPath).Path
    if ($seenBackupPaths.ContainsKey($resolvedBackupPath)) {
        throw "Factory backup manifest contains a duplicate file entry: $backupPath"
    }
    $seenBackupPaths[$resolvedBackupPath] = $true
    if ((Get-Item -LiteralPath $backupPath).Length -ne 0x800000) {
        throw "Factory backup has an invalid size: $backupPath"
    }
    if ((Get-FileHash -LiteralPath $backupPath -Algorithm SHA256).Hash -ne $expectedHash) {
        throw "Factory backup SHA256 mismatch: $backupPath"
    }
    ++$verifiedBackupCount
}
if ($verifiedBackupCount -lt 2) {
    throw 'At least two verified 8MB factory backups are required.'
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

$imageInfo = (& $Esptool image-info $AppBinary 2>&1 | Out-String)
if ($LASTEXITCODE -ne 0 -or
    $imageInfo -notmatch '(?m)^Detected image type:\s+ESP32\s*$' -or
    $imageInfo -notmatch '(?m)^Flash size:\s+8MB\s*$' -or
    $imageInfo -notmatch '(?m)^Flash freq:\s+40m\s*$' -or
    $imageInfo -notmatch '(?m)^Flash mode:\s+DIO\s*$') {
    throw 'Application image is not the expected ESP32 / 8MB / 40MHz / DIO format.'
}

$flashInfo = (& $Esptool --chip esp32 --port $Port flash-id 2>&1 | Out-String)
Write-Host $flashInfo
if ($LASTEXITCODE -ne 0 -or $flashInfo -notmatch '(?i)(8MB|64Mbit)') {
    throw 'The attached device was not positively identified as an ESP32 with 8MB flash.'
}

Write-Host "Writing app0 only: $AppBinary"
Write-Host "SHA256: $actualAppHash"
Write-Host "Verified factory backups: $verifiedBackupCount"

# Deliberately leave bootloader, partition table, NVS, otadata, app1 and SPIFFS
# untouched.  The verified factory backup remains the rollback source.
& $Esptool --chip esp32 --port $Port --baud $Baud write-flash --flash-mode dio --flash-freq 40m --flash-size 8MB 0x10000 $AppBinary
if ($LASTEXITCODE -ne 0) {
    throw 'P1 app0 flash failed.'
}

Write-Host 'P1 app0 write completed.'
