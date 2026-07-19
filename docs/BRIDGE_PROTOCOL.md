# 旧 Bridge 协议（已停用）

本文路径保留用于旧链接和 `0.3.x` 历史记录。当前 `0.4.0-p1` 固件已经
删除 `BridgeClient`、`GET /status` 消费端和 `POST /api/bridge` 配置接口，
也不再要求带 `claude`/`codex` 对象的旧兼容 JSON。

当前固件使用三个带 Bearer 认证的 Codex Monitor Agent，协议、轮询、
配置和隐私规则见 [`AGENT_PROTOCOL.md`](AGENT_PROTOCOL.md)。

旧版 `start_reference_bridge.*` 和 `stop_reference_bridge.ps1` 辅助脚本已
从仓库删除；若需复现历史 P1 验证，应从对应历史提交中获取。旧外部样例
也不得 vendoring 到本仓库。
