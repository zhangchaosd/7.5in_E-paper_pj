# 刷写安全边界

这是仓库最重要的操作约束。当前设备保留了原厂 bootloader、NVS、OTA 元数据、app1 和 SPIFFS，用于回退和继续识别硬件；普通开发只允许替换 app0。

## 唯一允许的写入

```text
offset: 0x10000
length: <= 0x250000
image:  firmware/epaper75-app/dist/epaper75-p1-app.bin
```

Windows 必须使用：

```powershell
.\tools\flash_app0_only.ps1 -Port <COM端口> -VerifiedFactoryBackup
```

macOS/Linux 必须使用：

```bash
./tools/flash_app0_only.sh --port /dev/cu.<device> --verified-factory-backup
```

两个脚本都会检查应用大小、SHA-256、镜像格式、芯片容量和两份本地完整备份，然后只执行 `write-flash ... 0x10000 <app>`。

## 刷写前清单

1. 串口输入 `INFO`，确认 `panel=powered_off` 且 `error=none`；
2. 设备不是正在刷新或 BUSY 故障状态；
3. `firmware/epaper75-bringup/backups/` 中有两份独立读取的 8 MB 备份及 `SHA256SUMS.txt`；
4. 备份大小、哈希验证通过，并且另有离线副本；
5. `dist/SHA256SUMS.txt` 与待刷 app 镜像一致；
6. 已确认连接的是目标 ESP32、8 MB Flash；
7. 已保存当前源码和测试计划。

完整备份可能包含 Wi-Fi 凭据和 NVS 私密状态。它们被 `.gitignore` 排除；禁止上传到 GitHub、聊天、工单或制品服务。

推荐使用 P0 的 `backup_flash_local.ps1`（Windows）或 `backup_flash.sh`（macOS/Linux）读取备份。脚本只执行 `read-flash`，并把哈希追加到私密 manifest；也可用 `EPAPER75_BACKUP_DIR` 将备份保存在仓库外。

## 明确禁止

- `erase-flash` / `erase_flash`；
- 从 `0x0` 开始写整包固件；
- 写 `0x1000`、`0x8000`、`0x9000`、`0xE000`、`0x260000` 或 `0x4B0000`；
- Arduino IDE 的 Upload、PlatformIO 的 `upload` 或任何不透明的一键烧录；
- 为了“修复”启动问题而清空 NVS 或 OTA data；
- 把 P1 构建生成的 bootloader/partition/boot_app0 文件刷入实机；
- 在没有两份验证备份时绕过脚本保护。

## 刷写后验证

1. 串口无 boot loop、panic 或 brownout；
2. 固件版本符合预期；
3. Wi-Fi/NVS 配置仍保留；
4. `/api/health` 的 Wi-Fi/Agent 安全聚合状态正常，且没有泄露 endpoint、token 或安装 ID；
5. 三个按键映射不变；
6. 做一次必要的物理全刷；产品固件不得执行局部窗口刷新；
7. 确认全刷结束后画面无半屏阴影，最终再次确认
   `panel=powered_off` 和无显示错误。

不要在公开日志中粘贴完整 `INFO`、原始 HTTP 响应或串口 `AGENTS` 配置命令；它们可能
包含网络地址、Agent 端点、Bearer token、安装 ID 或串口标识。只报告经过
脱敏的布尔结果和聚合计数。

## 回退原则

优先回退到上一个已验证的 app0 镜像，仍只写 `0x10000`。完整 8 MB 恢复是最后手段，可能覆盖当前 NVS 和设备状态；只有在明确诊断、核对备份并获得设备所有者确认后才能执行，本仓库不提供日常全片恢复快捷命令。
