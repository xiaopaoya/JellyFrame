# Device OS A2 就绪度与实现要求

> 最后更新：2026-08-25；适用版本：0.6.0-dev；状态：实现前置阶段

## 当前结论

**WS147 provider handoff 已对已发布镜像关闭；更宽范围的 A2 仍不能交付验收，
也不能开始外部开发者试用。**

当前主线已完成平台无关控制面：JFDP/1、typed Identity/Logs payload、安装契约、Developer Image manifest、严格 provider
JSON/JSONL parser、显式 provider host client、CLI 的 `discover/info/list/install/cancel/logs` 入口，以及
VS Code 的 discovery、identity 与只读 installed-App list session 状态。这些只证明 host 不会猜测端口、不会伪造取消，也不会静默接受不匹配
的 provider 输出。WS147 `jellyframe-device@0.1.0-dev` 交付包
`jellyframe-ws147-developer-0.6.0-a2-provider-0.1.0-dev.zip` 已提供可独立安装的 wrapper、provider、依赖锁定、
配置模板、Developer Image manifest、firmware、factory image 和逐文件 SHA-256 清单，并已于 2026-08-25 复核其
13 个载荷哈希。这关闭了 provider 打包前置，但不等于已完成真实干净机器 VS Code 会话，也不等于安装后 App 已形成
渲染、输入、日志或恢复闭环。

当前发布包的明确边界是 `discover/info/list` 只读 smoke。Runtime 扩展现已具备 capability-gated 生命周期分组，
但会在 selected provider 声明各 operation 前隐藏部署、变更与 App 调试入口。为对齐新的 selected-device attestation，未发布的
`jellyframe-device@0.1.1-dev` provider source 已补齐 live cancel 与 `logs` terminal；其重新打包、host fixture
与 archive hash 记录是后续可用 mutation/lifecycle UI 的前置，不能以现有 `0.1.0-dev` 包替代。

WS147 已有 A1 storage/recovery 与 factory image 证据。基于 `b372cc4` 的 2026-08-21 workspace remeasure 还证明，
真实 22,924-byte resource bundle 可以完整传输、commit、launch、stop：有效包在 2,049 ms 内返回 `accepted`；完整
传输的损坏包在 2,969 ms 内返回 typed `integrity-failed`，且没有 registry publication。storage owner 将 4 KiB
inspection workspace、4 KiB sector cache 和 1 KiB transport scratch 放在 device task stack 之外；24,576-byte 配置栈的
minimum free stack 为 14,468 bytes。

A2 provider handoff 对 firmware `afdcf75` 与已发布 WS147 manifest 为 **PASS**：identity cross-match、in-flight
cancellation、durable lifecycle 和 30 次 mixed cycle 均通过。更宽范围的 A2 仍为 **partial**，因为该证据尚未关闭
干净机器上的 VS Code 产品流程，以及真实已安装 App 的 panel/input 验收。这份 provider handoff 报告不是外部试用
发布签字。

## 所有权与完成度

| 层 | 已完成 | 必须完成后才能进入 A2 验收 |
| --- | --- | --- |
| Render Core | 独立 Core package、profile/ABI、平台无关渲染与输入契约 | 无 A2 阻塞项；不得在 provider 中复制渲染逻辑 |
| JellyFrame Runtime/Tools | `.jfapp`、bundle 检查、manifest/provider contract、CLI host client、provider handoff contract | 干净机器 VS Code 部署/日志会话、用户可读错误映射与端到端 tool regression |
| Device OS | A1 的 launcher/registry/staging/recovery 基础；WS147 provider handoff 与版本化 provider 交付 | 安装/配置真正的 `jellyframe-device` provider，并完成已安装 bundle 到 AppHost/renderer/input/log 的绑定 |
| WS147 port | JFDP wire、持久 lifecycle、factory recovery、真实 resource commit、已测 profile 的 provider handoff | 真实已安装 App 的 panel/input 证据；该镜像不再有 provider lifecycle blocker |

## Device OS 必需实现

### 1. Provider 进程与会话

实现一个独立、显式安装的 `jellyframe-device` 可执行文件。它只接受用户配置的 selector，不扫描并自动
选择端口；`discover` 返回稳定 opaque `endpointId`，`info` 复核同一 endpoint。provider 不得把 raw serial
console、flash 地址、文件系统路径、指针或密钥输出为 JSON。

provider 使用 [device_tool_provider_contract_zh.md](device_tool_provider_contract_zh.md)：

- `--request-id` 必须原样回显；JSON/JSONL stdout 不得混入日志。
- `discover/info` 使用 JSON；`install/logs` 使用严格递增 sequence 的 JSONL。
- `install` 只发送经过 Runtime 打包的 `.jfapp`；不得把 host 文件路径发给设备。
- `cancel` 必须将 JFDP abort 的实际结果映射为 `cancellation.confirmed`；断线、kill provider、MCU reset
  都不是成功取消。
- 退出码、resultCode 与 stderr 诊断遵循同一契约，不得互相矛盾。

### 2. 设备侧 JFDP 适配

endpoint 实现有界 frame decoder、session/request correlation、超时和 reconnect。每个 install 采用已有
`DeviceInstallStore` 状态机：begin/write/verify/commit/abort；只在原子 publish 后向 provider 报告成功。
provider 必须可读取 typed AppList、Recovery、progress 与失败 result，不能用固定 fixture 或解析串口文本代替。

必须定义下列 ownership：USB RX/TX buffer 仅归 transport task；install bytes 在跨 task 前复制；registry/storage
由 Device Runtime 单独拥有；UI/App task 不取得 provider 或 transport 句柄；任何 JerryScript、DOM、Node、
LayerNode、arena 地址均不得跨任务/跨进程传递。

### 3. 已安装 App 执行闭环

这是 A2 当前最大的缺口。首个实现现已将复制后的
`runtime.script: "classic"` bundle 路由至 worker-local
`ScriptTaskWorkerRuntime`；静态 bundle 继续走 native UI 路径。它已具备实机
验证条件，但在获得以下 WS147 证据前不得标为验收通过。完成
`AppInstalledBundleBinding` 到实际运行时的绑定：

1. launcher 从已发布 registry 选择 bundle，并用 bundle reader 加载资源。
2. 创建 App Runtime（需要时 script worker）与 Render Core document；installed-script 路径会在 worker 启动前复制
   HTML、CSS 与每个 package-local classic script。JerryScript、DOM、layout/layer data 只由 worker 持有，UI 只接收
   sealed value frame；资源、frame、input 和 service 仍只通过已有 value-only 协议交接。
3. UI task 解码/呈现 frame，并把输入转为 value-only packet；App fatal/load failure 返回 protected launcher。
4. App-scoped log 由 Runtime/launcher 带 app ID、generation、timestamp、level 输出；provider 只转发 typed copy，
   每个 response 至多 11 条，每条 message 至多 255 bytes。
5. stop/remove/rollback 与正在运行的 App 有明确顺序：停止输入和 service、等待 teardown、再改变 registry；
   不得让旧 frame 或旧 generation 在新 App 后 present。

### 4. Developer Image 发布面

交付一个版本化 Developer Image 包：firmware、factory raw image、manifest、recovery procedure、provider
版本/安装方式、支持的 profile/feature family、bundle/storage 上限和 provenance。provider 必须拒绝与 manifest
不匹配的 device/image，而不是尝试兼容性猜测。

## 必须先通过的自动化 Fixtures

按 [device_provider_port_acceptance_zh.md](device_provider_port_acceptance_zh.md) 实现 no-device、image mismatch、
transport unavailable、storage full、interrupted install、confirmed/unconfirmed cancel、bounded logs。另增加：

1. request ID/operation/sequence 错误均被 host 拒绝。
2. 同一 App 的 install -> update -> rollback -> remove，registry generation 单调且无 partial publish。
3. launch/load/runtime fatal 后 launcher 恢复，旧 service/frame/input 全部失效。
4. reconnect 不会重复 commit、重复 log 或把过期 response 归给新 session。

fixtures 必须可在无板卡 host 上运行；它们测试 provider contract，不冒充实机证据。

## WS147 A2 实机验收顺序

1. 固定一份已发布 Developer Image/manifest/provider 版本，复核 JFDP wire 与 A1 recovery 没有回归。
2. `discover -> info` 与 manifest identity 完全匹配，包括 JFDP Identity 中的 Render Core version、revision、ABI 与
   feature families。
3. 从 VS Code 或 CLI 对一个实际 `.jfapp` 执行 install；记录 JSONL、registry、launch marker、panel 及输入响应。
4. update、rollback、remove、load failure、runtime fatal、mid-install cancel 各做一次 reconnect/reboot 后检查。
5. 读取 app-scoped logs，确认诊断不污染 provider stdout，且不存在 watchdog、reset loop、DMA/SPI/panel 错误。
6. 完成至少 30 次混合生命周期循环，再以版本化 report/summary/raw log/flash log 归档。

步骤 1-4 已由 `provider-handoff-afdcf75-20260821` 报告覆盖，但该报告不宣称 panel/input 行为。installed-script
执行路径也尚无实机证据。没有真实安装 classic-script App 的 panel/input、fatal recovery 证据以及干净机器 VS Code
流程时，更宽范围 A2 仍必须标为 `partial`，不得用 A1 或 desktop reference 填补。

已安装 classic-script 路径的独立实机步骤、fixture 与证据要求见
[ws147_installed_script_task_acceptance_20260826_zh.md](ws147_installed_script_task_acceptance_20260826_zh.md)。

## A2 出口

只有满足以下条件才可关闭 A2：干净作者机器无需 ESP-IDF，安装 provider 与官方 image 后，在 VS Code 完成
`new -> check -> package -> discover -> install -> live logs -> update -> rollback -> remove`；每个失败能定位到
package、manifest、provider、transport、registry、Runtime 或 port；所有受控异常回到 launcher，无未解释 reset、
watchdog、registry 损坏或越权 host 操作。A2 关闭后才讨论 A3 小范围外部试用。
