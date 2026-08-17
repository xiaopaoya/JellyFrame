# JFDP/1 物理传输验收

> 最后更新：2026-08-18；适用版本：0.6.0-dev；协议：JFDP/1

这是第一条真实 JellyFrame Device Protocol 传输的移植侧验收契约，适用于 USB CDC、USB Serial/JTAG、UART、Wi-Fi 或 host bridge。它不实现 transport，不授予原始设备访问能力，也不把桌面 reference endpoint 伪装成硬件证据。

应先阅读 [device_runtime_zh.md](device_runtime_zh.md)。协议与 typed payload codec 由 `src/device_runtime_contracts` 负责；port 负责字节流、受限接收状态、任务交接与 Device OS adapter。

## 范围与非目标

首个 endpoint 只承载有界 `JFDP/1` 控制消息。不得借此暴露 raw flash、任意文件路径、native command 执行或通用 serial console。物理 firmware、transport driver、storage 与 launcher policy 仍是 port/Device OS 工作。

本验收只证明字节兼容性与失败隔离，不证明 panel、触控、包签名策略、性能或完整 developer image 生命周期；这些仍需独立的 A1/A2 证据。

## 规范字节输入

`tests/fixtures/jfdp_v1_wire_vectors.txt` 是 canonical fixture。每份 port 报告必须记录它的 SHA-256 与仓库 commit。它由两套独立 encoder 验证：

- C++ `jellyframe_device_runtime_contracts_tests`；
- Python `tests/tool_regression/device_reference_cli_tests.py`。

物理 endpoint 必须逐字节收发下列完整 frame：

| Vector | 方向 | 必须观察到的结果 |
| --- | --- | --- |
| `frame-discovery` | host 到 device | device 接受给定 session/request id 的 discovery request。 |
| `frame-capabilities-response` | device 到 host | response 保留 discovery type 和 id，并设置 response flag bit 0。 |
| `frame-install-begin` | host 到 device | device 在触碰 storage 前进入有界 begin decoder。 |
| `frame-install-chunk` | host 到 device | device 准确接收 transaction、offset 与四个字节。 |
| `frame-install-commit-response` | device 到 host | response 保留 commit type 和 id，并携带 typed operation result。 |

同一 fixture 中的 payload-only vector 是 codec fixture，不是可选替代品。port 必须使用公开的 `device_runtime_protocol.h` codec，或独立测试过的逐字节等价实现；不得改用 port 私有 JSON 或直接序列化 C/C++ struct。

## Stream Adapter 契约

`decode_device_frame()` 有意只接收一条完整 frame。因此 physical adapter 负责 stream reassembly，且必须：

1. 先累计完整 24-byte header，之后才能读取 length 或分配 payload buffer；整数均为 little endian。
2. declared payload 大于 4096 bytes 时，在 allocation、queue 或 storage 之前拒绝。
3. 累计到恰好等于 declared payload length，且 magic/version/type/size/CRC 全部有效后才 dispatch。
4. 把一次 read 视为任意分片：可为一个 byte、header 后半段、一条完整 frame 或多个粘连 frame；read boundary 绝不是 message boundary。
5. `DeviceInstallChunkView.bytes` 一旦跨 task、queue、DMA lifetime 或异步 storage boundary，必须先复制。receive task 不得泄漏 `payload`、`Node*`、`LayerNode*`、`jerry_value_t` 或 adapter buffer pointer。
6. receive buffer、outstanding request 和 queue depth 都必须有上限；stalled host 不能使 endpoint 持有无限 bytes 或工作。
7. malformed header、超限 length、CRC failure 或 mid-frame disconnect 时，丢弃未完成 frame，且不得产生 storage/lifecycle side effect。第一版可关闭并要求重连，不强制 stream 内 magic resync；选择的行为必须是确定的并写入报告。

`DeviceFrameHeader.flags` 的 bit 0 是 `response`。response 保留 request 的 message type、session id 与 request id。预留 flag 在 `JFDP/1` 没有语义；endpoint 可拒绝，但不得私自赋义。request correlation state 属于 port/session owner，不属于 Render Core 或 App task。

## 必须自动化的用例

使用可记录收发字节的 host probe 在目标 firmware 执行。probe 可以是 port-local；不要为了运行本清单而在平台无关 Runtime 中抢先实现假 transport。

| 用例 | 步骤 | 通过条件 |
| --- | --- | --- |
| Exact vectors | 交换表内每个完整 frame vector。 | 捕获字节与 fixture 完全一致，含 CRC/header。 |
| Header fragmentation | 每条 request 分别按 1 byte 和 header 边界 1、4、5、6、8、12、16、20、24 分片。 | 最后一字节前不 dispatch；最终 decode/response 与完整 frame 相同。 |
| Payload fragmentation | 每条非空 request 在每个 payload byte 边界切分。 | 仅在最后一字节后 dispatch 一次；无丢失/重复 chunk。 |
| Coalescing | 一次 read 交付两条有效 frame，例如 discovery 后跟 install begin。 | 依序 dispatch 两次，response 正确关联。 |
| Bad CRC | 保持 header 不变，翻转 `frame-install-chunk` 的一个 payload byte。 | 无 storage write、无 transaction progress，且 deterministic 地断开/报错。 |
| Oversize length | 发送 payload length=4097 的语法正确 header，之后不发送 payload。 | 在 4097-byte allocation、queue entry 或 watchdog delay 前拒绝。 |
| Invalid header | 分别破坏 magic、version、message type。 | 无 handler/storage action；adapter 回到文档规定的 reconnect/resync 状态。 |
| Truncation | 在 23 header bytes 后及 install chunk 中途分别断线；重连后发送 discovery。 | 无 partial mutation；新 session 正常 discover。 |
| Correlation | 一个 session 内至少使用两个不同 request id 并捕获正常 response。 | response 的 type/session/request id 均匹配且带 response flag。 |
| Repetition | 重复 connect、discovery 与一次有效 no-op/control exchange 共 100 次。 | 无 reset、panic、watchdog、receive-buffer leak 或 capability response 漂移。 |

canonical install vector 只能用于隔离的 staging fixture 或会明确清理的 transaction。它们本身不是有效的生产 `.jfapp`，绝不能意外发布 App。

## 报告与通过边界

port 报告必须包含：

- JellyFrame commit、Device OS image version、board/profile id、transport driver/configuration；
- Core package version/ABI/source identity 与 vector fixture 的精确 SHA-256；
- host probe version、endpoint identifier、transport settings；
- 五条完整 frame vector 的 byte capture 或确定性 hash；
- 每个用例的次数、reconnect、rejected frame、dispatch、storage write、registry publication 计数；
- 平台可提供时的 heap/queue high-water，以及全部 reset、watchdog、transport、storage error；
- 明确说明报告仅为 **wire acceptance**，或在更广范围已测试时链接独立的 developer-image lifecycle report。

所有必测用例均通过，且任何 invalid frame 都未造成 install mutation、registry publication 或系统 reset 时，transport 才通过本 gate。通过 wire report 不代表 A1、A2 或外部开发者试用已就绪。
