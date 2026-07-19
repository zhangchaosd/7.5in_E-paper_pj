# Codex Monitor Agent 对接协议

## 适用范围

`0.5.0-p1` 不读取旧桌面 Bridge 的 `GET /status`。墨水屏作为只读
看板，连接三个 Codex Monitor Agent（CMA）实例，消费 CMA `0.4.x` 的
schema `1.1`。三个实例使用同一个 Codex 账号，因此：

- 账号级 Weekly 限额固定优先从 Agent 槽位 0 获取，必要时从其余已验证
  Agent 回退；
- 活动、线程、待确认和错误是设备级数据，分别显示，不跨设备合并；
- Home 不向 Agent 发起批准、输入或其他控制操作。

固件只接受 schema major `1`，当前实现和测试字段以 schema `1.1` 为准。
跨 major 升级必须先更新解析器并复核 RAM、响应上限和 UI 语义。

## 鉴权与传输边界

对 CMA 的每个 JSON 请求都带：

```http
Authorization: Bearer <shared-token>
```

三个 Agent 当前共用一个 Bearer token。固件使用局域网明文 HTTP，Bearer
只能保护应用层访问，不能提供链路加密。Agent 和墨水屏应仅位于可信、
隔离且有防火墙限制的局域网；不要把端口暴露到公网。

设备不通过 Wi-Fi 配网页或 HTTP 接收 Agent host/token；私密配置唯一入口
是物理 USB 串口的 `AGENTS` 命令。配置完成后，设备的 UI、`INFO`、
`/api/info` 和 `/api/health` 都不得回显 Agent host、token 或
`installation_id`。串口命令原文同样是秘密，不得进入会话日志、截图或
测试报告。

## 固件请求顺序

Agent 客户端是 Core 0 上的低优先级任务。它每 5 秒轮询一个槽位，三个
槽位交错后，每个 Agent 约 15 秒更新一次；不会同时维持三条 SSE 长连接。

1. 配置首次加载或改变后，请求 `GET /api/v1/version`。
2. 确认 schema major 为 `1`，记录该槽位的 `installation_id`。
3. 请求 `GET /api/v1/status`，再次核对 schema 和
   `installation_id`。
4. 仅当摘要表明存在活跃线程时，请求
   `GET /api/v1/threads?limit=12`。
5. 每 60 秒开始一轮账号限额请求：槽位 0 先请求
   `GET /api/v1/rate-limits`；若数据不可用，再按槽位顺序尝试其余 Agent。

手动请求立即轮询时，从槽位 0 开始，并同时启动一轮限额刷新。
单次 HTTP 连接预算为 1 秒，读取预算为 2.2 秒。固件对响应做流式、限长
解析：version 8 KiB、status 16 KiB、threads 24 KiB、rate-limits
16 KiB；超限响应不会进入共享 UI 状态。

## 消费字段

### `GET /api/v1/version`

固件使用：

| 字段 | 要求 | 用途 |
|---|---|---|
| `schema_version` | 非空字符串，major 为 `1` | 协议兼容门 |
| `installation_id` | 非空、稳定 | 防止一个槽位静默指向另一实例 |

首次认证成功后，`installation_id` 会与私密 Agent 配置一起固定到项目自有
NVS，并在每次启动后重新核对。它只能存在于 `SettingsStore`/`AgentClient`
私有状态中，不能进入 UI model、设备 API 或日志。
固定操作在 SettingsStore 锁内使用配置比较后交换；若 USB 配置已变化，
旧轮询结果会被丢弃并重新加载，不能覆盖较新的 endpoint 或 token。

### `GET /api/v1/status`

固件消费这些 schema 1.1 字段：

| 字段 | 用途 |
|---|---|
| `schema_version`, `installation_id` | 每次响应的兼容性和身份复核 |
| `generated_at` | 账号数据的来源更新时间，RFC 3339 UTC |
| `stale` | 区分在线新数据与恢复中/过期数据 |
| `summary.workload_state` | `RUNNING`、`WAITING_APPROVAL`、`WAITING_INPUT`、`IDLE`、`ERROR` 或未知 |
| `summary.state_confidence` | 只有 `exact` 计为精确状态 |
| `summary.known_threads` | 决定是否补充请求线程列表 |
| `summary.active_workflows` | 设备行的工作流计数 |
| `summary.active_workers` | 设备行的 worker 计数 |
| `summary.states.running` | 运行中计数 |
| `summary.states.waiting_approval` | 等待批准计数 |
| `summary.states.waiting_input` | 等待输入计数 |
| `summary.states.error` | 错误计数 |

缺失的可选计数保留为未知，不按零处理。`UNKNOWN` 和 `IDLE` 也不是同一
状态。

### `GET /api/v1/threads?limit=12`

固件读取每个线程的 `state`、`name`、`preview` 和 `updated_at`，以及根级
`schema_version`、`generated_at`。当前任务选择优先级为：

```text
WAITING_APPROVAL > WAITING_INPUT > RUNNING > ERROR
```

同优先级选择 `updated_at` 最新的一项；优先显示 `name`，为空时使用
`preview`。显示文本会移除控制字符和非 ASCII 字节、合并空白，并限制为
固定缓冲区可容纳的短文本。线程接口临时失败时保留上一次安全任务文本；
401 仍按鉴权错误处理。

### `GET /api/v1/rate-limits`

固件首先检查根级 `availability`：

- `available`：继续解析 `rateLimits`；
- `unavailable`：当前 Agent 没有可用的账号级限额数据，尝试回退来源；
- 其他值或错误类型：视为无效响应。

在 `rateLimits.primary` 和 `rateLimits.secondary` 中，固件选取
`windowDurationMins` 位于 6 到 8 天之间的窗口作为 Weekly，并读取：

| 字段 | 要求 | 显示语义 |
|---|---|---|
| `usedPercent` | 0..100 数字 | Home 显示 `100 - usedPercent` 的余量 |
| `windowDurationMins` | 8640..11520 | 确认这是 Weekly 窗口 |
| `resetsAt` | 合法 Unix epoch 秒 | 转为 UTC+8 的绝对 `YYYY-MM-DD HH:MM` |

重置后进度条接近 100%，随使用逐步减少。UI 不显示倒计时，也不把无法
获得的数据伪装成 0%。

## 失败状态

| 条件 | 固件行为 |
|---|---|
| Wi-Fi 断开 | 三个已配置槽位立即标为离线；保留的账号数据标为 stale |
| 普通连接/HTTP/JSON 失败 | 先显示恢复中；连续三次失败后显示离线 |
| HTTP 401 | 该槽位进入 `AUTH ERROR`，停止重试，直到 Agent 配置改变 |
| schema major 不兼容 | 进入 `BAD DATA`，停止重试，直到配置改变 |
| `installation_id` 改变 | 进入 `BAD DATA`，停止重试，直到配置改变 |
| Agent 明确返回 stale | 保留有界缓存并显示恢复/过期语义，不计入 `N/3` 在线数 |

顶栏的 `N/3` 是通过身份验证、当前在线且不 stale 的实时数量，不是已配置
数量。三台机器不在同一可路由网络时，小于 `3/3` 是预期结果。

## 通过 USB 串口配置三个 Agent

连接本地 USB 串口后，以 115200 baud 发送一行完整配置：

```text
AGENTS <endpoint0> <endpoint1> <endpoint2> <shared-token>
```

尖括号只表示占位符，不要作为实际输入的一部分。四个参数以空格或 tab
分隔；endpoint 和 token 自身不能包含空白。

字段规则：

- 三个 endpoint 和 token 都是必填，不能只更新其中一个；
- 槽位 0 固定为账号限额主来源，没有 `primary` 参数；
- 端口省略时为 `8765`；
- host 接受 IPv4 风格文本或 DNS/mDNS 名称，可带端口；
- 不接受 URL scheme、路径、userinfo、查询串或 IPv6 bracket 语法；
- token 必须是非空可见 ASCII，最大 95 字节。

成功时串口只输出 `[agents] private settings stored`，失败只输出脱敏的
`Agent settings rejected.`，固件不会回显参数。设置保存在项目自己的
`ep75_p1` NVS namespace、带版本和 CRC 的 `agent_fleet` blob 中；不得
擦除 ESP32 Wi-Fi NVS。配置改变后客户端清空旧身份缓存、解除鉴权/协议
锁，并重新从槽位 0 验证。

使用关闭本地回显和会话记录的串口工具。不要在文档、提交、截图、shell
历史、CI 输出或测试报告中写真实地址和 token。自动化配置应从本地私密
环境或进程上下文读入变量，直接写串口，并只输出脱敏的成功/失败结果。
配置命令不得通过远程 HTTP、Wi-Fi 配网页或固件编译参数传递。

## 设备侧可观察接口

`GET /api/info` 和 `GET /api/health` 只返回安全聚合值，例如：已配置/在线
Agent 数、需操作数、错误数、账号数据 availability/stale、Weekly 余量和
重置 epoch。它们不得返回 Agent endpoint、Bearer token、安装 ID、设备
IP、SSID 或 MAC。设备 HTTP API 完全只读，没有 `/api/agents`、远程刷新
或其他网络写入口；配置与手动刷新只能通过 USB 串口或实体按键完成。

旧四站点 `NetworkProbe` 在 `0.5.0-p1` 中不启动，Home 和设备 API 都不再
提供网络延迟数据。
