# JellyFrame 主线路线图

> 最后更新：2026-08-25；适用版本：0.6.0-dev；状态：活动计划的唯一来源

## 先决判断

`0.5.0` 已发布。其后的桌面 `trial`、registry 和 Win32 壳证据证明的是**桌面作者流程**，不是可对外招募的硬件开发者试用。此前计划隐含要求作者自行准备开发板、构建 ESP-IDF、移植 port、编写设备 OS 和烧录调试；这不是合理的 App 作者前提。

因此，外部试用的前置条件改为：开发者不安装 ESP-IDF、不修改固件，使用第一方 developer image 和 VS Code/CLI 完成 App 的检查、安装、更新、启动、恢复和日志查看。桌面流程继续作为开发与预检工具，不再单独构成外部试用放行条件。

本路线图只列出尚未关闭的目标。完成项进入 `CHANGELOG*`、测试和归档证据，不继续占用执行队列。每个阶段必须通过其出口条件，不能由后续优化或特性工作替代。

## 当前基线

- 保留历史的 `xiaopaoya/JellyFrame-Render-Core` 仓库现在拥有物理 Core 分支。首个带签名的 `v0.6.0` release 是历史基线；Runtime `0.6.0-dev` 当前精确锁定 Core `v0.6.1`、ABI `1` 与 source identity `105d0166...b797c52b`。CI 会下载该 release artifact、校验 archive SHA-256 `f9d24aca...e18c7`、安装后运行 Runtime package-consumer tests。in-tree provider 只保留给同步本地开发。Core ABI `1` 明确以安装后的 `render_core/` headers 作为 C++ consumer surface；当前没有隐藏 header tier 或 C ABI。2026-08-19 已补齐 Core-only 与 Device contracts 的 CMake 边界回归：Core-only 不能创建 contracts target/test，contracts-only 仍可独立构建，source archive/install/package/source-override 闭环也已复核。
- App Runtime 已具备 `.jfapp` 生命周期、registry 参考语义、JerryScript 可选桥接，以及 script worker 的 session/generation/epoch、value-only frame/input/service/fatal 协议。P3 的 WS147 worker、service、恢复与 mixed soak 验收已关闭。
- WS147 的 value-frame v2 dirty/recovery fixture 已通过；全屏 rounded/gradient workload 的优化归因已完成，但仍不能达到 30 FPS。Canvas 还没有真实 host binding，保持 `not-tested`。
- `device_*` 的 JFDP/1 framing、capability、typed status/progress payload 与 staged-install controller 已有独立的 `device_runtime_contracts` source owner。WS147 native USB Serial/JTAG wire、A1-2 persistent lifecycle 与已发布镜像的 provider handoff 均已关闭。`provider-handoff-afdcf75-20260821` 通过同镜像 Identity matching、真实 in-flight cancellation、durable update/rollback/remove 与 30 次 mixed cycle。已发布 `jellyframe-device@0.1.0-dev` 只用于 `discover/info/list` read-only smoke；未发布 `0.1.1-dev` 源线补齐 selected-device attestation，须重新打包并完成 host fixture 后才可进入 mutation/lifecycle UI。已发布 Developer Image 具有严格 manifest 与 hash 验证的 factory recovery image；这仍不等于干净机器 VS Code 产品流程或已安装 App 的 panel/input 行为已完成。
- 当前开发线是 Runtime `0.6.0-dev` / Core `0.6.0-dev`。1.0 前不维护历史 package 兼容线。

## 已关闭的性能阶段

### O1：Value-frame v2 基线与低风险栅格优化

状态：**关闭，不再按微优化循环投入**。

- dirty/recovery 的受限 WS147 fixture 已稳定通过；该结果不授权 retained replay、framebuffer reuse 或通用 30 FPS 承诺。
- full-frame rounded/gradient fixture 已确认主要时间在 shadow 和 coverage/composite；同类 copy/span 微优化已到达收益边界，最新结果仍约 `340 ms` end-to-end p95。
- packed RGB565 与双 DMA buffer A/B 没有形成默认路径收益，保持不用。

后续性能工作只能由真实官方 image workload 触发，并必须先取得分相 telemetry。允许的下一类工作是：设备侧真实 dirty workload、文本/图像 host callback，或证明 framebuffer 内存/带宽为主导后的 tile/scanline RFC；不能再以合成全屏 fixture 猜测产品帧率。

## 轨道 A：官方开发板与 Device OS

目标：让 App 作者不接触板级构建、分区和驱动即可使用第一方硬件。

### A1：首个官方 Developer Image

目标板：ESP32-S3 Waveshare Touch LCD 1.47，profile `rect-172x320`。

storage/recovery 与 image-identity slice 已关闭：受保护 launcher/fallback、App staging storage、持久 registry、恢复启动路径、实际 USB JFDP/1 transport、WS147 measured manifest 与经 hash 验证的 factory recovery procedure 均已合入。端口只报告实测 profile/capability/budget，不能把未链接 feature 或未绑定 Canvas 宣称为可用。

出口：在干净机器上使用文档化工具完成 flash 后，连续 install/update/rollback/remove/坏 App recovery/断线重连，不需要重新烧录，不发生 watchdog、reset 或 registry 损坏。WS147 port handoff 已满足物理 lifecycle 部分；A1-2 本身不能替代 A2 对实际 App 渲染和作者工具流程的验收。

### A2：作者工具接入

1. CLI 选择真实设备并显示 profile、存储、能力和生命周期状态。
2. VS Code 增加设备视图和连接、部署、更新、启动、停止、删除、日志及错误入口；桌面壳调试与设备调试保持不同会话与报告。
3. 设备 telemetry 是唯一的设备性能来源；Win32 预览只用于视觉和流程预检。

WS147 provider handoff 子 gate 已由 `provider-handoff-afdcf75-20260821` 关闭：同镜像 identity、in-flight cancellation、durable lifecycle 与 30 次 mixed cycle 均通过。先使用已发布 provider 完成发现/身份/App list 的 read-only smoke；随后重新打包 `0.1.1-dev` provider，并以它完成 mutation/lifecycle UI。更宽范围作者工具出口仍要求干净机器从 VS Code 完成 `new -> check -> device install -> live log -> update -> rollback`，并取得真实已安装 App 的 panel/input 证据；错误信息能定位到 package、transport、registry、Runtime 或 port。

### A3：受控外部开发者试用

仅在 A1/A2 完成后开始。试用者不得需要 ESP-IDF。首轮反馈以可复现 App package、设备日志和版本化 developer image 为准；能够阻塞安装、运行、恢复或文档化能力的反馈才升级为主线 P0。

出口：独立用户完成完整生命周期；没有 unexplained reset、App 库损坏或开发流程阻塞。

## 轨道 B：Render Core 独立工程与仓库

目标：将不同节奏的渲染引擎、Runtime 和 Device OS 分开治理，而不引入子模块式日常开发负担。

### B0：拆仓规范冻结

1. 发布并维护 `render_core_release_policy`：版本、ABI、feature profile、archive、签名、依赖 lock 和本地 override 的唯一规则。
2. 确定物理迁移使用保留历史的 filtered repository export：`jellyframe-render-core` 保留 Render Core 文件及其相关 CMake/tests/docs 的历史；`jellyframe` 保留整体产品历史和一笔明确的 package-consumer 迁移提交。不得用无历史拷贝替代。
3. 建立导出后验证：独立 Core CI、Runtime pinned-package CI（含精确 source identity）、Runtime local-source override CI 和 Device OS profile consumer CI。

出口：三个 consumer 都只通过公开 package/headers 使用 Core；没有 Runtime/port 私有 include 反向进入 Core。

### B1：物理拆分与版本策略（Core/Runtime 边界已关闭）

| 工程 | 初始独立线 | 更新规则 | 依赖规则 |
| --- | --- | --- | --- |
| `jellyframe-render-core` | `0.6.1` / Core ABI `1` | feature/性能/兼容性可独立发布 | Runtime lock 精确 pin Core version、ABI、source identity；release metadata 记录已签名 archive SHA-256 |
| `jellyframe` | `0.6.0` | App 格式、Runtime、JerryScript、桌面工具缓慢发布 | 只在明确的 dependency bump 中升级 Core |
| `jellyframe-device-os` | `0.1.0-dev` | 板卡与产品镜像快速迭代 | pin JellyFrame release 与 board feature profile |
| JFDP | `JFDP/1` | 协议独立版本 | 破坏性 wire change 只能升 major |

Core release 提供源、头文件、CMake package、feature registry/profile schema 和 provenance manifest；它不是固定的一份“全功能固件”。每个 Device OS image 在构建期选定 profile，裁掉未选 feature，并让 manifest negotiation 拒绝需要未编译能力的 App。普通 `.jfapp` 永远不能携带 native feature module。

物理 Core 仓库迁移已经完成。首个独立 Core release 已带签名、可重建，并已由锁定 Runtime 构建消费；Core/Runtime 的 B1 出口已关闭。未来 Device OS 在物理迁出前也必须消费同一 provenance 契约；此后才将 Device OS/ports/launcher 作为一个产品边界整体迁出。

## 轨道 C：Render Core 能力演进

能力扩展不再与 Device OS 阶段竞争，也不再因未拆仓无限搁置。执行顺序是：先完成 B0 的导出、锁定和发布规范，再以独立 Core release 实施能力包；物理 Git 迁移应在第一个高价值能力包之前或与其同一发布窗口完成，不能继续让 feature 大量堆积在临时 monorepo 边界。

### C1：0.6 收束能力包的证据（desktop 已关闭）

以下低成本高收益子集已经实现并完成 desktop evidence 收束：2026-08-15 的全新 Core-only build 通过
完整 Core regression、三份 `jelly_controls` capture（`300x300`、`320x240`、`172x320`）和
logical/`hsl()`、flex-order microbenchmark。这是 desktop evidence，不是硬件性能结论：

1. 逻辑尺寸/间距在 LTR horizontal writing mode 的物理映射子集。
2. 文本布局小包：长词换行、截断/ellipsis 一致性、已实现 letter-spacing 的跨 backend 验证。
3. 常用 flex/grid placement：`order`、`align-self`、`place-*` 与受限 `grid-template-rows`/area。
4. `hsl()` 与常用背景 size/position/repeat。

后续新增 C1 candidate 的证据门槛是正/负行为测试、三 target desktop capture、支持矩阵、诊断、样例 recipe
与热路径 benchmark。

### C1.1：独立 Core 候选（受 release 门槛约束）

独立治理的 Core line 还包含面向短自然换行文本的有界 `text-wrap: balance`。其 standalone
build、unit、install 与 deterministic archive CI 均已通过；但正常 Runtime build 仍使用同步
in-tree provider，因此它尚不能视为面向作者的 Runtime 已交付能力。进入 Runtime capability
matrix 前，必须完成上述 candidate evidence，作出明确的 Runtime dependency/default-provider
决策，按需更新 lock 并运行 package-consumer regression。

### C2：后续候选，不是承诺

容器查询、`oklch()`、复杂 grid、`:has()`、filter/backdrop-filter、Shadow DOM、iframe、Worker、完整 SVG/video/复杂 shaping 不属于当前计划。它们只有在受控试用的可复现需求和硬件预算共同支持时进入 RFC。

## 版本出口

- `0.6`：完成 A0 的 contracts 收束、已关闭的 Core/Runtime B1 边界、C1 中已证实的能力包；不以 Canvas 或全屏 30 FPS 作为发布条件。
- `0.7`：首个官方 developer image 的 A1/A2 交付，以及 Core 独立仓库的 B1 首次发布。
- `1.0`：至少一个稳定官方板卡、可复现 Device OS 生命周期、冻结的 Japp/manifest/diagnostic/feature-profile 契约、支持 port 矩阵与已发布的依赖/安全更新政策。完整浏览器兼容不是 1.0 条件。

## 全局变更门槛

每项变更必须通过 `git diff --check`、相关 Debug/Release CTest；脚本、工具、package 或 profile 改动另有针对性回归。性能声明必须包含 workload、target/profile、p50/p95、内存水位和失败计数。实机结论只能来自对应 port 的版本化报告。任何阶段出口被证伪时，先回到该阶段修复，不让后续功能掩盖问题。
