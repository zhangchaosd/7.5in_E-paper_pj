# 7.5 英寸墨水屏固件

这是针对一块既有 ESP32 墨水屏设备重新开发的固件仓库。当前产品方向是**日历 +
天气**看板：

- ESP32-WROOM-32E，8 MB Flash，无 PSRAM；
- 7.5 英寸 800×480 黑白红墨水屏，排线 `P750057-MF1-A`，只用全刷；
- 当月日历：农历、二十四节气、传统节日、法定节假日「休/班」（离线计算）；
- 和风天气：当前天气、空气质量、气象预警、未来六天预报、日出日落；
- 电压估算电量（SHT30 室内温湿度按需求不显示、不采样）；
- 深度睡眠 + 定时唤醒，只在可见内容变化时刷新，见 [`docs/POWER.md`](docs/POWER.md)。

当前已刷入 `0.6.0-p1`，源码位于 [`firmware/epaper75-app`](firmware/epaper75-app)，
刷写与验证记录见 [`P5_RESULTS.md`](firmware/epaper75-app/P5_RESULTS.md)。
[`firmware/epaper75-bringup`](firmware/epaper75-bringup) 是已验证的 P0 硬件诊断工程，只在硬件回归或恢复调查时使用。

## 快速开始

先阅读以下文档，尤其不要跳过刷写安全边界：

1. [`AGENTS.md`](AGENTS.md)：给 AI agent 和新开发者的工作约束；
2. [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md)：Windows/macOS 环境、依赖和构建；
3. [`docs/FLASH_SAFETY.md`](docs/FLASH_SAFETY.md)：唯一允许的刷写范围；
4. [`docs/HARDWARE.md`](docs/HARDWARE.md)：已确认硬件、引脚和按键；
5. [`docs/WEATHER_PROTOCOL.md`](docs/WEATHER_PROTOCOL.md)：和风天气接口、JWT 认证和串口配置契约；
6. [`docs/POWER.md`](docs/POWER.md)：唤醒时间表、刷新与省电策略；
7. [`firmware/epaper75-app/P4_RESULTS.md`](firmware/epaper75-app/P4_RESULTS.md)：全刷恢复策略的脱敏验证记录。

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

中文点阵字库由开源字体 Noto Sans SC（SIL OFL 1.1）生成，字体源文件不进入 Git，只提交生成的字形表。旧版 Codex Agent 协议（[`docs/AGENT_PROTOCOL.md`](docs/AGENT_PROTOCOL.md)）和 Bridge 文档只作为历史记录保留。

## 许可证

本项目以 [`GPL-3.0-only`](LICENSE) 发布。构建依赖、上游归属以及预编译固件发布时仍需履行的义务见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。预编译固件不直接进入源码 Git 历史，后续应通过带完整源码和许可证材料的 GitHub Release 发布。

## 当前限制

- 实机重复局刷会产生明显半屏阴影，因此产品固件禁用局刷和快速刷新；
- 深度睡眠时只有 Upper、Middle 两个键能唤醒设备；
- 和风天气 v1 接口按文档实现，首次实机联网后需核对真实字段；
- 电量是电压曲线估算值，界面以 `~` 标识，仍需万用表校准；低电量阈值 3.45 V 待验证；
- 睡眠电流、电池续航尚未实测（估算见 `docs/POWER.md`）；
- 法定节假日表目前覆盖 2026、2027 年，每年 11 月需更新；农历/节气数据覆盖 2025–2035 年；
- 天气凭据只能通过本地 USB 串口写入，设备不提供网络配置接口；
- EN 在刷新中断、高压轨 BUSY 故障仍待硬件验证。
