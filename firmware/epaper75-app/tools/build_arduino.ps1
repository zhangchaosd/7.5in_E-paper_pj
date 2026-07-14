param(
    [string]$ArduinoCli = '',
    [string]$LibrariesDirectory = '',
    [string]$BuildDirectory = ''
)

$ErrorActionPreference = 'Stop'
$sketchDirectory = Split-Path -Parent $PSScriptRoot
$distDirectory = Join-Path $sketchDirectory 'dist'
$includeDirectory = (Join-Path $sketchDirectory 'include').Replace('\', '/')
$fqbn = 'esp32:esp32:esp32:FlashSize=8M,PartitionScheme=custom,PSRAM=disabled,FlashMode=dio,FlashFreq=40,EraseFlash=none,DebugLevel=info'

if ([string]::IsNullOrWhiteSpace($ArduinoCli)) {
    $command = Get-Command 'arduino-cli' -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        $ArduinoCli = $command.Source
    } elseif ($env:OS -eq 'Windows_NT') {
        $bundledCli = Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
        if (Test-Path -LiteralPath $bundledCli) {
            $ArduinoCli = $bundledCli
        }
    }
}
if ([string]::IsNullOrWhiteSpace($ArduinoCli) -or -not (Test-Path -LiteralPath $ArduinoCli)) {
    throw 'Arduino CLI not found. Run tools\setup_arduino.ps1 or pass -ArduinoCli.'
}

if ([string]::IsNullOrWhiteSpace($LibrariesDirectory)) {
    $documents = [Environment]::GetFolderPath('MyDocuments')
    $LibrariesDirectory = Join-Path $documents 'Arduino\libraries'
}
if (-not (Test-Path -LiteralPath $LibrariesDirectory)) {
    throw "Arduino libraries directory not found: $LibrariesDirectory"
}

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path ([IO.Path]::GetTempPath()) 'epaper75-app-build'
}

$coreList = (& $ArduinoCli core list 2>$null | Out-String)
if ($coreList -notmatch '(?m)^esp32:esp32\s+3\.3\.10\s') {
    throw 'The normative build requires installed esp32:esp32 core 3.3.10.'
}

$requiredLibraries = [ordered]@{
    'GxEPD2' = '1.6.9'
    'Adafruit_GFX_Library' = '1.12.6'
    'Adafruit_BusIO' = '1.17.4'
    'ArduinoJson' = '7.4.3'
    'WiFiManager' = '2.0.17'
}
foreach ($libraryName in $requiredLibraries.Keys) {
    $propertiesPath = Join-Path (Join-Path $LibrariesDirectory $libraryName) 'library.properties'
    if (-not (Test-Path -LiteralPath $propertiesPath)) {
        throw "Required Arduino library is missing: $libraryName"
    }
    $versionLine = Get-Content -LiteralPath $propertiesPath |
        Where-Object { $_ -match '^version=(.+)$' } |
        Select-Object -First 1
    $actualVersion = if ($null -eq $versionLine) { '(unknown)' } else { ($versionLine -replace '^version=', '').Trim() }
    if ($actualVersion -ne $requiredLibraries[$libraryName]) {
        throw "$libraryName version is $actualVersion; required $($requiredLibraries[$libraryName])."
    }
}

# Espressif's Windows assembler cannot reliably create object files below a
# path containing Chinese characters, so all transient output stays in TEMP.
New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null

& $ArduinoCli compile --fqbn $fqbn --build-path $BuildDirectory `
    --build-property "compiler.cpp.extra_flags=-I$includeDirectory" `
    --libraries $LibrariesDirectory $sketchDirectory
if ($LASTEXITCODE -ne 0) {
    throw 'Arduino compilation failed.'
}

$sourcePath = Join-Path $BuildDirectory 'epaper75-app.ino.bin'
if (-not (Test-Path -LiteralPath $sourcePath)) {
    throw "Expected app artifact missing: $sourcePath"
}

New-Item -ItemType Directory -Force -Path $distDirectory | Out-Null
$appOutput = Join-Path $distDirectory 'epaper75-p1-app.bin'
Copy-Item -LiteralPath $sourcePath -Destination $appOutput -Force

# Never export generated bootloader/partition/boot_app0 files into dist. They
# are not used by this device's app0-only update flow and are easy to misflash.
Get-ChildItem -LiteralPath $distDirectory -Filter '*.bin' -File |
    Where-Object { $_.Name -ne 'epaper75-p1-app.bin' } |
    Remove-Item -Force

$app0MaximumBytes = 0x250000
if ((Get-Item -LiteralPath $appOutput).Length -gt $app0MaximumBytes) {
    throw "P1 application exceeds the verified app0 partition size of $app0MaximumBytes bytes."
}

$hash = Get-FileHash -LiteralPath $appOutput -Algorithm SHA256
Set-Content -LiteralPath (Join-Path $distDirectory 'SHA256SUMS.txt') `
    -Value "$($hash.Hash)  $((Get-Item -LiteralPath $appOutput).Name)" -Encoding ascii

Get-Item -LiteralPath $appOutput |
    Select-Object Name, Length, @{Name = 'SHA256'; Expression = { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }} |
    Format-Table -AutoSize
