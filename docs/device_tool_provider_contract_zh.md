# Device OS 工具 Provider 契约

> 最后更新：2026-08-25；适用版本：0.6.0-dev；状态：WS147 provider handoff 已通过；更宽范围 A2 待完成

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
jellyframe-device --output json --request-id <host-id> discover
jellyframe-device --output json --request-id <host-id> --selector <endpoint-id> info
jellyframe-device --output json --request-id <host-id> --selector <endpoint-id> list
jellyframe-device --output jsonl --request-id <host-id> --selector <endpoint-id> install --bundle <absolute-jfapp-path>
jellyframe-device --output json --request-id <host-id> --selector <endpoint-id> launch --id <app-id>
jellyframe-device --output json --request-id <host-id> --selector <endpoint-id> stop --id <app-id>
jellyframe-device --output json --request-id <host-id> --selector <endpoint-id> remove --id <app-id> [--keep-data]
jellyframe-device --output json --request-id <host-id> --selector <endpoint-id> rollback --id <app-id>
jellyframe-device --output jsonl --request-id <host-id> --selector <endpoint-id> logs --id <app-id>
jellyframe-device --output json --request-id <host-id> --selector <endpoint-id> recovery
```

这是 provider contract。Runtime CLI 通过已配置 provider path 暴露对应的 `device` command；它不内置 provider，
也不能从 `PATH` 推断 executable、猜测 COM port、USB identity 或 network host。已安装同一 App identity 时，
`install` 也是 update。VS Code 在 provider fixture 通过后复用同一 client。诊断写入 stderr；`--output json`
的 stdout 仅有一个 UTF-8 JSON document，`--output jsonl` 为 UTF-8 JSON Lines。

退出码：`resultCode=ok` 或 `accepted` 时为 `0`；设备操作失败为 `1`；无效调用为 `2`；transport 不可用为
`3`；protocol/image 不兼容为 `4`；provider failure 为 `5`。

每次 machine-readable 调用都必须传入 `--request-id`。Runtime host 生成该值、原样传递，并拒绝 request ID
或 operation 与请求不一致的结果；provider 不得自行生成或改写它。

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

`operation` 只能是 `discover`、`info`、`list`、`install`、`cancel`、`launch`、`stop`、`remove`、`rollback`、`logs` 或
`recovery`。适用时沿用文档化 JFDP result-code name；provider 专属值仅有
`transport-unavailable`、`protocol-mismatch`、`provider-failed`。`requestId` 由 host 生成，只含 ASCII，
最长 64 bytes。

`discover` 返回有界 `devices` array。可用 record 包含稳定 opaque `endpointId`、board/profile/image/runtime
identity、`JFDP/1`、connection state、display shape/size、enabled feature family、maximum bundle bytes 和
available storage。feature-family ID 必须唯一、使用小写 ASCII `[a-z0-9][a-z0-9.-]{0,95}`，且最多 64 项。
除 `discover` 外，每个 `ok`/`accepted` selected operation 都必须返回一个 typed `device`，其 `endpointId`
必须精确等于调用方传入的 selector；host 必须拒绝缺失或不匹配的 attestation。失败结果可省略该字段，以保留
`transport-unavailable` 等可诊断状态。result 最大 64 KiB。禁止传出 raw bundle bytes、flash address、filesystem
path、private key 或 native handle。

### 由能力声明的生命周期控件

discovery device record 可选携带 `capabilities.supportedOperations` array。为兼容已发布的
`jellyframe-device@0.1.0-dev` 包，该字段可以缺失；一旦出现，它就是作者侧生命周期控件唯一可信的 opt-in
列表。每项必须唯一，且只能是 `install`、`cancel`、`launch`、`stop`、`remove`、`rollback`、`logs`、
`recovery` 之一；`discover` 有意不在其中，因为它是 host 建立 session 的动作，而非 selected device capability。

VS Code 扩展只会在当前选中设备声明相应值时显示部署、变更和 App 调试入口。字段缺失或为空时，仅显示只读的
发现、身份和 App 列表流程。即使已声明，provider 仍可返回 typed result code 拒绝请求；能力声明不能替代
selected-device attestation、破坏性操作确认、有界 JSONL 校验或 integration acceptance。尤其是仅声明 `cancel`
并不能证明 in-flight cancel 安全，仍必须由 provider delivery 与实机验收分别证明。

`info` 以 typed `JFDP/1 Identity` response 为依据。除 discovery 使用的 device record 外，它还必须证明
`imageId`、`profileId`、`imageVersion`、`renderCoreVersion`、40 字符小写 source revision、非零 Render Core ABI
和完整 feature-family set。稳定 wire bit 分别映射 `core.document`、`core.paint`、`css.flex-grid`、
`css.modern-paint`、`forms.advanced`、`graphics.canvas2d`；document 与 paint 必须存在。provider 必须报告设备
实际返回的值，不能猜测 host configuration。成功的 `info` result 必须同时包含 `device` 和只含这 7 个
camel-case field 的 `identity` object；其中 `profileId`、`imageVersion` 必须与 `device` 相同。

`list` 映射 JFDP AppList payload，返回 `apps` 与 `registryGeneration`，二者必须同时出现。最多 24 条 entry；
每条 entry 必须且只能包含 `appId`、`versionName`、`versionCode`、`bundleBytes`、`state`（`installed`、`disabled`
或 `failed`）及 `rollbackAvailable`。成功的 `list` 必须包含这两个字段。

`recovery` 映射 JFDP recovery-detail payload。成功 result 必须且只能携带 `appId`（仅 device-wide recovery
可为空）、`registryGeneration`、`recoverySequence`、`reason`、`launcherActive`、`appDisabled` 与
`rollbackAvailable`。`reason` 只能是 `none`、`registry-invalid`、`staging-discarded`、`app-load-failure`、
`app-runtime-failure`、`app-budget-exceeded` 或 `launcher-fallback`。

可选 `transaction` 必须且只能是 `id`、`receivedBytes`、`expectedBytes`、`complete`、`active`；byte count
均为 uint32，且 received 不得大于 expected。可选 `progress` 必须且只能是 `completedBytes` 与 `totalBytes`，
并遵守同一范围。成功的 terminal `logs` 只包含 `logSummary`，且必须且只能是 `returnedRecords` 与
`droppedRecords`；具体记录在 JSONL event 中，禁止在终态重复列表。这与 typed JFDP Logs response 对齐：
每个 response 至多 11 条，每条包含 `level`、`appId`、uint32 `generation`、十进制字符串 uint64
`timestampMs` 与最多 255 UTF-8 bytes 的 `message`。十进制字符串避免 JavaScript client 丢失时间戳精度。
provider 不得改为序列化 registry 或 task-private structure。

## JSONL 与接入

`--output jsonl` stream 最多 256 KiB、1024 条非空行。每行都带有 `format`、`formatVersion`、`operation`、
`requestId`、`sequence` 和 `provider`；`sequence` 是正的严格递增 uint32。只有 `install` 可以输出
`progress` event，且只能额外携带 `progress.completedBytes` 与 `progress.totalBytes`；只有 `logs` 可以输出
`log` event，record 必须且只能是 `level`、`appId`、`generation`、`timestampMs`、`message`，其中 `level`
只能是 `debug`、`info`、`warn` 或 `error`。唯一最终 `result` 使用普通 result envelope 加 `sequence`，且必须
是最后一行；成功 selected stream 的终态同样必须携带与 selector 一致的 typed `device`。成功 `logs` stream 的
`logSummary.returnedRecords` 必须精确等于已输出 log event 数量。终态缺失、
重复或乱序，或 stream 内任何 identity 改变均为 provider failure，不能显示 install 成功。`cancel` 必须返回 boolean `cancellation.confirmed`；Runtime 只将 `true` 视为
取消成功。仅 kill host process 不代表 staging 已清理。

在向作者交付 physical provider 或把它接入 VS Code 部署 UI 前，Device OS 必须交付同 image/profile 的 JFDP
wire-acceptance report、已校验的 `device_image_manifest_zh.md` record、覆盖 no-device/protocol mismatch/storage
full/interrupted transfer 的确定性 JSON/JSONL fixture，以及版本化的 discovery/install/update/rollback/remove/log/
reconnect report。host 必须在部署前将 discovery data 与该 manifest 匹配。在此之前，CLI command 仅是显式的
contract client，不得被当作 physical provider 已存在的证据。
