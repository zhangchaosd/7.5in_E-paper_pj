# 7.5 英寸墨水屏固件

这是针对一块既有 ESP32 墨水屏设备重新开发的固件仓库。当前固件已在实机上验证：

- ESP32-WROOM-32E，8 MB Flash，无 PSRAM；
- 7.5 英寸 800×480 黑白红墨水屏，排线 `P750057-MF1-A`；
- 首页显示 Codex 5 小时/周额度余量、绝对重置时间、四站点 TCP/443 连接延迟；
- 顶栏显示估算电量、Wi-Fi 和 Bridge 状态；
- 三个用户按键、SHT30、Wi-Fi 配网、Bridge 轮询和设备 HTTP API 均已打通。

当前产品固件是 `0.3.0-p1`，源码位于 [`firmware/epaper75-app`](firmware/epaper75-app)。
[`firmware/epaper75-bringup`](firmware/epaper75-bringup) 是已验证的 P0 硬件诊断工程，只在硬件回归或恢复调查时使用。

## 快速开始

先阅读以下文档，尤其不要跳过刷写安全边界：

1. [`AGENTS.md`](AGENTS.md)：给 AI agent 和新开发者的工作约束；
2. [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md)：Windows/macOS 环境、依赖和构建；
3. [`docs/FLASH_SAFETY.md`](docs/FLASH_SAFETY.md)：唯一允许的刷写范围；
4. [`docs/HARDWARE.md`](docs/HARDWARE.md)：已确认硬件、引脚和按键；
5. [`docs/BRIDGE_PROTOCOL.md`](docs/BRIDGE_PROTOCOL.md)：桌面 Bridge 的最小协议。

macOS 初次安装依赖并构建：

```bash
brew install arduino-cli
cd firmware/epaper75-app
./tools/setup_arduino.sh
./tools/build_arduino.sh
```

Windows PowerShell：

```powershell
cd firmware\epaper75-app
.\tools\setup_arduino.ps1
.\tools\build_arduino.ps1
```

构建只在本地产生 app0 应用镜像 `firmware/epaper75-app/dist/epaper75-p1-app.bin`。不要使用 Arduino IDE、PlatformIO 或普通 `esptool write-flash` 直接上传整套产物。

## 仓库边界

仓库只保留当前固件、P0 诊断源码、可复现脚本和继续开发所需的结论文档。逆向阶段使用的厂商资料、样例项目、便携 SDK、编译缓存和设备完整 Flash 备份不进入 Git。

两份实机 8 MB 完整备份必须继续离线保存在 `firmware/epaper75-bringup/backups/`。该目录被忽略，因为备份可能包含 Wi-Fi/NVS 私密数据；不要删除、重命名或上传它们。

Bridge 参考实现来自外部项目，未复制到本仓库。固定版本和跨平台启动方式见 [`docs/BRIDGE_PROTOCOL.md`](docs/BRIDGE_PROTOCOL.md)。

## 许可证

本项目以 [`GPL-3.0-only`](LICENSE) 发布。构建依赖、上游归属以及预编译固件发布时仍需履行的义务见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。预编译固件不直接进入源码 Git 历史，后续应通过带完整源码和许可证材料的 GitHub Release 发布。

## 当前限制

- 墨水屏仅启用约 21 秒的全屏刷新，未启用局刷；
- 刷新期间主循环上的按键和 Bridge 轮询会暂停，HTTP API 与网络探测仍可用；
- 电量是电压曲线估算值，界面以 `~` 标识，仍需万用表校准；
- Bridge 和设备 HTTP API 当前无鉴权，只适合可信局域网；
- EN 在刷新中断、电池续航、低压保护和高压轨 BUSY 故障仍待硬件验证。
