# Device OS 工具 Provider 契约

> 最后更新：2026-08-18；适用版本：0.6.0-dev；状态：草案；physical provider 尚未实现

本 host-process contract 用于隔离 Runtime 作者工具与物理板卡依赖。它不是 `JFDP/1`，不改变
wire bytes，也不把桌面 reference endpoint 变成 device transport。

## 所有权

`jellyframe-device-os` 负责 physical discovery、USB/serial dependency、endpoint selection、JFDP
framing 与 device telemetry。JellyFrame Runtime 负责 package preflight、桌面调试和 editor UX；
Render Core 不负责这些内容。

`jellyframe_cli.py device-reference` 仍是桌面 control-semantics fixture，不能变成 serial fallback 或
动态加载 hardware plugin。physical provider 只能由 Device OS tooling 显式配置。

`tools/device_provider_contract.py` 已能为未来 Runtime tooling 校验有界 JSON result envelope。它不打开
transport，不能 discover 或控制开发板；physical provider 仍属于 Device OS 工作。

## Provider 调用

第一版 Device OS 应交付一个暂名 `jellyframe-device` 的可执行文件，并提供 machine-readable mode：

```text
jellyframe-device --output json discover
jellyframe-device --output json --selector <endpoint-id> install --bundle <absolute-jfapp-path>
jellyframe-device --output json --selector <endpoint-id> logs --id <app-id>
```

这只是 provider contract，不是当前 Runtime command。Runtime CLI/VS Code 只能调用已配置的 absolute
provider path，不能从 PATH 推断 executable，也不能猜测 COM port、USB identity 或 network host。诊断写入
stderr；`--output json` 的 stdout 仅有一个 UTF-8 JSON document，`--output jsonl` 为 UTF-8 JSON Lines。

退出码：`resultCode=ok` 或 `accepted` 时为 `0`；设备操作失败为 `1`；无效调用为 `2`；transport 不可用为
`3`；protocol/image 不兼容为 `4`；provider failure 为 `5`。

## Result Envelope

每个 result 使用如下有界 envelope。第一版拒绝未知 top-level field，避免 host 静默忽略 contract change。

```json
{
  "format": "jellyframe.device-provider",
  "formatVersion": 0,
  "kind": "result",
  "operation": "discover",
  "requestId": "host-000042",
  "resultCode": "ok",
  "provider": { "id": "jellyframe-device", "version": "0.1.0-dev" },
  "devices": []
}
```

`operation` 只能是 `discover`、`info`、`install`、`launch`、`stop`、`remove`、`rollback`、`logs` 或
`recovery`。适用时沿用文档化 JFDP result-code name；provider 专属值仅有
`transport-unavailable`、`protocol-mismatch`、`provider-failed`。`requestId` 由 host 生成，只含 ASCII，
最长 64 bytes。

`discover` 返回有界 `devices` array。可用 record 包含稳定 opaque `endpointId`、board/profile/image/runtime
identity、`JFDP/1`、connection state、display shape/size、enabled feature family、maximum bundle bytes 和
available storage。其他操作返回一个 selected device 和可选 typed transaction、progress、logs、recovery data。
result 最大 64 KiB，log result 最多 256 records。禁止传出 raw bundle bytes、flash address、filesystem path、
private key 或 native handle。

## JSONL 与接入

`--output jsonl` 的每行保留 format、operation、request id，含严格递增的 `sequence`，`kind` 只能为
`progress`、`log` 或唯一的最终 `result`。终态缺失、重复或乱序均是 provider failure，不能显示 install 成功。
取消时必须报告 provider 是否确认 JFDP transaction cancellation；仅 kill host process 不代表 staging 已清理。

Runtime CLI 或 VS Code 消费 provider 前，Device OS 必须交付同 image/profile 的 JFDP wire-acceptance report、
覆盖 no-device/protocol mismatch/storage full/interrupted transfer 的确定性 JSON/JSONL fixture，以及版本化的
discovery/install/update/rollback/remove/log/reconnect report。满足后 Runtime 才可增加真实 `device` command 或
VS Code device selector。
