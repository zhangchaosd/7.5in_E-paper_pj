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

## Codex Monitor Agent

固件本身不读取电脑上的 Codex 文件或凭据。`AgentClient` 在 Core 0
依次连接三个局域网 Codex Monitor Agent，使用共享 Bearer token 消费
CMA `0.4.x` schema `1.1`。设备活动是每个 Agent 独立的数据；Weekly
限额是共享账号数据，固定优先从槽位 0 获取并允许经身份验证的回退。

完整协议和安全边界见 [`AGENT_PROTOCOL.md`](AGENT_PROTOCOL.md)。开发时尤其
注意：

- 不把真实 Agent 地址、token、`installation_id`、设备 IP、SSID 或 MAC
  写进源码、构建参数、Git、截图和测试输出；
- 三个 endpoint 和共享 token 只能通过本地 USB 串口命令
  `AGENTS <endpoint0> <endpoint1> <endpoint2> <token>` 保存到项目自有 NVS
  blob；槽位 0 固定为主来源；
- 固件没有 `/api/agents` 或其他网络私密配置入口；不要通过 Wi-Fi 配网页、
  HTTP、命令行参数或编译宏传入这些值；
- 使用不回显、不保存会话日志的本地串口工具发送 `AGENTS` 命令；只确认
  固件返回脱敏的成功/失败信息，不复制原始命令到测试记录；
- 401、schema 不兼容或安装身份变化会锁定对应槽位，修改配置后才重试；
- 顶栏 `N/3` 必须来自实时在线状态，不能用已配置数量替代。

`docs/BRIDGE_PROTOCOL.md` 仅保留为 `0.3.x` 旧版迁移指针。当前固件不再
依赖外部 `AIClockBridge` 或 `GET /status`。

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
`panel=powered_off`；局刷改动还必须确认窗口边界、黑/白/红擦写、BUSY
时长、一次循环只发一个更新波形、局刷后的全刷清理以及最终断高压；并发/
网络改动还要检查按键、三个 Agent 的安全聚合 health、401/离线退化和无
panic。测试回报中隐藏所有网络、串口、凭据和 Agent 安装标识。
