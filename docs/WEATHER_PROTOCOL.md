# 和风天气（QWeather）接入契约

`0.6.x` 固件直接通过 HTTPS 访问和风天气，不再依赖电脑端的 Agent 程序。本文件
记录固件消费的接口、字段、认证方式和私密数据边界。和风天气接口会演进，字段解析
在 `src/net/WeatherClient.cpp` 中做了兼容处理，主机测试见
`tools/hosttest/host_tests.cpp`。

## 认证

- 每个开发者账号有专属 **API Host**（`xxxx.qweatherapi.com`，控制台-设置）。旧的
  `devapi.qweather.com` 等公共域名自 2026 年起逐步停用，固件不使用它们。
- 使用 **JWT（Ed25519 / EdDSA）**。API KEY 方式从 2027-01-01 起受每日请求数限制，
  固件不支持。
  - Header：`{"alg":"EdDSA","kid":"<凭据ID>"}`
  - Payload：`{"iss":"<开发者ID>","sub":"<项目ID>","iat":<now-30>,"exp":<iat+900>}`
  - 三段 Base64URL（无填充）以 `.` 连接；签名使用 core 自带的 libsodium
    `crypto_sign_ed25519_detached`。
  - 请求头：`Authorization: Bearer <token>`。
- JWT 依赖正确时间：每次联网先做 NTP 校时，校时失败不发请求。
- TLS 使用 ESP32 core 内置的 Mozilla 根证书包校验服务器证书，不关闭校验。

密钥对用 `firmware/epaper75-app/tools/qweather_keygen.swift` 在 macOS 上生成
（系统自带的 LibreSSL 不支持 Ed25519）。公钥（PEM）上传到控制台，私钥只留在本机。

## 接口与字段

所有路径都在 API Host 之下，坐标取两位小数（纬度/经度）。

| 用途 | 路径 | 使用字段 |
|---|---|---|
| 当前天气 | `/weather/v1/current/{lat}/{lon}?lang=zh&localTime=true` | `temperature.value`、`feelsLike.value`、`condition.code`、`humidity`（0–1）、`wind.direction.degree`、`wind.scale` |
| 7 天预报 | `/weather/v1/daily/{lat}/{lon}?days=7&lang=zh&localTime=true` | `days[].forecastStartTime/EndTime`（取中点换算北京日期）、`temperatureMax/Min.value`、`daytime/nighttime.condition.code`、`daytime/nighttime.precipitation.probability`（0–1）、`astro.sunrise/sunset` |
| 空气质量 | `/airquality/v1/current/{lat}/{lon}?lang=zh` | `indexes[]` 中 `code == "cn-mee"` 的 `aqi` |
| 气象预警 | `/weatheralert/v1/current/{lat}/{lon}?lang=zh&localTime=true` | `alerts[].id`、`eventType.name`、`color.code`、`messageType.code`（跳过 `cancel`） |
| 地名 | `/geo/v2/city/lookup?location={lon},{lat}&number=1&lang=zh` | `location[0].name`、`adm2`/`adm1` |

- 响应默认 gzip 压缩：固件发送 `Accept-Encoding: gzip`，用 ESP32 ROM 的 `tinfl`
  解压并校验 CRC32；也接受未压缩响应。压缩体上限 32 KB，解压后上限 96 KB。
- 天气现象文字和图标由固件根据 `condition.code`（100 晴、101 多云……999 未知）
  查表得到，不直接显示接口返回的文字，以保证字库覆盖；未知新代码按百位分组降级。
- AQI 类别按 HJ 633 由固件计算（优/良/轻度/中度/重度/严重）。
- 预警只显示"事件名 + 颜色 + 预警"，多条时取颜色最严重的一条，系统页显示条数。
- 地名查询成功后缓存到 NVS（仅在坐标变化后重新查询）。
- 按设备所有者要求，屏幕上不显示"和风天气"字样。和风天气文档要求随数据展示
  来源标注；若公开发布或展示该设备，请先核对其使用条款。

## 用量

完整更新 4 次请求，预警检查 1 次。按 [`POWER.md`](POWER.md) 的时间表约 1200 次/月，
和风天气每月前 5 万次免费。

## 配置（仅限本地 USB 串口）

```text
QWEATHER <api-host> <developer-id> <project-id> <credential-id> <private-key-base64>
LOCATION <lat> <lon>
QWEATHER CLEAR
```

- `<private-key-base64>` 是私钥 PEM 中间那一行（PKCS#8，解码 48 字节），也接受
  32 字节原始种子的 Base64。
- 推荐使用 `tools/provision_qweather.sh`：它从 PEM 文件读取私钥、提示输入其余 ID，
  直接写串口，不回显、不进 shell 历史，只显示设备的脱敏回复。
- 固件只回复 `[qweather] settings stored/rejected`，`INFO` 只显示 `set/missing`。

## 私密数据边界

- API Host、开发者 ID、项目 ID、凭据 ID、私钥和坐标只保存在项目自有 NVS
  命名空间（`ep75_p1`），只由 `SettingsStore`/`WeatherClient` 使用，不进入
  `UiModel`、渲染输出、串口输出或 Git。
- 不要把这些值写进源码、构建参数、截图、测试记录或 Issue。私钥文件 `*.pem`
  已被 `.gitignore` 排除。
- 没有任何网络配置入口；Wi-Fi 配网页面不包含天气凭据。
