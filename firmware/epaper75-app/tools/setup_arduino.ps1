param(
    [string]$ArduinoCli = ''
)

$ErrorActionPreference = 'Stop'

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
    throw 'Arduino CLI not found. Install Arduino CLI/Arduino IDE 2 or pass -ArduinoCli.'
}

$esp32Index = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
& $ArduinoCli core update-index --additional-urls $esp32Index
if ($LASTEXITCODE -ne 0) { throw 'Arduino core index update failed.' }
& $ArduinoCli core install 'esp32:esp32@3.3.10' --additional-urls $esp32Index
if ($LASTEXITCODE -ne 0) { throw 'ESP32 core installation failed.' }

$libraries = @(
    'GxEPD2@1.6.9',
    'Adafruit GFX Library@1.12.6',
    'Adafruit BusIO@1.17.4',
    'ArduinoJson@7.4.3',
    'WiFiManager@2.0.17'
)
foreach ($library in $libraries) {
    & $ArduinoCli lib install $library
    if ($LASTEXITCODE -ne 0) { throw "Arduino library installation failed: $library" }
}

Write-Host 'Pinned Arduino toolchain is installed.'
