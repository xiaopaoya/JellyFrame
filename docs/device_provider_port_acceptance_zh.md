# Device Provider 移植验收

> 最后更新：2026-08-21；适用版本：0.6.0-dev；协议：JFDP/1

这是物理 Device OS provider 的 A2 交接单。它验证 `jellyframe_cli.py device` 使用的
host-process 边界，不替代 JFDP wire、Developer Image lifecycle、panel 或 touch 验收。
完整 A2 就绪边界及已安装 App 执行要求见
[device_os_a2_readiness_zh.md](device_os_a2_readiness_zh.md)。

## 范围

port/Device OS 交付一个显式选择的可执行文件，暂名 `jellyframe-device`。它负责 endpoint discovery、
USB/serial dependency、JFDP framing、device telemetry 与全部 board-specific state。JellyFrame CLI/VS Code
绝不猜测 COM port，也不运行 serial fallback。

每次 machine-readable 调用都必须接收 host 生成的 request ID：

```text
jellyframe-device --output json --request-id <id> discover
jellyframe-device --output json --request-id <id> --selector <endpoint> info
jellyframe-device --output jsonl --request-id <id> --selector <endpoint> install --bundle <absolute.jfapp>
jellyframe-device --output json --request-id <id> --selector <endpoint> cancel --transaction-id <id>
jellyframe-device --output jsonl --request-id <id> --selector <endpoint> logs --id <app-id> --limit <1..11>
```

provider 必须精确实现 [device_tool_provider_contract_zh.md](device_tool_provider_contract_zh.md) 的
result/JSONL schema。protocol 与 operation diagnostics 只能写入 stderr；stdout 只能是一份 JSON result
或一条有界 JSONL stream，不得混入 banner 或 serial output。

## 必需 Fixtures

提供无需开发板即可在 host 运行的确定性 provider fixture：

| Fixture | 预期 host 结果 |
| --- | --- |
| no device | `discover` 返回 `ok` 与空 `devices` |
| wrong image/profile | device record 与所选 manifest 不同，host 拒绝 |
| transport unavailable | exit `3`，terminal 为 `transport-unavailable` |
| install storage full | JSONL terminal 为 `storage-full`，不发布新 App |
| interrupted transfer | JSONL 返回稳定 failure/cancellation，staging 不发布 |
| confirmed cancellation | 只有 JFDP transaction 已取消后，`cancel` 才返回 `cancellation.confirmed=true` |
| unconfirmed cancellation | `cancel` 返回 `confirmed=false` 或 failure；host 必须失败 |
| log bounds | `logs` 不超过 requested limit，且绝不超过 11 条 typed record；每条 message 至多 255 bytes |

所有 fixture 必须在每条 response 保留输入的 request ID 与 operation。不得把 desktop reference registry
伪装为物理设备。

## WS147 实机验收

在已发布 WS147 Developer Image 上运行 provider，记录 provider version、Device OS commit、Runtime/Core
provenance、manifest SHA-256、board、USB endpoint identity、firmware hash 与 build configuration。

1. `discover` 精确返回已发布 board/profile/image/runtime、display、feature families、bundle limit 与当前 storage。
2. 对返回的 opaque selector 执行 `info`，同时返回 `device` 和匹配的 typed JFDP `identity`，证明 identity 一致，
   并包含 Render Core version、source revision、ABI 与完整 feature-family set。
3. `list` 返回带 registry generation 的 typed AppList，`recovery` 返回 typed recovery record，不得解析 serial text。
4. 安装一份已检查 `.jfapp`，归档 JSONL progress/terminal，再验证 App 可 launch。
5. 开始第二次 install，经 provider `cancel`，并在 reconnect 或 reboot 后证明旧 committed App 仍可 launch。
6. 读取有界 app-scoped logs，证明 stdout JSONL 未混入 diagnostics。
7. 触发 manifest mismatch 与 storage-full/oversize bundle；两者均不得发布 partial App。

不得以 kill host process、仅 USB disconnect 或 MCU reset 声称取消。JSON 中不得暴露 flash address、任意文件、
raw serial console、private key 或 JFDP handle。

## 证据与出口

2026-08-21 的 `provider-handoff-afdcf75-20260821` 报告已关闭已发布 WS147 镜像的 provider handoff。它包含此前
workspace 定向证据，并补齐同镜像 Identity matching、真实 in-flight abort、durable update/rollback/remove、
reconnect/reboot 与 30 次 mixed cycle。它不关闭更宽范围的 Device OS A2 gate，也不授权外部试用。

归档版本化目录，至少包含 `report.md`、`summary.json`、每个 case 的 direct provider stdout（命名为
`provider.stdout.raw.jsonl` 或 `.json`）、provider stderr（`provider.stderr.raw.log`）、单独保存的 CLI stdout
（`cli.stdout.json`）、CLI stderr、CLI command、provider fixture source、manifest、build/flash log 与精确 `.jfapp`
hash。CLI pretty-printed output 不是 provider raw stdout。`summary.json` 必须分别记录 discovery、
identity matching、install、cancellation、logs、reconnect/reboot、watchdog/reset 与 transport/panel error count。

所有 fixture 与 WS147 run 都以同一已发布 image identity 通过时，本 A2 provider handoff 才通过；
`provider-handoff-afdcf75-20260821` 已满足该 gate。主线下一步是完成干净机器的 VS Code device view，以及真实已安装
App 的 panel/input 验收；它本身不放行外部开发者试用。
