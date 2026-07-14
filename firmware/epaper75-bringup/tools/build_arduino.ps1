param(
    [string]$ArduinoCli = "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe",
    [string]$LibrariesDirectory = "$env:USERPROFILE\Documents\Arduino\libraries",
    [string]$BuildDirectory = "$env:TEMP\epaper75-bringup-build"
)

$ErrorActionPreference = 'Stop'
$sketchDirectory = Split-Path -Parent $PSScriptRoot
$distDirectory = Join-Path $sketchDirectory 'dist'
$fqbn = 'esp32:esp32:esp32:FlashSize=8M,PartitionScheme=custom,PSRAM=disabled,FlashMode=dio,FlashFreq=40,EraseFlash=none,DebugLevel=info'

if (-not (Test-Path -LiteralPath $ArduinoCli)) {
    throw "Arduino CLI not found: $ArduinoCli"
}
if (-not (Test-Path -LiteralPath $LibrariesDirectory)) {
    throw "Arduino libraries directory not found: $LibrariesDirectory"
}

# The Espressif Windows assembler cannot reliably create object files below a
# path containing Chinese characters. Keep all transient outputs in TEMP.
New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null

& $ArduinoCli compile --fqbn $fqbn --build-path $BuildDirectory --libraries $LibrariesDirectory $sketchDirectory
if ($LASTEXITCODE -ne 0) {
    throw 'Arduino compilation failed.'
}

New-Item -ItemType Directory -Force -Path $distDirectory | Out-Null
$sourcePath = Join-Path $BuildDirectory 'epaper75-bringup.ino.bin'
if (-not (Test-Path -LiteralPath $sourcePath)) {
    throw "Expected app artifact missing: $sourcePath"
}
Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $distDirectory 'epaper75-p0-app.bin') -Force

Get-ChildItem -LiteralPath $distDirectory -Filter '*.bin' -File |
    Where-Object { $_.Name -ne 'epaper75-p0-app.bin' } |
    Remove-Item -Force

Get-FileHash -LiteralPath (Join-Path $distDirectory 'epaper75-p0-app.bin') -Algorithm SHA256 |
    Select-Object Path, Hash |
    Format-Table -AutoSize
