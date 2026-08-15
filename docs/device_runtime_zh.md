# JellyFrame Device Runtime

> 最后更新：2026-08-15；适用版本：0.6.0-dev；当前开发线：0.6.0

## 目的

JellyFrame Device Runtime 是让 App 作者无需构建固件即可使用官方开发板的产品层。它不是新的
renderer，不是通用操作系统，也不是应被塞进 Render Core 的开发板抽象或应用商店。

第一版用户流程只保留四步：

1. 选择受官方支持的开发板，安装官方 developer image。
2. 通过官方开发者通道连接开发板。
3. 在 CLI 或 VS Code 中构建、安装、更新、启动、停止或删除 `.jfapp`。
4. 查看仅属于该 App 的日志、生命周期状态和设备能力。

这条路径中，App 作者不应需要 ESP-IDF、分区表、板卡引脚图或重新构建固件。

## 边界

| 层 | 负责 | 不负责 |
| --- | --- | --- |
| Render Core | DOM、layout、paint、输入语义和能力 profile | 板卡驱动、存储、传输、安装策略 |
| App Runtime | 生命周期、恢复和有界 host service；D0 暂时承载平台无关安装事务契约 | serial/USB/Wi-Fi 驱动、flash API、签名权威和最终 Device OS 策略 |
| Device Runtime | 启动器策略、已安装 App registry、回滚策略、开发者会话和设备诊断 | 渲染实现或 SoC 专属 fast path |
| Port | 板卡镜像、启动、显示/触控、持久存储 adapter、开发者传输和固件升级 | 没有平台无关需求的新 core API |
| CLI 与 VS Code | 打包、预检、连接体验、部署进度、日志和交互调试 | 直接 GPIO、flash 或任意 shell 访问 |

Device Runtime 只消费 Render Core 能力，不把它们变成强制项。未启用 scripting、Canvas 或 media
adapter 的端口必须在传输 App 前告知工具。

## 现有基础

仓库已有多块可复用基础，但尚未组成一个设备产品：

| 已有能力 | 当前状态 | 用于设备仍缺少 |
| --- | --- | --- |
| `.jfapp` 打包、预检、target budget、资源完整性 | 已可用 | 设备端传输和存储 adapter |
| registry 安装/更新/回滚语义 | 桌面参考实现 | 嵌入式持久 registry 与 staging store |
| App 启动、teardown、崩溃恢复、返回 launcher | 平台无关契约及已测 port | 把设备 registry 接入已安装 bundle loader |
| sample launcher 与桌面 app manager | 参考 UI | 官方 system launcher image 和受保护 fallback |
| VS Code 作者工作台 | 桌面 package/check/preview/debug | 设备选择、部署、日志和实时设备调试命令 |
| ESP32-S3 显示 port | bring-up 与验收配置 | 稳定 developer image、存储分区和控制传输 |
| ESP32-P4 port | 加速器 bring-up | 有显示能力的官方板卡 profile 与 developer image |

上述缺口关闭前，能力矩阵必须把 App 分发描述为桌面/system-shell contract，不能宣称已支持实机部署。

## 设备控制契约

第一版设备协议命名为 `JFDP/1`（JellyFrame Device Protocol），仅服务本地开发。消息语义不绑定
传输方式；port 可选 USB CDC、USB Serial/JTAG、UART、Wi-Fi 或 host bridge，但不得暴露原始 flash、
任意 native 执行或任意文件系统访问。

协议分为五组有界操作：

| 分组 | 操作 |
| --- | --- |
| 发现 | `hello`、runtime 版本、board/profile id、屏幕形状、已启用能力族、存储预算 |
| App 库 | 列出已安装 App、读取状态、启动、停止、删除、回滚、启用、停用 |
| 传输 | 开始、有序分块、提交、取消、进度和可重试的结果码 |
| 调试 | App scoped 日志订阅；profile 允许时的 frame/capture 请求 |
| 恢复 | 当前 App 状态、最近失败原因、launcher/fallback 状态和受控重启请求 |

每个请求携带 session 和 request id。传输使用单一 transaction id；chunk 有明确 offset 和完整性信息。
提交前必须完整校验 bundle，再原子发布新 registry entry。失败或断连只丢弃 staging bytes，最后一个
已提交版本仍可启动。更新前台 App 时，要么继续运行旧 bundle 直到宿主切换，要么返回 launcher；绝不让
半成品 bundle 可见。

该协议不定义远程下载、账号登录、市场支付或签名权威，它们仍属于产品 host。

`JFDP/1` 中 `DeviceFrameHeader.flags` 的 bit `0` 是
`kDeviceFrameFlagResponse`。response 保留其 request 的 message type、session id 和
request id。其余 flag bit 预留：接收方保留帧内数值，但在后续协议版本定义前不得赋予含义。
D0 的 C++ 回归包含内存内 discovery request/capability response 回环，它只证明这条 framing
契约；不代表已存在物理 transport，也不表示桌面 registry reference endpoint 已经是 JFDP 设备。

### 当前可用的参考实现（D0 过渡）

平台无关的 `src/device_runtime_contracts/device_runtime_protocol.*` 已提供 `JFDP/1` framing：固定 24 字节头、
小端整数、严格 payload 上限 4096 字节、消息类型校验和 CRC32。解码得到的 payload 只是输入缓冲区的
只读视图；跨任务或异步队列前必须复制，协议层不会转移指针所有权。

同一模块还提供固定边界的 `DeviceCapabilitySnapshot` 编解码和稳定的请求结果码，包含 board/profile
标识、runtime 版本、屏幕尺寸、启用的能力位、最大 App 包大小和可用存储。字符串有明确长度上限，
不依赖 JSON、堆分配或端口私有结构。

`src/device_runtime_contracts/device_install_transaction.*` 提供有界、有序、可取消的 staging 状态机。它只依赖
`DeviceInstallStore` 注入的存储适配器，因此不会把 flash、文件系统、签名或 registry 实现带入
Render Core。写入失败、校验失败、提交失败和主动取消都会清理 staging；只有原子提交成功后新版本
才可见。

这两个 `device_*` 模块不属于 App Runtime 的所有权模型。D0 已将它们移至
`src/device_runtime_contracts`，并编译为独立的 `jellyframe_device_runtime_contracts` target，测试不链接
App Runtime 或 Render Core。直到 typed JFDP request/response payload dispatcher 和未来 Device OS package
迁移完成前，这仍是 monorepo 过渡状态。port 必须消费现有 framing、result code 与 staging 契约，不能复制它们。
这不表示 USB、串口、Wi-Fi 或任何物理 JFDP wire transport 已经实现。

桌面端可以显式运行 reference endpoint，以验证工具链而不误认为连接了开发板：

```text
python tools/jellyframe_cli.py device --transport reference --store build/device-reference discover --json
python tools/jellyframe_cli.py device --transport reference --store build/device-reference install --bundle app.jfapp --chunk-bytes 1024
python tools/jellyframe_cli.py device --transport reference --store build/device-reference launch --id org.example.app
python tools/jellyframe_cli.py device --transport reference --store build/device-reference logs --id org.example.app --json
python tools/jellyframe_cli.py device --transport reference --store build/device-reference rollback --id org.example.app
```

reference host 会将 chunk staging 与 registry 分离持久化，提供 `resume`、`commit`、`cancel`、`launch`、
`stop`、`remove`、`logs` 和 `recovery`，并且只在 commit 后发布 bundle。`--pause-after-chunks` 只是验证
resume/cancel 的 reference-only test hook，不是设备传输选项。endpoint 的 `deviceAvailable` 固定为 false：其
lifecycle log 和 recovery record 仅是桌面 reference 证据，不能被当作 panel、触控、wire transport 或设备帧率
telemetry。没有 `--transport reference` 时 CLI 会明确报错；真实 USB、串口或 Wi-Fi transport 由对应 port 单独注册。

## 官方板卡 Profile

“官方 profile”不只是 port 能编译；它必须发布稳定的 board id、显示/触控配置、能力 profile、存储限制、
开发者传输、出厂 launcher 和恢复行为。支持顺序为：

1. **ESP32-S3 Waveshare Touch LCD 1.47**：首个 developer image 与参考可穿戴 profile
   （`rect-172x320`）。
2. **ESP32-S3 Waveshare Touch LCD 1.69**：同一套生命周期与传输契约完成验收后的第二个官方镜像
   （`rect-240x280`）。
3. **ESP32-P4**：仅在带屏板卡 profile 完成验证后纳入；当前 P4 加速器 bring-up 不是 App 作者开发板。

每个 image 都要预留受保护的 system partition/App set，其中包含 launcher 与 fallback screen。第三方
bundle 存储在固件镜像之外，不能替换 launcher、recovery UI 或 port code。

## 交付阶段

### D0：契约与参考 Host（契约基线；等待物理迁移）

- 定义 `JFDP/1` framing、request/result code 和 capability handshake。
- 增加有注入式 storage callback 的平台无关 staged-install controller，并为 offset、重放、取消、断连和
  atomic commit failure 编写 focused test。
- 已有平台无关 framing、capability payload、请求结果码和 staged-install controller。桌面 reference host
  已覆盖持久 discovery/list/chunked-install/commit/cancel/lifecycle/log/recovery control semantics；内存内
  discovery 回环已验证 request/response 关联和 capability payload。typed JFDP request/response payload dispatch
  与独立 owner 迁移仍需完成后，才能进入 port 接入。

### D1：首个官方 Developer Image

- 为 ESP32-S3 1.47 增加 storage partition、不可替换 launcher/fallback 和 USB 开发者传输 adapter。
- 只实现 D0 首条工作流所需的控制操作。
- 发布一份板卡/profile manifest 与可恢复的 factory flash 工具。

### D2：作者工具

- CLI 支持设备发现和显式设备选择。
- VS Code 提供设备视图及连接、安装/更新、启动、停止、删除、日志和 runtime capability 操作。
- 桌面预览与设备调试保持区分。设备帧时间必须来自设备 telemetry，不能从 Win32 推断。

### D3：外部试用

- 用没有 ESP-IDF 环境的用户验证。
- 安装、更新、回滚、坏 App 恢复和断线重连必须重复成功，无需重新烧录。
- D1 生命周期路径稳定后再增加第二款官方板卡。

### D4：物理仓库拆分

- 只有 package consumer 矩阵和 provenance 记录跨过一个发布周期仍保持绿色后，才迁出 Render Core。
- 将 D0 的 `device_*` 契约迁移到 Device OS 或 `device_runtime_contracts`；在新 owner 保留 JFDP/1 兼容性测试。
- launcher、registry、官方镜像和 port 应作为 Device OS 产品边界一起迁移，不要零散塞入 Render Core 或 Runtime 仓库。

## 验收

首个官方 image 只有在一台干净机器配合 VS Code 扩展完成以下流程后，才能面向外部 App 作者：

1. 识别连接的受支持开发板，显示准确的 profile/capability。
2. 安装经过 `check` 的 `.jfapp`，观察有界传输进度并启动它。
3. 更新 App、默认拒绝降级、回滚、删除，并仅经明确操作保留或删除私有数据。
4. 收到 App scoped 日志和生命周期错误；断开/重连不会损坏已安装库。
5. 从 malformed、超预算或崩溃 App 回到 launcher，不发生 watchdog reset，也不需要重新烧录固件。

这条产品路径是外部开发者试用的前置门槛。更多 HTML/CSS 能力依然有价值，但无法抵消“每个 App 作者都得
成为 board-port maintainer”的使用门槛。
