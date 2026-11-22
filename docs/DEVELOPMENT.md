# 开发环境与构建

## 目录

```text
firmware/epaper75-app/
  epaper75-app.ino       Arduino CLI sketch marker
  include/               public project headers
  src/                   application source
  tools/                 setup, build, and app0-only flash helpers
  dist/                  current app0 image and SHA-256 only
firmware/epaper75-bringup/
  ...                    P0 hardware diagnostic source and private backup location
```

`epaper75-app.ino` 只用于满足 Arduino sketch 目录命名规则，实际入口位于 `src/main.cpp`。

## 固定版本

| 依赖 | 版本 |
|---|---:|
| Arduino ESP32 core | 3.3.10 |
| GxEPD2 | 1.6.9 |
| Adafruit GFX Library | 1.12.6 |
| Adafruit BusIO | 1.17.4 |
| ArduinoJson | 7.4.3 |
| WiFiManager | 2.0.17 |

构建脚本会检查已安装版本，避免工具链悄然升级后生成行为不同的固件。

## macOS

前置条件：Homebrew、Xcode Command Line Tools。

```bash
xcode-select --install  # 如果尚未安装
brew install arduino-cli
cd firmware/epaper75-app
./tools/setup_arduino.sh
./tools/build_arduino.sh
```

默认 Arduino 用户目录是 `~/Documents/Arduino`。需要其他位置时设置：

```bash
ARDUINO_LIBRARIES_DIR=/path/to/libraries \
BUILD_DIR=/tmp/epaper75-build \
./tools/build_arduino.sh
```

串口通常是 `/dev/cu.usbserial-*` 或 `/dev/cu.wchusbserial*`。先用 `ls /dev/cu.*` 确认，不要把某台电脑的端口名写入仓库。

## Windows

可以使用 PATH 中的 `arduino-cli.exe`，也可以使用 Arduino IDE 2 自带的 CLI。脚本会依次查找两者。

```powershell
cd firmware\epaper75-app
.\tools\setup_arduino.ps1
.\tools\build_arduino.ps1
```

需要指定位置时：

```powershell
.\tools\build_arduino.ps1 `
  -ArduinoCli 'C:\path\to\arduino-cli.exe' `
  -LibrariesDirectory 'C:\path\to\Arduino\libraries' `
  -BuildDirectory "$env:TEMP\epaper75-build"
```

Windows Espressif 汇编器在包含中文的构建路径下可能无法创建目标文件，因此脚本默认把临时输出放在 `%TEMP%`，而不是仓库目录。

## 规范构建参数

FQBN：

```text
esp32:esp32:esp32:FlashSize=8M,PartitionScheme=custom,PSRAM=disabled,FlashMode=dio,FlashFreq=40,EraseFlash=none,DebugLevel=info
```

构建完成后只复制 app 镜像到 `dist/epaper75-p1-app.bin`，并重新生成 `dist/SHA256SUMS.txt`。bootloader、partition table、`boot_app0`、ELF 和 map 文件都是临时产物，不应提交或刷入当前设备。

应用镜像必须小于等于 app0 的 `0x250000` 字节限制。构建输出还应检查静态 RAM，目标设备没有 PSRAM。

## 为什么暂不使用 PlatformIO

已有 P0 `platformio.ini` 仅用于早期实验。常见 `espressif32` 平台目前不能等价复现已验证的 Arduino ESP32 core 3.3.10 组合，而且默认 upload 流程可能写入 app0 之外的区域。因此 P1 的规范构建和刷写只使用本仓库脚本。

## 和风天气与私密配置

固件直接访问和风天气，契约见 [`WEATHER_PROTOCOL.md`](WEATHER_PROTOCOL.md)，
刷新与省电策略见 [`POWER.md`](POWER.md)。开发时注意：

- 用 `tools/qweather_keygen.swift` 生成专用 Ed25519 密钥对（macOS 自带
  LibreSSL 不支持 Ed25519），不要复用 SSH 等其他用途的密钥；
- API Host、开发者 ID、项目 ID、凭据 ID、私钥和坐标只能通过本地 USB 串口
  （`tools/provision_qweather.sh`）写入项目自有 NVS，不写进源码、构建参数、
  Git、截图和测试输出；
- 打开串口会复位开发板并进入 90 秒清醒窗口，串口命令只在清醒时有效。

## 主机测试、界面预览与数据生成

```bash
cd firmware/epaper75-app
./tools/hosttest/run_host_tests.sh            # 解析器、调度表、签名、Base64 等
./tools/preview/build_preview.sh /tmp/ep75    # 用真实渲染器生成 800x480 PNG
```

只在 macOS 上需要重新生成的数据（生成结果已提交）：

```bash
swift tools/gen/gen_calendar.swift > include/cal/CalendarData.h
tools/gen/gen_fonts.sh
```

`gen_fonts.sh` 需要把 Noto Sans SC 的 Regular/Bold OTF 放在
`~/Library/Caches/epaper75-fonts/`（或用 `EPAPER75_FONT_DIR` 指定），字体文件
不进入 Git。新增中文界面字符串后必须重新生成字库。

## 提交前检查

```bash
git diff --check
git status --short
```

如果修改了 C++、分区或构建脚本，还需：

1. 完整运行规范构建；
2. 核对 app 大小和 SHA-256；
3. 用 esptool `image-info` 确认为 ESP32、8 MB、DIO、40 MHz；
4. 搜索是否误加入 IP、SSID、MAC、token、auth 文件、完整串口日志或 Flash 备份；
5. 搜索所有刷写命令，确认唯一写入地址仍是 `0x10000`。

实机测试必须与改动风险相称。UI 改动至少确认一次完整刷新后
`panel=powered_off`。产品固件禁止局刷：`0.5.0-p1` 实测连续局刷会产生
半屏阴影，不能仅凭 GxEPD2 能力标志重新启用。网络/电源改动还要检查按键唤醒、
定时唤醒、取数失败退化（无 Wi-Fi、401）和无 panic。测试回报中隐藏所有网络、
串口、凭据和坐标信息。
