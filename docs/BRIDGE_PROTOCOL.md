# Bridge 协议与跨平台参考

## 边界

ESP32 不直接读取电脑上的 Codex/Claude 日志或凭据。桌面 Bridge 在可信局域网提供 HTTP 服务，固件每 5 秒请求：

```http
GET /status
```

连接和读取总预算约 3 秒，响应必须是 HTTP 200、有效 JSON，且不超过 16 KiB。连续三次普通请求失败后界面显示 Bridge 离线；Wi-Fi 断开、端点删除或端点更换会立即转为不可用。

## 必需 JSON

当前解析器为兼容参考项目，要求根对象同时包含 `ts`、`claude` 和 `codex`。即使首页只显示 Codex，Claude 对象仍不能省略。

```json
{
  "ts": 1784000000,
  "claude": {
    "status": "idle",
    "tokens_today": 0,
    "session_min": 0,
    "session_window_min": 300,
    "five_hour_pct": null,
    "five_hour_reset_min": null,
    "seven_day_pct": null,
    "seven_day_reset_min": null,
    "needs_input": false
  },
  "codex": {
    "status": "working",
    "tokens_today": 12345,
    "primary_pct": 24.0,
    "primary_window_min": 300,
    "primary_reset_min": 90,
    "weekly_pct": 37.0,
    "weekly_window_min": 10080,
    "weekly_reset_min": 4200,
    "needs_input": false
  }
}
```

字段规则：

| 字段 | 类型/范围 | 说明 |
|---|---|---|
| `ts` | 非负整数 | Bridge 生成响应时的 Unix 秒时间 |
| `status` | 非空字符串 | 常用值为 `working`、`idle`、`offline` |
| `tokens_today` | 0..INT32_MAX 整数 | 当日 token 统计 |
| `*_pct` | 0..100 数字或 `null` | 已使用百分比，不是余量 |
| `*_window_min` | 非负整数或 `null` | 窗口长度 |
| `*_reset_min` | 非负整数或 `null` | 从 `ts` 起到重置的分钟数 |
| `needs_input` | boolean | 是否需要用户介入 |

额外的严格约束：`status` 必须非空且短于 20 字节；Claude 的 `session_min` 必须是非负整数，`session_window_min` 必须是正整数，二者都不能为 `null`。其余 `*_pct` / `*_reset_min` / Codex `*_window_min` 即使未知也必须保留键并显式给 `null`，不能省略键。

固件显示 `100 - primary_pct` 和 `100 - weekly_pct`，所以重置后为满条、使用后逐步变少。固件把 `ts + reset_min * 60` 转成 UTC+8 的绝对 `YYYY-MM-DD HH:MM`，并对 Bridge 缓存造成的数分钟时间漂移做稳定处理。

## 配置设备 Bridge 地址

Bridge host 通过设备 API 持久化到项目自己的 NVS namespace：

```bash
curl -X POST -d 'host=hostname-or-ip:8765' http://<device>/api/bridge
```

设备只接受主机名或 IPv4 风格文本加可选端口，不接受 URL scheme、路径、userinfo 或 IPv6 bracket 语法。不要把真实设备或 Bridge 地址写进 Git。

## 外部参考实现

逆向阶段验证的跨平台参考项目：

- 仓库：<https://github.com/pengchujin/esp8266-ai>
- 固定提交：`9906402d8724082d27ba95c754c481d25978866c`

该项目包含许多本设备不需要的 TFT、GIF、音乐、网速镜像和桌宠功能，因此不 vendoring 到本仓库。需要运行时在仓库外单独克隆并固定到上述提交：

```bash
git clone https://github.com/pengchujin/esp8266-ai.git
cd esp8266-ai
git checkout 9906402d8724082d27ba95c754c481d25978866c
```

macOS：

```bash
cd mac-app
swift build -c release
.build/release/AIClockBridge
```

Windows：

```powershell
cd windows-app\AIClockBridge
dotnet build -c Release
dotnet run -c Release
```

本仓库的 Windows 启动辅助脚本接受 `-BridgeRoot`，macOS 辅助脚本使用 `EPAPER75_REFERENCE_BRIDGE_ROOT` 或位置参数，不再依赖中文参考资料目录。

## 安全限制与后续方向

当前 Bridge 和设备 API 都没有鉴权，且 Bridge 需要监听局域网接口才能被 ESP32 访问。只在可信家庭/开发网络运行，并用系统防火墙限制访问范围。不要向设备传送 API key；设备只需要状态、计数、百分比和重置信息。

正式 P2 Bridge 应放在新的自有 `bridge/` 目录，至少增加：

- 版本化协议和 `device_type=epaper75` 校验；
- 一次性配对或共享 token；
- `/event` 来源限制；
- 不相关 TFT/GIF/音乐菜单的删除；
- Windows 与 macOS 的同一组契约测试；
- 开机自启和本机凭据读取的最小权限说明。
