# 7.5 英寸墨水屏固件

这是针对一块既有 ESP32 墨水屏设备重新开发的固件仓库。硬件、安全刷写路径和三设备 Codex Agent 看板已经过实机验证：

- ESP32-WROOM-32E，8 MB Flash，无 PSRAM；
- 7.5 英寸 800×480 黑白红墨水屏，排线 `P750057-MF1-A`；
- 首页左侧显示共享 Codex 账号的 Weekly 余量、绝对重置时间和数据来源；
- 首页右侧显示三个 Codex Monitor Agent 的当前活动、计数和待操作状态；
- 顶栏只显示 Wi-Fi 图标和动态 `N/3` 在线数，不显示电池或 Bridge；
- 三个用户按键、SHT30、Wi-Fi 配网、Agent Bearer 认证轮询和设备 HTTP API 已集成。

当前已刷入固件是 `0.5.0-p1`，源码位于 [`firmware/epaper75-app`](firmware/epaper75-app)。
[`firmware/epaper75-bringup`](firmware/epaper75-bringup) 是已验证的 P0 硬件诊断工程，只在硬件回归或恢复调查时使用。

## 快速开始

先阅读以下文档，尤其不要跳过刷写安全边界：

1. [`AGENTS.md`](AGENTS.md)：给 AI agent 和新开发者的工作约束；
2. [`docs/DEVELOPMENT.md`](docs/DEVELOPMENT.md)：Windows/macOS 环境、依赖和构建；
3. [`docs/FLASH_SAFETY.md`](docs/FLASH_SAFETY.md)：唯一允许的刷写范围；
4. [`docs/HARDWARE.md`](docs/HARDWARE.md)：已确认硬件、引脚和按键；
5. [`docs/AGENT_PROTOCOL.md`](docs/AGENT_PROTOCOL.md)：三 Agent、Bearer 认证、schema 1.1 和设备配置契约。
6. [`firmware/epaper75-app/P3_RESULTS.md`](firmware/epaper75-app/P3_RESULTS.md)：本版本刷新策略的脱敏验证记录。

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

Codex Monitor Agent 程序和它的上游协议资料不复制到本仓库。固件只实现经审阅的最小消费端，见 [`docs/AGENT_PROTOCOL.md`](docs/AGENT_PROTOCOL.md)。旧版 Bridge 文档保留为迁移指针，`0.4.x` 固件不再消费其 `/status` 协议。

## 许可证

本项目以 [`GPL-3.0-only`](LICENSE) 发布。构建依赖、上游归属以及预编译固件发布时仍需履行的义务见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。预编译固件不直接进入源码 Git 历史，后续应通过带完整源码和许可证材料的 GitHub Release 发布。

## 当前限制

- 面板支持局部窗口更新，但不支持快速局刷；局部波形本身仍需十几秒，优化重点是只闪动脏区而非缩短波形；
- 首次启动、页面切换、手动清理、大面积变化以及累计 24 次局刷或 6 小时后仍会全刷；
- 刷新期间主循环上的按键扫描会暂停；Core 0 上的 HTTP API 和 Agent 轮询仍可运行；旧 NetworkProbe 在 `0.5.0-p1` 中不启动；
- 电量是电压曲线估算值，界面以 `~` 标识，仍需万用表校准；
- Agent 请求使用共享 Bearer token；三个 endpoint 和 token 只能通过本地 USB 串口 `AGENTS` 命令写入，设备不提供网络配置接口；
- 三个 Agent 中若有一台不在墨水屏可路由的网络，顶栏会如实显示小于 `3/3`，不会把配置数量当作在线数量；
- EN 在刷新中断、电池续航、低压保护和高压轨 BUSY 故障仍待硬件验证。
