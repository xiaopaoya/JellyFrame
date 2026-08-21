# Developer Image 生命周期验收

> 最后更新：2026-08-21；适用版本：0.6.0-dev；协议：JFDP/1

这是首个官方 Developer Image 的 A1-2 验收 gate。它必须在 [JFDP/1 物理传输验收](jfdp_v1_port_acceptance_zh.md) 之后执行，但不取代后者；它证明一个具体板卡镜像的持久 staged install、registry 发布和 launcher 恢复。它不是通用文件系统测试、固件升级协议、市场、远程下载服务或性能 benchmark。

## 前置 gate 与范围

### 已接纳的 WS147 基线

首个已发布 WS147 Developer Image 于 2026-08-19、source revision
`fbf10784ac8ce38f41ced40fa013a43564c992c8` 通过本 gate。其 manifest 为
`org.jellyframe.ws147.developer@0.1.0-dev`；factory image 写入已经 hash 验证，启动后以
`RegistryInvalid` 进入 protected launcher，并可无需重新烧录地经由 JFDP/1 安装并启动
`org.jellyframe.device.lifecycle@1.0.0` fixture。已发布报告记录 9 transmitted、9 received、0 timeout，
且 panic、watchdog、reset-loop、DMA、SPI、panel、present 错误均为零。这确认 A1-2 lifecycle 与
image manifest/factory-recovery 子项；它不是 A2 的 installed-App rendering、input 或作者工具证据。

开始前，精确 image/profile 必须已有通过的 JFDP/1 wire report。WS147 native USB Serial/JTAG 已在 2026-08-18 以 fixture SHA-256 345d2c6bafadfdfab86af216b428c437fd34e0b9b3adfd16687662da494ef3bb 通过 wire-only gate。这份证据不等于本生命周期 gate；之后 image 发生变化时，必须评估兼容性，必要时重新出具 wire report。

port 必须复用 device_runtime_contracts/device_install_transaction.h 的 DeviceInstallStore，不得复制 transaction state machine：

- begin_staging、write_staging 与 verify_staging 绝不发布 App。
- commit_staging 只有在新 registry entry 已耐久且原子发布后才可返回 true；返回 false 时旧 registry 不变。
- abort_staging(transaction_id) 必须幂等且仅作用于该 transaction，包括 begin 失败时已部分创建的 staging object。
- DeviceInstallChunkView.bytes 在异步 storage 或跨 task 交接前必须复制。

`DeviceInstallStore::verify_staging()` 必须通过 `DeviceBundleReader` 与
`inspect_device_bundle(...)` 检查 staging bytes，并传入 board profile 明确的 bundle/resource/summary budget。
发布前必须持久化或重新验证已接受的 descriptor。AppList 与 Recovery response 使用 typed
`DeviceAppListPayload`、`DeviceRecoveryDetailPayload` codec。installed app 启动与 failure fallback 使用
`AppInstalledBundleBinding`，不得使用固定 fixture loader 或 port-private registry-to-HTML shortcut。

面向 device 的 `inspect_device_bundle(...)` overload 接收 `DeviceBundleInspectionWorkspace`。port 必须把这块
最大 4 KiB summary workspace 放在 storage owner 或其他显式预算的长期对象中，绝不能放在 JFDP、UI 或 script task
call stack；sector cache 同样遵守此规则。desktop convenience overload 不能作为 board profile 的验收证据。

第三方 bundle 必须置于不可变 firmware、launcher 和 fallback assets 之外。JFDP 只能提供文档化的有界操作；不得成为 raw flash、任意文件或 native command 通道。

## 必需的耐久模型

报告必须写明 partition/filesystem 方案、record format、bundle/staging 上限及 boot-time recovery 算法。journal、双 registry slot 或 generation record 均可，只要具备以下性质：

1. committed app record 只指向完整、已验证的 bundle 和 version/identity；registry 不得指向未完成 staging bytes。
2. replacement 在新版本原子发布前保留可启动的已提交版本；allow_downgrade=false 必须拒绝较低版本且不改动该 record。
3. 中断或无效 staging 在重启后不可见，并通过有界 recovery 回收；它不能永久阻塞后续 install。
4. registry decode 必须有界且能安全处理损坏。invalid、torn 或 checksum-failed metadata 必须进入 protected launcher/fallback，不得运行任意 bundle；报告要给出 recovery diagnostic。
5. launcher/fallback 不能被 app-library 操作删除。App load 失败、runtime fatal 或 app budget recovery 后必须回到该处，不得要求重新烧录或整机 reset。
6. remove 和 rollback 都是耐久 lifecycle 操作。remove 不得删除受保护系统资产；rollback 必须选择保留的已验证版本，或以文档化的 not-found/unsupported 返回且不损坏 active record。

成功 commit 后清理旧 staging 不是发布的一部分。发布后、清理前掉电仍必须启动已提交 registry。

## Fixture 与中断规则

至少使用两个 app id 相同、可通过预检、可见 version string 不同的小 .jfapp，另加一个 malformed 或 integrity-failing bundle。每个 fixture 必须在 app-scoped log 或 launcher state 留下确定 launch marker；不要求面板照片。

port 必须提供可重复的 interruption hook：test-only controlled reset、storage-adapter failpoint 后 reboot，或记录清楚的物理断电均可。仅 host disconnect 不足以证明 durable-write case。hook 不得绕开正常 storage 代码修改 registry，也不得在 reboot 后手工修复。

每个 reboot case 都重新 discovery，最后读取 installed-app state，并启动期望的 App 或 protected launcher。image 可提供时记录 registry generation/version 和 recovery reason。

## 强制矩阵

| 用例 | 步骤 | 通过条件 |
| --- | --- | --- |
| 基线安装 | 安装 fixture A、commit、reboot、list、launch。 | A 已发布，出现 marker，且无可见 staging record。 |
| 更新与 rollback | 对 A 的 app id 安装 B、commit、reboot 并启动 B，再 rollback 后 reboot。 | B 仅在 commit 后替换 A；rollback 恢复 A 或文档化的保留版本。 |
| 默认拒绝降级 | B 已发布时以 allow_downgrade=false 提交低版本 A。 | 被拒绝；重启后 B 仍列出且可启动。 |
| 显式降级 | 仅 profile 声明支持时，以 allow_downgrade=true 重复。 | 文档化 target 仅在耐久 commit 后成为 active。 |
| begin 中断 | staging 创建后中断并 reboot。 | 不发布新版本；旧 App/launcher 可启动；stale staging 被回收。 |
| 中途写入中断 | chunk write 中断（含接近最后的 chunk）后 reboot。 | 不列出或启动 partial bundle；旧 committed state 保留。 |
| verify 中断/失败 | verification 中断一次，再使用无效 CRC/bundle。 | 不发生发布；retry 或 abort 后回到可用 idle state。 |
| commit 中断 | registry publication 前及耐久 publication 后、可选 cleanup 前分别中断。 | 前者保留旧 state；后者仅保留一个完整新 state；不得见 torn registry。 |
| commit/storage 失败 | 在 adapter-private preparation 后使 commit_staging 失败。 | 稳定失败；不出现新 entry；后续 install 成功。 |
| 断线与 abort | transfer 中途断线、重连并开始新 transaction；显式 abort 两次。 | partial staging 未发布；重复 abort 无害；新 transaction 完成。 |
| Remove | 支持时删除前台与非前台 fixture，随后 reboot。 | 被删除 App 无法启动；launcher 完整；保留 rollback 数据遵从策略。 |
| Bad-app recovery | 选择 validation/load 失败包；可用时触发一次受控 app fatal。 | 回到 launcher/fallback，无 watchdog、reset loop 或 registry 损坏。 |
| Registry corruption | 经 test storage path 注入 torn/corrupt metadata 后 reboot。 | parser 有界；启动 launcher/fallback；不运行任意 bundle；记录 diagnostic。 |
| 重复性 | 至少 30 次 install -> update -> rollback -> remove 加 disconnect/reconnect。 | 无 reset、watchdog、publication mismatch、无界 staging 增长或不可恢复 install failure。 |

不支持的 lifecycle operation 可以拒绝，但不得静默成功。每次拒绝必须有稳定 JFDP/1 result code 和 unchanged-state assertion。

## 必需证据与通过判定

版本化报告目录必须包含 Runtime/Device OS commit；image/profile 与 storage 配置；JFDP fixture SHA-256；未提交 port change（如有）；image 和 fixture hash；build/flash log；raw host capture；machine-readable summary；每个 interruption point 与 reboot 后 observation。

还必须包含 staging begin/write/verify/commit/abort、registry publication、recovery/fallback、rejected request、reconnect、reset、watchdog 计数。可用时报告 memory/queue watermark，否则说明限制与结构上限。本 gate 可以通过 typed binding 与 resource-read 结果证明 launch/fallback；已安装 App 的 DOM/panel 渲染、可视对比与输入属于 A2 的端到端证据，不能用它们替代或冒充本 storage lifecycle gate 的结论。

真实 resource verification 出现 timeout 或 latency regression 时，必须归档 phase telemetry：transport CRC、JFAPP
header/bundle CRC、summary parse、resource validation、registry publish、response write；还要归档执行 task 的
configured stack、minimum stack watermark、workspace/cache placement 以及 reader call/byte count。provider timeout
不能作为最终结果：port 必须定位到一个有界 phase，并在文档化 provider timeout 内返回 typed failure 或成功 commit。

报告必须分开给出 wire acceptance、storage lifecycle、launcher recovery 与 tooling verdict。reference dispatcher 或仅断线测试不能证明 persistent interruption safety。

只有所有适用 case 均确定性通过、每个失败/中断发布都保留先前 committed state，且所有 recovery 均无需 watchdog、MCU reset loop 或重新烧录而回到 protected launcher/fallback 时，A1-2 才能通过。WS147 的 board/profile manifest 与 factory recovery procedure 已分别通过；A1 仍须等待 A2 作者工具流程验收。
