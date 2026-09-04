# 变更记录

> 最后更新：2026-09-04；适用版本：0.6.0-dev

JellyFrame Engine 的重要变更记录在这里。

项目使用轻量语义化版本规则。详见 `docs/versioning_zh.md`。

## 0.6.0-dev - 开发中

### 计划

- 启动外部开发者试用线：完善构建 profile/模块证据、打包与启动器工作流，并针对能力缺口做有边界的补全。任何新的浏览器兼容承诺都必须同时落入能力矩阵、profile gate 和回归测试。

### 变更

- Runtime 现在锁定带签名的 Render Core `0.6.2` release（ABI `1`、source identity
  `539a8945...8462e3f0`），并在 package-consumer CI 中校验其确定性 archive。
  这次 lock 更新与尚待完成的 Device OS image provenance 更新保持独立。

- ESP32-S3 第一方 app manifest、模板与 App Packaging 指南示例现在统一跟随锁定的
  Render Core `0.6.2` 开发线；新增回归会依据当前 Runtime/Core 版本检查这些作者侧输入。

- CSS custom property 展开现在具有有界的解析值预算
  (`StyleResolverOptions::max_resolved_value_bytes`，默认 16 KiB)，重复的 `var()` 引用不会再生成无界的中间字符串。
  超预算值保持原有的安全 fallback 行为。

- Inline style 解析现在具有独立的源字节数和声明数量预算
  (`StyleResolverOptions::max_inline_style_bytes` 与 `max_inline_declarations`)。源过长时只解析边界前的完整声明，
  并输出可定位的诊断。

- Render Core Flex 布局现在只对交叉轴为 `auto` 的 item 应用 `stretch`，保留显式交叉轴尺寸，正确计入
  margin、padding 和 border，并在每个换行后的独立 flex line 内执行对齐。响应式矩阵和交叉轴回归已在
  完整、精简及关闭 Flex 的 profile 中运行。

- `flex-wrap: wrap-reverse` 现在会被正常 style diagnostic 拒绝，不再静默近似为 `wrap`。

- Script-task 的 sealed frame 与 service-payload lease 现使用 64-bit 不透明 ID：16-bit slot index
  配合 48-bit reuse generation，避免旧 lease 在原有 16-bit generation 循环后与新发布的值别名。
  service completion 值包升级为 version 3 并携带 64-bit payload lease ID；port 必须按此契约重新编译。

- Script-task service bridge 现在会在调用 provider copy/release callback 前验证返回 handle 的
  app/token 所有权。畸形 queue entry 会变成终态 `HandleRejected` 值，不会触碰其他 consumer 的资源。

- Script-task worker inbox 的接收现命名为 `take_worker_packet(...)`，明确它同时接收 input 与
  service completion。它一律要求显式传入 `ScriptAppSession`，已移除读取 current session 的便利
  接收路径，防止延迟退出的 worker 消费新生命周期的 input。`ScriptTaskSupervisorOptions::worker_inbox`
  与 teardown 的 `discarded_worker_inbox_packets` 也替换了错误暗示该共享通道只承载 raw input 的旧名称。

- App Runtime 现在会拒绝不属于 completion 所属 app instance 的非零 `result_handle`，包括
  unsolicited event 携带的句柄；显式绑定非零 client token 的 handle 还必须匹配该 consumer。
  拒绝错误 request-bound completion 不会消耗 in-flight request，正确的 provider result 仍可后续
  交付。已在 teardown 中释放 handle 的 stale completion 仅为回收旧 request 而允许进入队列；
  Audio close 不再返回已经释放的 handle。worker pump 也会将被拒绝的 completion 与
  `completion_queue_full` 分开报告。

- App manifest 现在通过 `runtime.minJellyFrame` 与 `runtime.minRenderCore`
  显式声明 Runtime/Core 配对。两者必须精确匹配活跃的 1.0 前 Runtime line 及其锁定的
  Render Core package；schema、packer、`.jfapp` registry 和原生桌面 source-package
  parser 都会拒绝不匹配项。这取代了旧的、未校验的原生 source-manifest metadata 路径。

- 1.0 前的包处理现在只接受一个当前开发线，不再维持历史兼容基线。第一方 manifest 统一声明
  `0.6.0`；packer 与 registry 会拒绝其他 `minJellyFrame` 值，包括手工构造的包。
  这是 1.0 前有意的破坏性调整，用于避免旧实验产物绕过当前包/运行时契约。

- 将 Runtime 侧的 Render Core provider 选择、package lock 校验、provenance 生成与
  source override 处理收敛到专用 CMake 模块。standalone Render Core 构建模块仍由 Core
  源码分发包所有，从而明确区分消费者策略与导出的引擎边界。

- 新增默认关闭、仅供桌面正确性验证的 value-frame v2 retained-diff replay probe。它只在成功
  present 后保留有界的 previous/candidate RGBA image，清空 old/new visual bounds 的保守 union，
  按 paint order 重绘所有与 region 相交的 current command，并要求与 canonical full-frame RGBA
  完全一致。它未接入 Runtime 或 port，不产生性能、dirty rendering 或 framebuffer reuse 结论；配套
  双语 RFC 已在任何硬件 A/B 前冻结 eligibility、所有权与 fallback 契约。

- 新增只读的相邻 value-frame diff 报告，用于 retained-rendering 测量。它输出 frame 结构兼容性、
  command churn、稳定前缀/后缀和候选 changed bounds，但绝不改变 repaint 或授予 framebuffer reuse
  权限。首个 WS147 transform fixture 测得 `0%` structure-equal pair、候选 changed area 为 `70.16%`，
  因此 retained replay 保持未实现，等待固定几何、局部 mutation workload 的新证据。

- rounded value-frame 合成现在会在每行可保守证明所有圆角 clip 均为 full coverage 的 span 中跳过
  clip-chain 查询；所有 corner bounding box 仍走原有精确抗锯齿 coverage 路径。WS147 实机 A/B 显示
  sampled-coverage composite 降低 31.14%、render p95 降低 3.64%，internal RAM low-water 不变、
  传输/运行时错误为零且目检通过。实验性 full-frame fixture 仍远未达到 realtime，不能视为默认路径结论。

- 增加 Render Core standalone 构建边界：App Runtime 和 JerryScript 可以独立关闭；开启
  `JELLYFRAME_INSTALL_RENDER_CORE=ON` 后会导出版本化的
  `JellyFrame::jellyframe_render_core` CMake 包、公共头文件和当前能力 profile。

- 重新提供 VS Code 内嵌调试，但采用隔离的 desktop-shell 帧流会话：每个 viewport 快照都带严格递增的
  sequence，先完整写入会话专属文件再通知编辑器；pointer、wheel 与按键只以值消息回传该会话。标签页的
  Stop 与关闭都会请求正常退出，宽限期后也只终止本会话进程树；外部窗口调试保持独立路径。
- 新增面向 App 作者的桌面工具布局：`tools/debug` 提供统一的启动、frame script
  回放和截图入口，`project_tools` 负责项目维护审计，VS Code 扩展新增 app/构建自动发现、
  diagnostics 状态视图以及截图/frame script 命令。交互式原生壳现在构建为
  `jellyframe_desktop_shell`；在项目仍处于 1.0 之前阶段，旧的
  `jellyframe_win32_browser` 入口已明确移除。
- 开启 `0.6.0-dev` 线并新增声明式 Render Core feature catalog。CMake profile 与桌面 feature 检查现在从同一份 registry 派生 ID 和依赖闭包；必需的 `core.document` 与 `core.paint` 源码族保持构建兼容，运行时路径不变。
- P3 移植接入指南已记录 WS147 完成证据和剩余责任边界。

## 0.5.0 - 2026-08-09

### 变更

- H4 最新 WS147 功耗归档已完成四组独立外部测量：active、screen-off idle、light sleep、deep sleep
  均超过 60 秒，USB serial/JTAG 断开，仪器全程自动量程，并保留平均/最小/最大/电压/时长/样本数。
  仪器不导出 p95，因此该字段保持未记录，不从均值或极值推导。

- 新增可选 script-task runtime 的平台无关跨任务值协议基础：session generation/epoch、固定槽
  mailbox、sealed AppFrame/service-payload lease、独立 service request 通道、取消 tombstone、两阶段
  teardown 与 worker-local input/completion dispatch。该模块只有在 scripting 与 script-task runtime
  两个构建开关均启用时编译；未启用 profile 不链接 VM、RTOS 或渲染状态。

- script-service completion wire packet 升至 V2：worker 只接收 session-scoped `payload_lease_id`，
  不再接触 host handle。supervisor 用有界 writer 复制服务结果，并通过 provider-release callback
  恰好一次回收源记录；取消、背压、陈旧 completion 和 teardown 均有回归覆盖。真实 JerryScript App
  service gateway 与 port 验收仍属于后续工作。

- Win32 验收程序现在无参数启动时只输出简要用法并正常退出；未知选项和过多位置参数会给出简洁
  错误并提示使用 `--help`，不再误当作 HTML 输入路径。

- 新增无脚本 `jelly_flex_grid_probe` 桌面 fixture 与 capture 回归。Flex/Grid ON
  现在检查真实像素布局；同一 fixture 也已用 OFF profile 捕获，确认稳定的 block/inline
  fallback 且无重叠。

- 已在 `f472cbc` 上完成 WS147 `forms.advanced` ON/OFF 验收：两组基础控件均可操作，
  ON 执行 submit/reset 默认动作，OFF 安全抑制默认动作；select 瞬态弹层使用旧/新 layer
  bounds 做局部重绘，没有用全屏重绘掩盖问题。本验收不覆盖 Canvas、JerryScript FormData
  以及视觉签收。

- CTest 现会将生成的 Render Core 测试可执行文件 link map 与当前 profile 对照；CMake
  target metadata 会将生成的 map 路径提供给校验器。

- Windows JerryScript CI 现会编译并运行 `forms.advanced` OFF 的 bridge 测试 target，
  确保省略的表单 API 分支可构建且不会暴露给脚本。

- 新增构建期 `forms.advanced` Render Core family 门控。关闭 profile 仍保留基础表单控件，
  但 C++ API 走安全 no-op stub、不执行 submit/reset 默认动作、JerryScript 不暴露高级表单 API，
  并在生成 profile 与 link-map 证据中记录该选择。

- link-map 报告现在明确将 `css.flex-grid` 标记为不适用 object-marker 验证：该 family
  在共享 translation unit 内编译门控，仍必须以生成 profile 和 ON/OFF 行为测试作为证据。

- Render Core profile ID 现按固定 disabled-family 顺序生成，不再维护易遗漏的组合表。
  新增 configure-only 回归，覆盖 Canvas 2D、modern-paint 与 flex-grid 的全部组合，以及
  生成的 feature 列表和编译 notes。

- 新增构建期 `css.flex-grid` family 门控。parser、style、layout 和 layer 共用同一条
  ON/OFF 边界；关闭 profile 时拒绝 flex/grid 声明及命中的 `@supports` 条件，并回退到
  block/inline。minimal profile 可关闭示例与 Win32 回归注册，Canvas/modern-paint/flex-grid
  的全部组合现在都有确定且不重复的 profile ID。

- ESP32-S3 packed RGB565 presentation sink 现在是明确 opt-in 的 port 对比路径。WS147
  整屏渐变 A/B 中 packed conversion 使 frame p95 从 58 ms 升到 71 ms，因此默认保持
  线性 framebuffer sink。

- linker-map 校验现在支持显式 `--used-feature` workload 范围：被 linker GC 移除但未被
  workload 调用的已启用 family 标记为 `not-tested`；真实 Canvas smoke workload 仍必须
  检查 Canvas symbol。

- Render Core 的 feature ID 与依赖规则现在由桌面工具共用唯一注册表；package 预检和
  linker-map 校验会在 App 或固件证据引用 profile 之前拒绝不完整的依赖闭包。

- Render Core 现为 Canvas 2D 和 modern-paint 构建切片输出 feature family 源码元数据与桌面链接 map。
  新的 link-map checker 会验证生成的 profile 与最终可执行文件一致，并区分真实实现和 disabled stub。
  render-core microbench 现报告可穿戴尺寸 modern-paint 的 p50/p95 延迟、display command 数、surface 字节数
  和空白页基线；这些仍是桌面证据，不是 MCU 性能结论。

- 一方 Markdown 现有 CTest 新鲜度关卡：每份文档必须带当前版本的编辑标记；自动生成的支持表必须带当前版本的审计快照标记。
- Windows Clang sanitizer 测试二进制现在会在旁边部署所需的 ASan runtime DLL；平台无关的 ASan/UBSan 验证可用 `RelWithDebInfo` 配置复现。
- 明确 0.5 收束状态：平台无关的核心/工具/文档工作进入收束候选，正式切 `0.5.0`
  仍需要 WS147 实机签收。新增视觉、字体/图片、滚动、恢复和输入验收的硬件签收清单。
- 新增默认关闭的 `JELLYFRAME_ENABLE_SANITIZERS` CMake 开关，用于 Clang/GCC 的 AddressSanitizer 与 UndefinedBehaviorSanitizer 构建。CI 现会在 Linux 上以这两种 sanitizer 运行非 scripting 的核心/工具测试；该项不宣称已覆盖 scripting bridge 的 sanitizer。
- 将当前开发线明确为 `0.5.0-dev` 收尾：`0.5` 的关闭条件是设备可用性、诊断、宿主契约和真实
  port 证据；完成后才进入以外部开发者试用为目标的 `0.6.0-dev`。该阶段不承诺完整浏览器兼容。
- 统一 app 作者能力表与能力矩阵中 package-local CSS 背景图片的契约：单张图片可使用
  `cover`/`contain`/`100% 100%`、简单 position 和 `no-repeat`，但不支持多层、平铺和任意尺寸表达式。
- 为官方 service-status 与 canvas-gauges target preset 声明了 `hostServices.audioPlayback` 和
  `hostServices.canvas2d`。严格四包 external-trial doctor 现为 4/4 通过，且没有屏蔽真正的
  unsupported-target diagnostic。

### 修复

- script-task bridge 在已取消的 in-flight completion 返回时直接释放宿主源资源，不再进行无意义的
  payload 复制或 lease 分配。

- modern-paint OFF 构建不再编译未使用的渐变/阴影辅助函数；核心回归测试按 profile 检查纯色/无 command fallback，
  不再错误断言被关闭的能力必须存在。

- script-service teardown 现在将内部 runtime client token 贯穿 request、completion 和返回 handle。清理
  script service 只释放该 runtime 的 network/location 资源，迟到 worker completion 仍会回收，不会释放同一 app
  内其他 consumer 的 handle。
- Grid placement 最多保留 128 条 tracked row。更大的隐式 row/span 会以
  `grid-placement-budget` 退化为不重叠的 block flow，不再压到最后一行重叠，也不会越过有界存储索引。
- document CSS 与 classic-script 收集现在执行宿主配置的聚合字节和数量预算。后续资源会通过稳定
  diagnostic 完整跳过，不再无界累积，也不会被截断后交给 parser 或 script runtime。
- HTML DOM 节点上限现在包含合成的 `html`/`head`/`body` 结构；HTML text/CDATA 与 CSS 输入、selector、at-rule prelude、declaration value 也会在临时 parser 存储无限增长前受限。超限字段会被消费或跳过，并输出稳定 diagnostic。
- worker 已持有的 service request 会在 app teardown 后继续计入预算，直至 stale completion 成功投递；预算 telemetry 现将 queued 和 in-flight 一并统计，避免迟到 worker 结果静默释放并发容量。
- worker completion 遇到已满的 UI ring 时会保留在固定的 in-flight slot，直至成功投递。Host handle 现采用受同步保护的 copy-out 查询；system event producer/UI-pump 也围绕原子 app-instance snapshot 同步。
- 桌面 `.jfapp` loader 会在乘法或 `reserve()` 前拒绝过大的 resource-index count；manifest 字段只从所属 JSON object 的直接成员或显式请求的已文档化嵌套 object 读取；`jellyframe_cli.py trial --clean` 现在只能删除仓库 `build/` 下的子目录。
- registry 安装现在要求 package 的规范化 manifest summary 具备完整且类型正确的字段，并验证 capability 派生布尔值与 `permissions`/`capabilities` 数组一致；畸形 summary 会在修改 registry 前被拒绝。
- 带 transform 的 layer 超过 offscreen budget 时现在会报告并跳过，不再静默绘制未变换结果。`text-overflow` 具有显式 specified 状态，嵌套元素不会意外继承父级 ellipsis。
- Host budget 为零时，JavaScript timer、listener、detached node 和 animation allocation 会按宿主策略真正禁用。相同 node/type/callback/capture 的重复 `addEventListener()` 会去重，移除也会匹配 capture。
- 修复方形 `outline`/offset 与圆角描边覆盖：focus ring 和带边框控件会保留直边，不再退化为只描圆角。
  新增方形 outline 几何和圆角描边直边覆盖的 render-core 回归。
- HTML tokenizer 现在在 DOM 构造前限制 tag 名、属性名和属性值的字节数。超限字节会继续被消费并输出稳定
  diagnostic，但不会让 token 内字符串持续扩容；既有属性数量限制不变。
- `FormData` 现在在核心 form collection 与 JavaScript `append()`/`set()` 路径应用独立 entry 与
  字节预算。默认 32 entries / 4096 bytes，超限抛出 `RangeError`，form 构造超限不会留下部分 entries。
- 定位 completion 现在在进入 JavaScript success/error callback 前脱离 runtime 请求容器并释放 snapshot
  handle。回调可安全地再次请求定位、重建 document 或清理服务，不会继续解引用旧请求记录。
- XHR completion 与 `Audio` event dispatch 现在会在进入 JavaScript 前解析独立持有的 runtime record，
  回调中新建同类 wrapper 不会使 dispatcher 持有的容器 entry 失效。
- Win32 验收壳现在通过 `app_service_policies_for_app(...)` 推导可选 script-service 绑定。未声明
  network、storage、audio 或 location capability 的 package 不会仅因桌面壳存在 debug fixture 而获得对应服务。
- 裁剪 text/image repaint 的临时 surface 现在复用 compositor 的 offscreen pixel 预算。预算拒绝时 text 会跳过，
  image 会以带裁剪的占位色和 `paint-transient-surface-budget` 降级。
- source-package loader 现在拒绝解析到 app root 外的 resource，包括 symlink，并在解析前拒绝 symlink manifest。

### 新增

- 新增 `jellyframe_cli.py trial`，这是仅限 Windows 的干净目录发布证据流程。它将严格官方试用包检查、模板创建、三个 target 的 check/package/preview、一个故意缺失 host service 的拒绝，以及安装/更新/回滚后的 Win32 启动统一记录到 `external_trial.report.json`。Windows CI 会上传 Release artifact；该能力只属于桌面工具，不增加嵌入式 runtime 成本。
- 新增有界 app 内路由片段：`location.hash`、`hashchange` 与 `onhashchange` 可在一个运行中的 app 内切换 tab/设置状态，不引入 history、URL 加载或浏览器导航；新增 `jelly_route_tabs` package 示例。
- 新增打包期静态本地 ES-module authoring V0。一个外部 `type="module"` 入口可使用有界、无环的 package-local `.js` 图；打包器生成 classic bundle，并从最终 package resources 移除原 module。`preview` 现通过 Win32 验收同一 prepared package，`--debug-dir` 也包含可运行 manifest。动态/远程加载和 runtime module loader 仍不存在。
- 新增 Form V0，本地嵌入式流程可使用 `form.checkValidity()`、
  `form.reportValidity()`、`form.requestSubmit([submitter])`、带
  `event.submitter` 的可取消 `SubmitEvent` 形状 `submit`，以及字符串 entry 的
  `FormData`。required 控件、text/textarea 长度边界、required checkbox/radio group
  与 required select value 现可在无浏览器 popup 的条件下校验。submit button 和
  `HTMLElement.click()` 复用同一条有界默认动作路径。该能力刻意不引入页面导航、自动 HTTP POST、
  文件上传、reset、完整 `ValidityState` 或常驻 form-data snapshot。
- 桌面 installed-app registry 增加第一版 app distribution V0 状态模型：entry
  带有 `status`、`enabled`、`updatedAtUtc` 和可选 `rollback` 元数据。更新
  `.jfapp` 时会保留上一版 bundle 作为回滚目标；`tools/app_registry.py rollback`
  和 Win32 `--rollback-app ID` 可在不修改 app 私有数据的情况下验收回滚。
- Registry 安装现在可输出 V0 install transaction report。`tools/app_registry.py
  install --report` 会写入独立报告；`jellyframe_cli.py install --root --report`
  会把 `installTransaction` 合并进 package/preflight report，让 app 作者在同一份
  文件里看到 action、完整性、rollback 可用性和数据策略。
- 新增 `tools/schemas/jellyframe.installed_apps.registry.schema.json`，用于桌面
  installed-app registry mock。持久 `status` 只保留 `installed`、`disabled` 和
  `failed`；`rollback-ready` 仍是由 rollback 元数据派生出的展示状态。
- 桌面 registry 与 Win32 壳新增 V0 enable/disable app-manager 命令。禁用 app 会保留
  bundle 和数据，但 launch lookup 会拒绝它，直到重新启用。
- App 安装现在应用第一版 update policy：用更低 `versionCode` 覆盖已安装 app 默认会被拒绝；
  如请求 install transaction report，会输出 `downgrade-blocked`；必须显式使用
  `--allow-downgrade` 才允许。面向用户的常规回退仍应使用 rollback 命令。
- Installed-app registry entry 现在有 V0 `failure` 记录。失败 app 会被禁用启动，launcher
  可以显示稳定 reason，显式 enable 会清除 failure 记录。
- 新增 V0 host install candidate。`jellyframe_cli.py install --candidate` 和
  `tools/app_registry.py install-candidate` 会校验宿主准备的本地 JSON，其中包含已下载
  bundle 路径、SHA-256、宿主签名校验状态和用户批准状态，再提交 `.jfapp`。
- 新增 `tools/app_registry.py state`，输出面向 launcher 的 app-manager state report，
  包含派生的 `launchable`、`rollbackReady`、summary 计数和 failure 详情。Win32 system shell
  也复用同一套派生 state helper 判断可启动状态和 rollback 标记。
- 更新宿主 optional-service 文档，明确当前安装/更新流程由宿主持有：worker 下载、宿主签名验证、
  install candidate 校验、transaction report、派生 launcher state，以及非固件操作必须具备 fallback。
- 示例 Win32 launcher UI 增加 rollback 操作，并把动作区改成适合小屏的两列网格。
- Win32 system-shell 遇到不支持的 launcher `data-action` 时会在状态行报告，不再静默吞掉。
- 生命周期文档补充受信 launcher/system-shell role：`system.launcher` 和 `system.appManager`
  是宿主解释的 capability，Win32 `data-action` 处理不会暴露给普通已安装 app。
- 授权 file-broker 文档明确 V0 UX/API 决策：通用文件访问仍是 host/system-shell broker 契约；
  普通 app 默认继续使用 app-private storage；受信文件管理器/系统组件角色由 host 授予；文件修改操作
  必须 staging 并可 rollback，不能要求重新烧写固件恢复。
- CLI 新增 `--runtime-log`，可把 Win32 frame-script capture 日志合并进同一份 package report，
  生成 `runtimeMetrics` 和测得的 performance summary 字段。
- CLI 新增 `--port-telemetry`，可把真实开发板/port 日志合并进 package report，生成
  `portTelemetry`，并把 frame time、DMA wait、flush-done time 和 internal-RAM 峰值写入
  measured performance 字段。Port telemetry 只属于桌面工具输入，不增加嵌入式运行时解析成本。
- Package report 现在包含 `performanceSummary.bottlenecks[]`，用短列表向 app 作者提示最可能的
  性能瓶颈来源；`performanceAdvice[]` 也会解释慢管线阶段、非首帧 full-frame repaint、
  scroll-strip telemetry、runtime overload、dirty area，以及 port 侧 frame/DMA/flush/RAM 压力。
- `developerAdvice[]` 覆盖更多 runtime/render diagnostics，包括 animation keyframes、
  package/script/stylesheet resource failure、图片 decode/cache、app 字体资源、paint fallback、
  system event reject 和常见 HTML/CSS parser recovery，并给出面向 app 作者的修复建议。
- 未分类的 pipeline diagnostic 进入 `developerAdvice[]` 时会保留来源 stage/source 和 detail 文本，
  因此新增诊断即使还没有专属 advice 模板，也能告诉 app 作者从哪里开始排查。
- `visual-vertical-paint-overflow` 现在会在能归因到真实 layout box 时输出可能的 node/path 以及
  上下越界 metrics，与现有横向溢出定位口径一致。
- `developerAdvice[]` 现在会把结构化的文本溢出、scroll container、横向/纵向绘制溢出诊断
  转成更具体的修复动作，直接指出可能的元素/文本和像素越界量，而不是只重复 diagnostic code。
- `scriptApiDiagnostics.missingCapabilityCount` 现在只统计缺失的 manifest capability；
  延后/子集 API warning 会进入新的 `warningCount` 总数，不再被误标成 capability 缺口。
- Package report 新增 `htmlApiDiagnostics`，用于静态提示 `iframe`、`embed`、`object`、
  Shadow DOM `slot`、image map 和浏览器 form submission 等浏览器专属 markup。
- 脚本 API 预检现在也会提示延后的浏览器通信、拖放和 worker API，例如 `WebSocket`、
  `EventSource`、`BroadcastChannel`、`DataTransfer`、Web Workers 和 `serviceWorker`。
- `jellyframe_cli.py doctor` 新增 `--sample` 和 `--exclude-sample`，便于试用者或维护者只运行
  某个样例子集，同时不改变默认全样例健康检查。
- `jelly_component_recipes` 示例新增确定性的 Win32 滚动 capture 脚本，CTest 会验证该示例的
  内部滚动路径使用 dirty repaint，且没有非首帧 full repaint。这给 port 作者提供了稳定的
  桌面基线，便于再对比真实 panel flush telemetry。
- layout text-overflow 诊断现在除紧凑 `node` 标签外，还会输出类似选择器的
  `path` 字段。CLI report 和 VS Code helper 会把该路径带入 `diagnosticSamples[]`
  与 `developerAdvice[]`，便于定位窄屏 target 上需要修复的元素。
- 内部 scroll container 诊断改为结构化输出 `node`、`path`、`boxHeight`、
  `contentHeight` 与 `overflowY`，scroll-list 建议可以指向具体被裁切的容器。
- app manifest 音频能力名从偏 codec 的 `media.audio.mp3` 改为
  `media.audio.playback`。包内音频资源现在只在未声明播放能力时 warning；
  具体 codec 仍由 host/profile 决定。
- JerryScript scripting 构建现在把标准 `Date.now()` 绑定到宿主时钟，来源是
  `set_host_time_ms(...)` 或 `TimeChanged` 系统快照。时间快照只更新时间，不额外派发
  Web 事件。
- Win32 frame script 新增 `event FRAME time-ms VALUE`，用于确定性注入宿主时间。
  watch-face 样例改为 `new Date(Date.now())`，并使用真实 epoch capture 起点，使脚本化截图可复现。
- 新增 `AppHostDataSnapshot` 与 `AppHostDataAccessPolicy`，作为固定大小 battery、weather、
  activity、location 和 sensor summary 的宿主/system 边界。过滤器默认清掉所有字段，目前还不是
  JavaScript API。
- Win32 frame script 现在可用 `battery`、`weather`、`activity`、`location` 和 `sensor` 事件
  注入 debug host-data summary；逐帧 capture 会输出过滤后的 `host_data` 行，便于
  system-shell 和未来 runtime-data 验证。
- 新增 `tools/benchmark_guard.py`，作为 render-core 和 app-runtime 微基准的宽松 CI
  smoke guard。CI 现在会检查 style/custom-property resolution、full pipeline、
  dirty-rect replay、scroll-blit planning、Canvas 2D path/gradient 路径、
  font-family measurement，以及 app-runtime queue/system-event helper，并使用故意留宽的
  灾难性退化阈值。
- 增加 `border-top` / `border-right` / `border-bottom` / `border-left` 单边 shorthand
  子集。支持 `border-right: 1px solid #ddd` 这类简单 width/color 语法；单边宽度只作用于该边，
  color 仍映射到 JellyFrame 当前单一全局 border color。
- 进入 `0.5.0-dev` 设备可用性开发线，添加固定容量 storage lifecycle report，供
  system shell 统一处理 exit、crash、uninstall、update 和 memory-pressure 下的存储策略，
  并输出稳定 diagnostics：`storage-flush-ok`、`storage-flush-failed`、
  `storage-drop-pending`、`storage-delete-data` 和 `storage-retain-data`。
- `AppBudgetSnapshot` 现在可在宿主提供时携带轻量 localStorage shadow item/byte 计数；
  Win32 壳的脚本化 capture 摘要会打印这些计数，便于调试小内存设备上的存储压力。
- 桌面 installed-app registry mock 现在用 `data/<sanitized-app-id>` 表示 app 私有数据。
  删除 app 默认删除这份数据；`--keep-data` 会保留数据，`delete-data` 可在不移除已安装
  bundle 的情况下清理数据；Win32 壳提供同等语义的 `--delete-app-data`。
- 添加固定容量 budget recovery 分类。`AppBudgetRecoveryReport` 会把耗尽的 runtime counter 映射为
  `warn` 或 `terminate-app`；Win32 system-shell 验收现在会把 request queue 耗尽恢复为
  `budget-exceeded` 并回到 launcher。
- 文档明确授权文件访问边界：普通 app 仍无通用文件系统访问能力；未来文件管理器或系统组件应通过
  宿主持有、用户授权的 file broker，并具备异步预算、rollback/fallback，且不暴露裸 filesystem handle。
- Win32 frame-script 摘要现在会输出 `layer_tree layers=N display_commands=N`，便于对比手表样例的
  retained rendering 与 full-frame fallback 采样结果。
- `embedded_framebuffer` 新增可选 `EmbeddedFrameBufferPresentStats`，用于开发板 bring-up
  统计 converted pixels、packed bytes、clipped/empty dirty rects 和 flush count。virtual board
  benchmark 会打印这些字段，方便把核心输出与 port 侧 panel bytes 和 DMA wait time 对齐。
- 新增第一版产品级图片 codec adapter 形状。`AppImageCodecAdapter` 与
  `AppImageCodecRequest` / `AppImageCodecResult` 允许产品 host 在现有 image
  request/completion/handle 路径后接入 PNG、JPEG、WebP 或厂商 decoder；
  `app_image_codec_result_within_policy` 会按 app 图片预算校验 decoded surface。
- Package report 新增 `imageDiagnostics`，作为打包期的图片 codec 与 target profile
  摘要。工具会分类包内 BMP、PNG、JPEG、WebP、GIF 和未知图片资源，读取轻量 BMP/PNG
  metadata，报告所选 target 的 `hostServices.imageDecode` / `imageCodecs` 支持状态，
  并在 package 使用 unsupported 或尚未验证的 codec 时输出 warning。
- 新增包内图片验收回归：检查 `watch_weather` 的 `imageDiagnostics`，通过 Win32 壳捕获样例，
  并读取输出 BMP 像素，确认 package-local BMP 图标确实被绘制出来。
- `jellyframe_virtual_bench` 现在除 full-frame RGB565 presentation 外，还会输出典型
  dirty-rectangle 和 scroll-strip 的 render/present/virtual-flush 估算，方便 port
  用更低成本把核心 packed bytes 与 panel/DMA flush 数据对齐。
- 扩展 `jelly_font_policy` 示例为两个 family 的 `.jffont` 验收包，覆盖中文 glyph、常用符号、
  故意缺字 warning，并新增 Win32 `--use-app-fonts` capture 回归。
- Manifest app font 现在会按 CSS `font-size` 做有界整数倍 bitmap 缩放，并继续对
  `font-weight >= 600` 使用现有合成粗体路径。
- 添加第一版授权 file broker 契约：标准 `file.read`、`file.write`、`file.manage`
  capability 名称、平台无关请求校验、稳定 diagnostics，以及 `AuthorizedFile`
  host-service job kind。
- Win32 壳新增 `--authorized-file-smoke DIR` 验收，覆盖未授权写入、路径穿越拒绝、
  staged commit/rollback 和 manage 操作 gate。
- Win32 壳新增 `--system-survival-smoke N` 验收，覆盖坏 app 多轮 budget recovery、
  stale completion 过滤和 launcher 事件投递。
- render-core 新增 `scroll_blit` 计划 helper，让 Win32、framebuffer 和 panel 实现共享同一套
  纵向滚动快速搬移矩形；未滚动 app 不增加 steady-frame 成本。
- 实现可选 Canvas 2D V0。render core 新增按需、有界 `Canvas2DRegistry`；layer tree 可通过现有
  image display command 路径绘制 `<canvas>`；JerryScript 构建暴露 `getContext("2d")`、矩形绘制和
  简单 stroked path；Win32 壳可渲染 canvas backing store。同步新增 canvas 单元测试、脚本测试、
  微基准和 `jelly_canvas_smoke` 示例。
- 登记 `graphics.canvas2d` 作为标准可选 Canvas 2D capability 名称。Package report 会输出
  Canvas target support；target profile 仍通过 `hostServices.canvas2d` 显式开启。
- 扩展可选 Canvas 2D 到 V0.1，加入 `globalAlpha`、`save`/`restore`、`arc`、
  `closePath`、`fill`、有界 path/state 预算和抗锯齿 stroke coverage。新增
  `jelly_canvas_gauges` package，并补齐 CLI、Win32、脚本、render-core 和微基准回归。
- 扩展可选 Canvas 2D 到 V0.2，加入有界 `font`、`measureText` 和 `fillText`
  子集。Canvas 文本在宿主绑定 text backend 时复用宿主文本后端，否则退回 JellyFrame
  极小 bitmap 文本路径；已覆盖 render-core/script 测试、微基准和 gauges 示例。
- 扩展可选 Canvas 2D 到 V0.3，加入有界、标准的 `createLinearGradient` /
  `CanvasGradient.addColorStop` 子集。`fillStyle` 和 `strokeStyle` 现在可以接收
  CanvasGradient 对象；纯色路径继续走原有快速填充。

### 修复

- 修正部分公开文档顶部新鲜度行误写为未来 `0.6.0-dev` 的问题；当前源码版本仍是
  `0.5.0-dev`。
- Release CTest 现在是有效的正确性 gate：CMake 会对 C++ 测试二进制显式保持
  `assert(...)` 生效，并在测试入口发现 `NDEBUG` 泄漏时直接编译失败。CI 也会额外
  运行 Debug CTest。
- 修复先前被 Release 断言关闭掩盖的 Canvas 2D 文本绘制测试、app service worker
  预算设置和 storage lifecycle 请求清理问题。
- 修复 host completion 与 system-event 环形队列在 wraparound 后 discard app 记录时
  可能覆盖尚未读取条目的压缩逻辑。

### 变更

- 低色深 embedded framebuffer 转换现在按 rectangle 和 pixel format 分派，减少 RGB565/BGR565
  这类目标上的每像素分支与 stride 重算开销。
- Canvas fill-path 扫描线交点现在复用 surface 持有的 scratch storage，避免每次 `fill()`
  都产生临时分配。
- Win32 browser 壳会把 DOM 文本使用的同一套 GDI/app-font 文本后端绑定进 Canvas，
  让桌面验收时 Canvas 标签和 DOM 标签的测量结果保持一致。
- `jelly_canvas_gauges` 现在在圆环和电量条中使用 Canvas gradient，Win32 脚本化截图会覆盖
  新增渐变路径。
- app-runtime 的 host completion 投递改为固定容量环形缓冲，host handle allocation
  增加 free-slot hint，用相同的有界 API 减少嵌入式循环中的队列搬移和重复 slot 扫描。
- app-runtime 的 system event 投递也改为同样的固定容量环形缓冲，在保持 app instance
  过滤语义的同时，避免宿主注入时间、电量、网络或低功耗状态快照时产生逐帧前端 `erase`。
- render-core 的 CSS declaration 应用逻辑拆出 sizing 与 box-model helper；公开 CSS 子集不变，
  后续按属性族继续维护和测试会更清楚。
- render-core 的 flex 与 grid layout 内部拆成更小的 row sizing、placement 和 auto-placement
  helper。支持的 layout 子集不变，但 flex justify/align/wrap 和 grid column/span placement
  有了更直接的回归测试。

## 0.4.0-dev - 2026-06-28

### 新增

- 添加可选 JerryScript 执行 watchdog。Runtime options 和 `HostBudgets` 现在可以设置有限
  script execution-check 预算；当链接的 JerryScript 使用 `JERRY_VM_HALT=ON` 构建时，
  失控的 eval、timer、rAF 和事件 callback 会被中断，并给出稳定的
  `script execution budget exceeded` 异常。
- 添加稳定 app teardown reason 和 `AppRuntimeHost::terminate_current(...)`，宿主现在可区分
  normal exit、app switch、user kill、runtime error、script watchdog、budget exceeded、
  load failure 和 system policy recovery，同时复用同一套有界 request/completion/handle/font 清理路径。
- `ScriptEvaluationResult` 现在携带稳定 status，scripting runtime 也为 callback 路径暴露 sticky
  watchdog interrupt flag。Win32 壳会据此在 package app 脚本 watchdog 中断后恢复到 system shell。
- 添加低成本 CSS `background: linear-gradient(...)` 绘制子集。两色垂直渐变现在会从
  style resolution 进入 layer display list 和软件栅格器；不支持的角度或 stop 不会覆盖
  之前的纯色 fallback。
- 扩展“可选付费”的视觉 CSS 子集：加入水平 `linear-gradient(...)`、第一条
  `text-shadow` 近似绘制，以及不参与布局的 `outline` stroke；并把后续视觉能力按
  标准子集和可选成本评估的原则写入路线图。
- 添加第一版视觉质量/抗锯齿路径：圆角 fill/stroke/linear-gradient 使用局部 coverage AA，
  composited layer scale 默认使用双线性采样，`image-rendering: auto | pixelated | crisp-edges`
  会传给 image painter，RGB565/BGR565 embedded framebuffer target 可选择 4x4 ordered dithering。
  普通不透明直角矩形仍保留快速填充路径。
- 通过 `.jffont` V1 coverage glyph 添加显式 opt-in 的 bitmap 字体抗锯齿路径。
  `jellyframe_font_pack_gen --coverage-bits 2|4` 可把 2bpp/4bpp glyph coverage 输出到
  C++ `BitmapFont` header 或 `.jffont` 资源；V0/1bpp 字体仍保留紧凑且无额外成本的路径。
  Package diagnostics 现在会报告 manifest `.jffont` 资源解析出的 coverage depth。
- 添加便宜的 `text-decoration` / `text-decoration-line` 子集，支持
  `underline`、`line-through` 和 `none`。
- 添加第一版有界 CSS `@keyframes` / `animation-*` 子集。Parser 会保存
  `from`/`to` 或 `0%`/`100%` keyframes，style resolution 每个 style 最多保留四条
  animation entry，`AnimationTimeline` 在共享 active-animation 预算内采样
  `opacity`、`transform: translate()/scale()`、`background-color` 和 `color`。
- 添加不支持 keyframe 属性、缺失 keyframe 名称和有界 keyframe 采样的 diagnostics 与测试，
  并在 render-core microbench 中新增 `keyframe_animation_sample`。
- `watch_weather` 加入一个标准 CSS keyframe pulse，让包式 app 示例能展示受支持动画，
  不需要自定义 API。
- 添加 Jelly UI 视觉系统示例：可安装 `jelly_controls` package、聚焦动效的
  `jelly_motion` fixture 和 `jelly_launcher_mock` fixture。sample launcher 也调整为同一胶体
  panel/button 风格，且不依赖当前不支持的 pseudo-element 绘制。
- 添加 `jelly_motion_lab` 可安装样例，覆盖图标展开为窗口、底部 sheet 弹出和按钮果冻反馈等
  LVGL/手表 UI 常见动效，并用 `requestAnimationFrame` 输出帧计数以验证 JS/rAF 正常运行。
- Win32 browser 壳新增隐藏逐帧 capture：`--capture-frames DIR --frame-count 30 --frame-step-ms 33`，
  并可通过 `--frame-event FRAME:kind[:x:y]` 注入 click、pointer 和系统状态事件。调试入口进一步扩展为
  `--frame-script PATH`，可从脚本统一设置帧数、视口、事件、逐帧输出目录和 contact-sheet 拼图输出。
- 主要 native 命令行工具新增 `--help` / `-h` 快速帮助输出。
- 添加 R1 responsive profile report：`jellyframe_cli.py check`/`preview`/`package`/`install`
  可显式传 `--targets` 或 `--all-targets`，按多个 target preset 跑 render-core pseudo browser，并在
  JSON report 写入 `responsiveProfiles[]`，包含 viewport、content height、layout bounds、横向/纵向
  overflow、pipeline 计数和 diagnostics 摘要。单 target 路径保持旧 report 形状。
- 添加 package 字体 family 策略 diagnostics。`fontDiagnostics` 现在包含 `fontFamilyUsage`，
  会把显式 CSS `font-family` declaration 与 manifest `.jffont` family 元数据、generic fallback 名称
  和未匹配首选自定义 family 对照。新增 `jelly_font_policy` 示例 package 和 app-runtime 字体 fallback
  微基准。runtime app-font 选择现在会使用规范化后的 `font-family` 偏好，启用包内字体时，
  匹配 manifest 的 `.jffont` 资源会在测量和绘制中保持一致。
- 添加可选数据服务的 manifest/profile policy 合成：`AppServiceManifestCapabilities`、
  `AppServiceHostProfile` 和 `app_service_policies_for_app(...)` 现在会在 runtime mock
  或 JS binding 提交任务前 gate `network.fetch` 与 `storage.kv`。
- 添加 `AppSystemEventQueue`，这是一个有界、绑定 app instance 的系统事件队列，用于宿主注入
  时间、时区、网络、电量、屏幕和低功耗状态快照。
- `AppSystemEventQueue` 新增 `try_push_current(...)` 和稳定 push 状态名，宿主/Win32 壳可把
  `empty-instance`、`queue-full` 等系统事件注入失败原因写入 diagnostics。
- JerryScript bridge 现在会把 accepted network status 变化映射到标准 `window`
  `online`/`offline` 事件子集，支持有界函数 listener、`removeEventListener` 和 `once`。
- app-runtime microbench 新增可选 network fetch、KV storage、image decode mock 和 system-event pump 覆盖。
- 添加静态表 app host service worker group pump，协作式 MCU loop 可按每个服务自己的预算泵送
  network、storage、audio worker，不需要动态分配，也避免跨服务误消费 request。
- package `serviceIntent` report 现在会在 target preset 声明 `hostServices` 时输出可选宿主服务的
  `targetSupport`；没有 profile 数据时保持 `unknown`。
- 内置 target preset 现在会声明保守的 `hostServices` 支持状态；如果 app 请求了所选 target 明确不支持的
  服务，package 会输出 `service-target-unsupported` warning。
- Manifest font `sizes` 和 `weights` 元数据现在会产生稳定 diagnostics：缺失数组报告
  `font-axis-metadata-missing`，非法数组报告 `font-axis-metadata-invalid`，规范化结果写入
  `fontDiagnostics.manifestFonts[]`。
- Win32 debug 壳现在通过与 port 相同的 host worker pump 边界驱动 mock image decode 和 debug
  network fetch，真实资源和回调仍只通过 UI completion 路径回到页面。
- 添加 B1 图片解码 V0 helper：`ImageDecodePolicy`、`ImageDecodeMock` 与
  `AppDecodedSurfaceRecord` 定义平台无关 raw surface fixture、`Surface` handle 生命周期、
  尺寸/decoded bytes/pending 预算和 release 规则。
- render core 新增轻量 image display command、`ImageHandleResolver` 和 `ImagePainter`；
  `<img src>` 在宿主提供 surface handle resolver 时可进入 display list，并由宿主 painter 绘制。
  真实资源加载和产品级 codec 仍由宿主/runtime 接入。
- 添加 `AppImageSurfaceCache`，用于把 `<img src>`/图标 URL 映射到有界异步 image decode request、
  completion、ready surface handle 和 release；Win32 browser debug 壳已接入 `/debug/icon.raw`
  和 `/debug/photo.raw` raw RGB565 fixture，可自动提交 mock decode 并在 completion 后
  paint-dirty 重绘。
- Win32 browser debug 壳可从 `.jfapp`/源码包资源加载无压缩 24/32-bit BMP 作为包内图片 V0，
  复用 image surface cache 和重绘路径。
- `AppImageSurfaceCache` 新增通用 ready surface eviction，可按 surface 数量和 decoded bytes
  预算回收未被当前 display list 引用的 LRU surface；render core 新增 `object-fit` 子集
  `fill`、`contain`、`cover`、`none`、`scale-down`，以及关键词/百分比一二值
  `object-position` 子集，Win32 painter 已按该位置绘制。
  Win32 debug 壳会把图片 decode request 拒绝和 completion 失败写入 diagnostics，保留原始
  `src`、稳定失败原因和状态码，方便定位缺资源、预算拒绝或解码失败。
  app-runtime 新增 `classify_app_image_failure(...)` / `app_image_failure_detail(...)`，
  以及 `AppImageSurfaceCache::diagnostic_detail_for_url(...)`，供桌面工具和未来
  嵌入式诊断口在 request 或 completion 失败后复用同一套图片失败分类和稳定 cache-state 字段。
  PNG/JPEG/WebP、复杂四值/长度偏移 `object-position` 和产品级 MCU codec 仍留给后续。
- 加固图片 cache 生命周期：直接调用 `AppImageSurfaceCache` completion 处理时会拒绝旧 app instance
  的 completion；eviction 可丢弃并报告 stale ready entry，避免被无效 handle 卡住；Win32 debug 壳会把
  stale image-cache drop 写入 diagnostics。
- 添加 `runtime_data_api.md` / `runtime_data_api_zh.md`，记录标准子集优先 runtime data API
  方向：先做异步 `XMLHttpRequest`，`fetch()` 等有界
  Promise/microtask 支持成熟后再考虑；只有存在非阻塞 app 私有内存 shadow 时才暴露极小
  `localStorage` 子集；系统状态尽量映射到 Web 邻近事件。
- 添加 `AppLocalStorageShadow`，作为标准 `localStorage` V0 子集的紧凑内存 helper；
  它执行 app 私有 KV policy 限制，但不会在 UI task 上做宿主 I/O。
- 添加 `AppXmlHttpRequest`，在 `NetworkFetchMock`/host completion 之上提供平台无关异步
  XHR V0 状态机，覆盖 GET、abort、readyState/status、响应文本和 JS binding 使用的标准事件顺序。
- JerryScript bridge 暴露异步 `XMLHttpRequest` GET V0 子集，并让 Win32 browser scripting 构建
  通过 debug network mock 验证 host completion 到 JS callback 的主线程分发路径。
- JerryScript bridge 在宿主绑定非阻塞 `AppLocalStorageShadow` 时暴露极小 `localStorage` V0
  子集；Win32 browser scripting 构建提供按 active app instance 清理的 debug shadow。
- 扩展宿主可选 JerryScript `Audio()` V0 子集：新增 `onended`/`onerror`、面向 `ended` 和
  `error` 的 `addEventListener`/`removeEventListener`、供宿主状态事件回到脚本的 runtime
  dispatch hook，以及 Win32 debug 壳在本地/包内播放后派发 `ended` 的调试路径。
- `jellyframe_font_pack_gen` 新增 `.jffont` V0 二进制字体补充包输出，复用现有
  `BitmapFont` glyph 数据模型，但去掉 C++ 指针和编译期符号，为后续 `.jfapp`
  动态包内字体资源打基础。
- 添加 `BitmapFontResource`，可把 `.jffont` bytes 解析成只读 `BitmapFont` view，
  复用现有 bitmap font 测量和绘制后端。
- 添加 `AppFontSet` 并接入 `AppRuntimeHost`，让 `.jffont` 字体资源随
  `app_instance_id` 加载、清理和切换；Win32 package loader 会读取 manifest
  `fonts` 声明并把 `.jffont` 加入当前 runtime 状态。
- Win32 browser 壳新增 `--use-app-fonts`，可显式让 `.jfapp` 包内 `.jffont`
  参与 layout 和 paint，用于动态字体补充包验收；默认路径仍使用 GDI 文本。
- 完成 A4 app 生命周期契约第一版：`AppRuntimeHost::crash_current()` 会复用统一
  teardown 规则；Win32 壳现在把 package loader、脚本 runtime、timer、输入和
  completion pump 绑定到 active `app_instance_id`，app 加载失败会释放实例并回到
  system shell。
- 添加 B3/B4 平台无关 mock：`NetworkFetchMock` 提供 manifest/profile 之后可复用的
  fixture/handle/completion 网络数据契约，`AppPrivateKvStorageMock` 提供按 app id
  隔离的异步 KV storage 契约和预算检查。
- package report 和 C++ package manifest reader 增加 `storage.kv` capability 摘要，用于后续
  runtime storage gate。
- `jellyframe_cli.py package/check/preview/install` 默认执行字体资源预检；新增
  `--no-font-check` 用于显式跳过。
- 添加共享 `PipelineStatistics` 统计入口，用同一口径统计 DOM、render、layout、
  layer、display-list、framebuffer、resource 和 arena 使用情况。
- 添加 arena capacity 和 waste 统计，便于嵌入式 benchmark 区分真实对象用量和块式分配余量。
- 添加低成本 `StyleResolver` 候选缓存统计，可观察缓存条目、缓存规则引用、命中、
  未命中和预算清空次数。
- 添加轻量管线 diagnostics sink。HTML parser、CSS parser、style resolver、
  render tree、layout 和 layer tree 现在会向 PC 工具报告预算截断、跳过、忽略和
  降级事件。
- 扩展 diagnostics 兜底覆盖：HTML tokenizer/tree-builder 会报告异常 tag、属性、字符引用、
  未闭合 raw text 和不匹配 end tag；script 收集会报告 module/未知类型跳过和外部脚本加载失败；
  package/resource loader、inline style parser 和 software renderer/paint fallback 也会报告触发字段。
- 添加 `jellyframe_pseudo_browser --diagnostics-json`，用于输出结构化桌面报告，
  覆盖管线统计、脚本状态、package 资源加载和各组件 diagnostics。
- 添加 GitHub Actions CI workflow：构建 Windows 验证目标，运行核心测试，检查
  Python/VS Code 工具，并执行 package 管线 diagnostics smoke。
- 添加 README 应用截图画廊，截图由 JellyFrame 伪浏览器从 starter app templates
  实际渲染生成。
- 扩展 Host/HAL capability profile，新增 async、media、network 和 app bundle
  能力描述，用于后续可选图片/音频/轻量视频、运行时网络数据请求和安装式 bundle。
- 添加可选宿主服务接口契约文档，定义通用 job/completion、图片 surface、音频句柄、
  轻量视频、fetch response 和安装式 bundle registry 的 V0 实现形状。
- 添加 `host_services` 核心辅助模块，提供有界 request/completion 队列和带 generation
  校验的 host handle table，为安装式 app、图片、音频和网络服务打地基。
- 添加 `AppLifecycleController`，用于生成 active `app_instance_id`、管理 foreground/suspended
  状态，并在 app 切换/退出时取消旧 request、丢弃旧 completion、释放旧 host handles。
- 添加 `AppRuntimeHost` 有界状态容器，将 app lifecycle、request/completion queue 和
  host handle table 组合为桌面壳/MCU host 接入可选服务的统一入口。
- 添加 `.jfapp` V0 安装式 bundle 输出：`tools/package_app.py` 和 `jellyframe_cli.py package`
  可生成小端、未压缩、固定索引的二进制资源包，并在报告中记录 bundle CRC/SHA-256 和分段大小。
- 伪浏览器和 Win32 browser 壳现在可以通过 `--app path.jfapp` 直接加载安装式 bundle，
  用于验证 bundle 与源包目录渲染结果一致。
- 添加桌面 installed-app registry mock：`jellyframe_cli.py registry install/list/path/remove`
  可校验 `.jfapp`、通过 staging 安装 bundle、原子提交 registry，并为后续系统壳 app manager 打基础。
- 添加 `samples/apps/system/sample_launcher`，并将 Win32 App Manager 启动器建模为带系统权限的
  JellyFrame App 角色；`--launcher-app` 可指定其他受信 launcher app。
- 添加 ESP32-S3 N16R8 benchmark 配置与 16MB 分区表，并记录 2026-06-19 实机基线：
  16MB Flash、8MB octal PSRAM、300x300 / 40 cards / 20 iterations 完整 pipeline 通过。

### 变更

- 公开 samples、templates 和 script/runtime 测试现在使用 `/data/weather.json`、
  `/audio/tone.wav` 这样的 package-local 标准路径；Win32-only debug fixture 仍是壳层内部细节。
  旧的 debug-only `app://...` fixture scheme 已删除；在 1.0 之前，JellyFrame 不为未纳入
  文档化 Web 子集的私有语法保留兼容 shim。
- Package report 新增稳定的 `serviceIntent` 摘要，用于记录 manifest 请求的网络、存储、音频和
  后台服务意图，但不暗示宿主已经授权。
- Host service worker 现在可以按 `HostServiceJobKind` 弹出 request，避免 network、
  storage、image 和 media worker 误消费彼此的队列任务。
- 根据 ESP32-S3 解码实验审计更新 HAL、宿主抽象、运行循环、app packaging 和路线图：
  MP3 与小尺寸 MJPEG/图片 decode 可作为可选宿主服务，H.264 不进入默认 ESP32-S3 profile；
  网络继续只作为运行时数据 API，不作为远程页面资源 loader。
- App manifest/schema 增加 `role` 字段和系统 capability 预留，声明 launcher/watchface/settings
  角色本身不授予权限，授权仍由宿主/profile 决定。
- Pseudo browser、pipeline dump、embedded host demo 和 virtual-board benchmark
  现在通过同一个 helper 输出面向内存/预算的管线统计。
- Render、layout 和 layer tree 计数改为显式工作栈，替代递归 helper 遍历。
- Software compositor 现在可从 `HostBudgets` 限制 offscreen compositing pixels；
  过大的 opacity/composited layer 会降级为逐命令透明绘制，而不是分配大块临时 framebuffer。
- 配置 framebuffer pixel 预算后，`SoftwareCompositor::render()` 会在分配前拒绝过大的主 framebuffer。
- Microbench 和 virtual-board benchmark 现在会输出样式候选缓存统计，便于用真实 app
  数据判断是否值得继续做 computed-style sharing。
- 弃用旧的文本检索式兼容性扫描。`jellyframe_font_resource_check` 现在只保留用于确定性的
  字体资源工作，例如 used-character 收集、bitmap font 预算估算和字体覆盖检查。
- 将原 `jellyframe_capability_check` 二进制重命名为 `jellyframe_font_resource_check`，
  旧名称仅作为早期工具名保留在历史记录中。
- `jellyframe_cli.py package` 和 `check` 现在默认先通过伪浏览器运行一次管线 diagnostics；
  `preview` 本身就是完整管线运行。只有请求字体选项时才运行字体资源检查。
- CLI 的 `check`、`preview` 和 `package` 会把伪浏览器 diagnostics 合并进 JSON report 的
  `pipelineDiagnostics` 字段。error 默认失败；warning 默认只提示，传入 `--strict` 后会失败。
- VS Code 辅助扩展现在会在报告面板和 inline diagnostics 中消费 `pipelineDiagnostics`，
  `preview` 也会写出 report，并新增打开所选 package 的 Win32 browser 壳命令。
- 删除 loose `watch_calculator` fixture，避免仓库发布一个刻意贴近专有手表计算器设计的 app。
- JerryScript 的 `XMLHttpRequest` 与 `Audio` constructor 现在只会在宿主绑定对应 network
  或 audio adapter 后暴露。App 可使用标准 `typeof` 能力检测，未支持目标不会暴露一个实际不可用的 API。
- 脚本事件对象现在使用轻量 event-kind 标记投影 mouse/wheel 字段，避免在嵌入式构建中依赖 RTTI。
  基础 `Event("click")` 仍是普通事件，不会被当作伪造的 mouse event。
- 拆分 `SoftwareCompositor` constructor，避免 ESP-IDF C++ 工具链遇到带默认 aggregate options
  的重载歧义，同时保留 image painter 路径。
- 新增稳定的 `FrameUpdateReason` / `FrameUpdateStatistics` 诊断，并在 Win32 frame capture
  输出中展示，便于在增加 retained rendering 结构前先定位 full-frame fallback 来源。
- `element.textContent` 现在会原地更新已有的唯一 text child，而不是替换子节点，避免计时器、计数器、
  rAF label 等常见路径制造不必要的 `DomDirtyTree` full-frame planning。
- JerryScript DOM 子集新增标准形状的 `element.className` 反射，底层仍使用已有 `class`
  attribute 和 style/layout dirty 路径。
- Win32 动画泵动现在只设置 root 聚合 paint-dirty bit 用于 timeline sampling，不再把 document
  标成 local dirty node。`jelly_motion_lab` frame capture 现在除首帧外能保持 dirty-rect repaint，
  不再退回 full-frame repaint。
- Win32 frame capture 新增 full-frame fallback 前的 attempted dirty 细节，包括尝试的 rect 数、
  最大尝试 dirty area，以及观察到的最大 dirty node。
- 新增 Win32 motion-lab dirty-frame CTest 回归，要求 90 帧动画 soak 保持首帧 full frame、
  后续 dirty-rect frame 的形态。

## 0.3.0-dev - 2026-06-18

### 新增

- 将 starter app templates 和 `samples/apps/packages/watch_weather` 更新为更现代的手表式
  UI 验收样例，并用伪浏览器截图验证 300x300 输出。
- 为 Win32 壳添加与伪浏览器对齐的 `--app` package 预览/截图路径，支持读取
  manifest viewport、package 本地 CSS/script 资源，并固定按 viewport 输出截图。
- 将平台无关 embedded host bring-up 示例源码移入 `ports/embedded_host_demo`，
  可执行文件名保持不变。
- 将样例资源统一收拢到 `samples/`，并把原生 C++ 验证工具移入
  `tools/native`，移除职责混杂的顶层 `examples` 目录。
- Render tree 构建会跳过非保留上下文中的纯格式化空白文本节点，减少缩进换行对
  block/grid/flex layout 的污染，并降低无意义 render/layout 对象数量。
- 支持 `repeat(N, minmax(0, 1fr))` 作为简化固定 grid 列模板，便于常见现代
  keypad/card UI 降级到 JellyFrame 的有界 grid 子集。
- 添加 PolyForm Noncommercial 1.0.0 许可证、商业授权联系说明，并在 README
  中明确 JellyFrame 是“非商业源码可用”软件。
- 为公开源码、示例、测试、工具、preset、schema、template 和 port 目录补充
  README，方便用户 clone 后快速审查仓库结构。
- 添加第一版 M12 `DomOwner` 原型，并为 JerryScript 脚本创建/移除后保留的
  detached DOM nodes 增加统计和预算限制。
- 添加平台无关 budget stress tests，并让伪浏览器输出脚本 runtime 的 timer、
  listener 和 detached DOM node 统计。
- 完成当前 M10 文本/字体工作流范围：字体资源检查器会给出 tiny、符号追加、
  中文 app 子集、中文标准和全球化产品字体包 profile 建议。
- 记录 ESP32-S3 增量审计结论：LVGL/vendor SDK 只应作为可选的薄
  panel/input/text hooks，不作为 JellyFrame 主渲染后端。
- 添加第一批 M7.6 HTML parser 兼容项：node/depth/attribute 上限的 parser 预算诊断、
  紧凑常用 named entity 表，以及 Windows-1252 legacy numeric-reference remap。
- 添加共享的显示期文本规范化，使 DOM 文本保留作者空白，而 layout/layer 输出仍能折叠普通显示文本。
- 添加面向第一次接触项目的上手文档（`HOW_TO_START.md` / `HOW_TO_START_zh.md`）
  和双语 `docs/README` 索引，用于区分技术契约与维护资料。
- 项目正式更名为 `JellyFrame`；`WearWeb` 现在仅作为早期代号出现在文档中。
- 添加平台无关的 `TextMeasureProvider`，让 layout 能使用宿主文本 metrics，同时继续把字体 API
  留在 `jellyframe_render_core` 之外。
- 为 display command 添加最小文本绘制语义：水平对齐，以及单行/可换行文本。
- 在已有 GDI 文本绘制之外，为 Win32 壳添加 GDI 文本测量注入，使 UTF-8/中文桌面验证更接近真实效果。
- 添加双语文本后端文档，描述测量/绘制契约和 fallback 限制。
- 为 `jellyframe_font_resource_check` 添加字体覆盖能力：可输出源码中用到的非 ASCII 字符，并用 UTF-8
  字体覆盖文件检查缺字。
- 在 `InputController` 上添加适合按键/表冠设备的焦点导航：
  `focus_next()`、`focus_previous()` 和 `activate_focused()`。
- 添加双语嵌入式 HAL API 文档，面向开发板 port，并包含 ESP32-S3 映射建议。
- 添加双语移植工作指导文档，明确 ESP32-S3/RTOS/LVGL port 的阶段任务、实现方式、
  验收标准，以及需要核心先补能力的边界。
- 添加 `ports/virtual_board` 桌面 virtual board 基准，并把 ESP32-S3/QEMU 实验包整理为
  `ports/esp32s3-idf` bring-up 工程。
- 为 ESP32-S3 添加有界静态资源包 hook，用于本地 HTML/CSS/classic-script 资源，包含生成式
  C++ table 和 P2 smoke 资源。
- 添加双语 ESP32-S3 QEMU PSRAM 梯度测试文档，记录 4M/8M/16M/32M 容量下的管线耗时和选型建议。
- 添加平台无关的静态 bitmap font backend，提供面向生成式嵌入字体包的测量和绘制 callbacks。
- 添加 `jellyframe_font_pack_gen` 桌面 BDF 子集生成器，可输出供嵌入式构建使用的 C++
  `BitmapFont` header。
- 添加 `jellyframe_embedded_host_demo` 平台无关静态资源示例，串起 HTML/CSS 解析、bitmap
  文本、焦点激活和 RGB565 framebuffer 提交，且不依赖 Win32、文件或硬件 I/O。
- 添加第一版宿主设备能力 structs，供开发板 port 描述显示、输入、内存、budgets 和可选宿主服务。
- 添加 `render_core/budget.h` helpers，把 `HostBudgets` 映射到 HTML/CSS parser、render/layout/layer/
  display-list、dirty-rectangle 和 JerryScript timer/listener 限制。
- 将 DOM attribute 存储从每节点 `std::unordered_map` 改为紧凑顺序 `AttributeList`，降低小型嵌入式 UI
  的 per-node heap 开销，同时保留现有 map-like 调用形态。
- 添加核心 `MonotonicArena` 内存工具，支持块式线性分配、反序析构和整 arena reset，为后续
  DOM/render/layout/layer 生命周期对象集中分配做准备。
- 为 render tree 添加 arena-backed 构建入口，并在 microbench、virtual board 和 ESP32-S3
  benchmark 中使用该路径验证文档生命周期分配模式。
- 为 layout tree 添加 arena-backed 构建入口，将嵌入式取向 benchmark 切到该路径，并补充核心回归测试。
- 为 layer tree 添加 arena-backed 构建入口，将嵌入式取向 benchmark 切到该路径，并补充 layer-tree
  回归测试。
- 为 `StyleResolver` 添加有界候选规则缓存，用于重复 id/class/tag 模式，同时保留逐节点选择器匹配和
  cascade 语义。
- 为 DOM 子树销毁和整子树 `textContent` 替换添加迭代路径，降低极深生成式文档的栈压力。
- 添加双语 DOM arena 可行性文档，说明为什么 mutable/scripted document 暂不直接切换 DOM arena。
- 添加迭代式 `compute_dom_statistics()` instrumentation，并在管线诊断中输出 DOM 深度和属性数量。
- 添加双语项目状态与里程碑文档，明确硬件无关主线范围、已完成能力、已合并移植支撑代码和后续核心里程碑。
- 为表单控件 value/checked/selection 变化添加 paint-only DOM dirty 状态，使 Win32 壳能对常见控件交互复用
  render/layout，并只重绘有界 dirty rectangles。
- 通过 callback 形式的 `document_style` API 添加平台无关的外链 stylesheet
  收集能力。核心代码仍不执行文件或网络 I/O；示例工具和 Win32 壳只在桌面验证时提供本地文件加载。
- 为常用 HTML5 语义/内容元素添加可用默认样式：`a`、`mark`、`blockquote`、
  `summary`、`details`、`address`、`hgroup`、`progress` 和 `meter`。
- 为 `progress` 和 `meter` 添加简单的软件绘制 value bar。
- 为 `jellyframe_win32_browser` 添加 `--capture`，可通过 Win32/GDI 文本路径渲染页面并写出
  BMP/PPM 图片，便于视觉检查。
- 添加轻量、平台无关的表单控件状态层，覆盖嵌入式应用常用的 text input、textarea、
  checkbox、radio、range 和 select。
- 添加核心 UTF-8 文本输入、简单按键处理和有状态控件激活 API。
- 添加面向 JerryScript bridge 的 DOM mutation 原语：子节点插入/删除、属性修改、`textContent`
  更新，以及 tree/attribute/text/style/layout dirty flags。
- 添加双语 JerryScript 接入规划文档，覆盖 runtime 生命周期、binding 所有权、里程碑、风险和第一个交互式
  demo 目标。
- 添加可选 `jellyframe_script` JerryScript runtime shell。该能力默认由
  `JELLYFRAME_BUILD_SCRIPTING=OFF` 关闭，保证 `jellyframe_render_core` 不依赖 JerryScript 头文件或库。
- 为 scripting 构建添加初始 `jellyframe_pseudo_browser --script`：执行一个外部 JavaScript
  文件并报告结果或异常。
- 添加 `src/script/samples/classic/runtime_probe.*`，作为第一个脚本 runtime 验收页面。
- 添加 JerryScript M3 最小 DOM binding：`window`、`document`、`getElementById`、
  `createElement`、`createTextNode`、`appendChild`、`removeChild`、`setAttribute`、
  `getAttribute` 和 `textContent`。
- 添加 `src/script/samples/classic/dom_mutation_probe.*`，用于通过伪浏览器验证脚本驱动的 DOM mutation。
- 添加 M4 JavaScript 事件 binding：`addEventListener`、`removeEventListener`、event object、
  default prevention 和 propagation control。
- 为 Win32 browser shell 添加 scripting 支持，使桌面 native input 可以派发到 JavaScript listener，
  并在 DOM mutation 后重绘。
- 添加 `src/script/samples/classic/event_probe.*`，用于交互式事件桥验收。
- 添加 M5 JavaScript 表单控件属性：`value`、`checked`、`selectedIndex` 和 `select.value`。
- 在 `samples/apps/loose` 下添加天气、时钟、计时器和计算器应用式验收示例。
- 添加中英文嵌入式应用子集文档，说明 M6 后能构建什么，以及哪些浏览器假设被刻意排除。
- 添加 M6 宿主泵动 timer：`setTimeout`、`clearTimeout`、`setInterval` 和 `clearInterval`。
- 添加 `jellyframe_pseudo_browser --pump-timers ms`，用于无交互窗口的 timer 脚本 smoke test。
- 添加中英文内存管理审视文档，覆盖当前所有权、嵌入式风险和 allocator/container 优化优先级。
- 添加单一聚合测试程序 `jellyframe_render_core_tests`，覆盖平台无关回归测试，替代普通构建中的多个独立测试
  executable。
- 添加 `JERRYSCRIPT_ROOT` CMake 支持，便于使用 `third_party/jerryscript` 这样的官方 JerryScript
  本地源码树。
- 添加面向嵌入式应用的响应式 grid card layout 子集：`display:grid`、
  `repeat(auto-fit, minmax(<length>, 1fr))`、`gap`、
  `grid-auto-rows: minmax(<length>, auto)`，以及 `grid-column`/`grid-row:
  span N`。
- 添加 `aspect-ratio` 尺寸计算，用于视觉/媒体盒子。
- 添加便宜近似 `box-shadow` 绘制：输出圆角半透明填充，不做真实 blur。
- 添加面向开发者的能力矩阵文档，覆盖 HTML/CSS/DOM/script/rendering 功能的支持、
  降级、懒处理和延后状态。
- 添加 `margin-*`、`padding-*` 和 `border-*-width` 物理单边 CSS longhands。
- 添加 M7 classic document script loading：scripting 构建会收集并执行 inline
  `<script>`，本地外部 `<script src>` 通过壳层 callback 加载。
- 添加 `document_script` helper，用于平台无关的脚本收集。
- 添加第一版宿主抽象草案和 `src/render_core/host.h`，覆盖 resource、clock、frame sink
  和 budget structs。
- 添加 `src/script/samples/classic/inline_loading_probe.*`，用于验证自动 document script loading。
- 添加 `font-weight` 解析、继承和 display-list 传递；核心 fallback 用近似加粗绘制，
  Win32/GDI 文本路径会选择原生字重。
- 添加轻量列表标记支持：`list-style`/`list-style-type`、`ul`/`ol` 原生轻量 marker，
  以及面向常见自定义有序列表的极小 `::before content: counter(...)` 路径。
- 添加简单固定 grid 列模板，例如 `grid-template-columns: 120px 1fr`，用于描述列表和设置页式结构化数据。
- 添加 `SoftwareCompositor::render_into` dirty-rectangle framebuffer 重绘，以及
  `HostFrameSink` presentation 辅助函数。
- 添加 `dirty_region`，作为第一版自动 dirty-rectangle 来源，用于直接文本、属性和表单控件
  mutation。树结构 mutation 仍保守重绘整个 viewport。
- 添加第一版 M8 frame-update planner 和双语运行循环契约文档，明确宿主 input、timer、
  dirty update、repaint 和 present 顺序。
- 添加 `FrameLoopOptions` / `FrameLoopPendingWork` planning helpers，让宿主可以限制每帧
  input event 派发和 script timer 泵动数量，同时不把队列所有权交给核心。
- 添加 `FramePipelineCacheState` / `make_frame_update_state`，让宿主可以用统一的
  cache snapshot 构造 frame-update plan，同时不把 render/layout/layer 所有权交给核心。
- 添加第二阶段 frame repaint planning，使宿主在 layout 解析出新的内容高度后再次确认
  framebuffer 是否可复用。
- 添加长时间 dirty-update smoke 覆盖，验证重复 paint-only 控件变化仍保持有界 dirty rectangles
  并正确清理 dirty flags。
- 添加长时间 frame-loop smoke 覆盖，验证 input/timer 积压能按每帧预算排空，并回到 clean cached idle。
- 添加 `compute_dirty_region(...)` 诊断接口，提供 clean、dirty-rect、full-frame mode
  和显式 fallback reason，用于 M9 invalidation 审计。
- 添加稳定的 dirty-region mode/reason 名称，并在 Win32 验证壳窗口标题中显示最近一次 dirty
  repaint mode。
- 添加 `DirtyRegionStatistics`，让测试和验证壳可以累计 dirty-rect/full-frame 次数、dirty area
  与 fallback reason 分布。
- 添加 dirty-region 重绘成本 helper，让宿主可以把估算 dirty area 与 viewport 对比，并在局部
  flush 已经不划算时选择全帧重绘。
- 添加 `display_invalidation` 诊断，可统计 dirty rectangles 覆盖了多少 layer 和 display
  command，并在 Win32 验证壳标题中显示 command 覆盖情况。
- 添加 `HostTextAdapter`，作为 LVGL/vendor 文本测量和绘制 callback 的平台无关桥接。
- 为 `jellyframe_font_resource_check` 添加字体预算汇总，并让 `jellyframe_font_pack_gen`
  输出 font pack 体积估算。
- 添加 `embedded_framebuffer`，作为平台无关 `HostFrameSink` adapter，可把 dirty rectangles
  转换到调用方持有的 RGBA8888/BGRA8888、RGB565/BGR565、RGB332、Gray8 或 1-bit
  单色显示 buffer。
- 添加 ESP32-S3 P3 显示 bring-up 支持：8 MB flash 分区布局、RGB565 packed dirty-rectangle
  flush callback、scratch buffer 逐行打包，以及覆盖全帧和局部 dirty 提交的 QEMU 显示 smoke 路径。
- 添加 ESP32-S3 P4/P5/P6 bring-up smoke 支撑：极小 bitmap 字体、有界开发板输入队列、
  焦点/文本/控件验证，以及 dirty-rectangle RGB565 提交检查。
- 添加面向嵌入式 app 的 JavaScript helpers：`children`、`parentElement`、简单 selector
  `matches`/`closest`、基于已有属性的 `dataset` 快照、可写的小型 `element.style` 对象，
  以及 boolean `hidden`/`disabled` reflection。
- 添加 mouse-like `pointerdown`/`pointerup` 和 `touchstart`/`touchend` 事件派发，用于可穿戴按下反馈。
- 添加早期 `jellyframe_capability_check` 桌面 HTML/CSS/JS 扫描器，用于报告受支持子集、
  降级特性和不支持 API。该工具后来被废弃并由管线 diagnostics 取代；剩余字体工作已重命名为
  `jellyframe_font_resource_check`。
- 添加保守的现代长度函数支持：当参数能归约为受支持长度时，解析 `min()`、`max()`、`clamp()`
  和简单 `calc(A +/- B)`。
- 添加简化 `flex-wrap` 行换行，用于常见卡片/盒子布局。
- 添加简化 flex row sizing，支持常见 app 布局中的 `flex`、`flex-grow`、
  `flex-shrink` 和 `flex-basis`。
- 添加有界 positioned layout，支持常见 app overlay 中的 `relative`、`absolute`、
  `fixed` 和简单 `top`/`right`/`bottom`/`left` offset。
- 添加有界条件 `@media` 子集：支持 `screen`/`all` 查询中的 `min-width`、`max-width`、
  `min-height` 和 `max-height`，按 parser viewport 一次性求值。
- 添加小型 CSS custom property 解析子集：支持沿 DOM 路径继承的直接
  `var(--token)` 和 `var(--token, fallback)`。
- 添加 adjacent/general sibling selector matching，支持 `+` 和 `~`。
- 添加动态 pseudo-class 样式匹配，支持 `:hover`、`:active`、`:focus`、
  `:focus-within`、`:checked` 和 `:disabled`，并在 input state 变化时触发 dirty
  invalidation。
- 添加 `:is()` 和 `:where()` selector-list matching，分别使用参数最高 specificity
  和 0 specificity。
- 添加保守的 `@supports` declaration feature query 子集，支持 `not`、同质
  `and`/`or` 链，并安全展开匹配 block。
- 添加外链 stylesheet 合并、语义 fallback 样式、inline 高亮绘制、DOM mutation invalidation
  和表单控件 fallback 行为的回归测试。启用 scripting 的构建还会加入 JerryScript runtime
  生命周期和异常路径测试。

### 改进

- 扩展 bitmap font 回归覆盖，验证缩放、宽标点、粗体近似和高码点缺字 fallback glyph。
- 将 bitmap font glyph 查找从线性扫描改为二分查找；生成的 glyph table
  必须继续按 Unicode codepoint 升序排列。
- `textarea` 和 `title` 现在走有界 RCDATA-like tokenizer 路径并解码字符引用；
  `script` 和 `style` 继续使用简化 raw text。
- 带自闭合斜杠的非 void HTML 元素现在遵循 HTML 语义并保持打开；真正 void 元素仍保持叶子节点行为。
- 将 HTML Living Standard 降级审计纳入路线图，形成 HTML parser/DOM 兼容短线：
  优先处理低成本、容易让 app 作者踩坑的差异，同时继续排除 quirks mode 和沉重历史兼容包袱。
- 改进 inline layout，使文本、链接、高亮和 inline 控件按可用宽度横向流动并换行，不再把每个 inline
  节点都垂直堆叠。
- 在简化 layout engine 中保留父级 `text-align` 对 inline text run 的影响。
- 将 inline 背景/边框绘制收缩到子文本范围，避免 `mark` 等 inline 元素填满整行。
- 将常见 replaced controls/media 节点作为叶子 render object 处理，避免 `select` options
  和不支持的媒体 fallback 文本溢出到页面布局中。
- 改进默认表单控件尺寸并支持 `border: none`，让按钮保持按内容收缩，同时让未显式设置宽度的
  input/select 更可用。
- 在 display list 中绘制轻量原生控件外观，包括 range track/thumb、checkbox/radio
  勾选标记、select 箭头以及文本控件 value/placeholder 内容。
- Win32 壳会把字符输入和 Backspace 转发到核心控件模型，并在同一份 DOM 上重绘，使桌面验证能反映实时控件变化。
- 将事件 listener 存储从 hash table 改为紧凑的按类型分组 listener 数组，降低嵌入式常见页面的 listener
  额外开销，同时保持公开事件 API 不变。
- 在布局阶段为表单控件提供 intrinsic 内容行高，使 select 和空 input 即使没有作者指定高度也保持可读。
- 仅在真实表单控件 wrapper 上安装脚本表单访问器，减少普通 DOM 节点的属性设置开销。
- 将 clock 和 timer 应用示例升级为使用 M6 `setInterval`，不再只依赖手动刷新。
- 改进简化 flex row layout，使其支持 `column-gap`。
- 改进 dirty rerender 路径：根节点 dirty 检查为 O(1)，dirty 清理跳过干净分支，
  同值 `textContent` 不触发 invalidation，Win32 壳在 clean input callback 后不再重建管线。
- 将 dirty flag 清理和 dirty-region 遍历改为显式工作栈，并按聚合 dirty 位剪枝，
  降低深层嵌入式文档的栈压力。
- 改进 dirty-region 的 layout 匹配方式：旧/新 layout tree 各扫描一次并聚合 dirty node
  bounds，避免多个脏节点时反复全树查找。
- 改进结构性 DOM 变化的 frame-update planning：`DomDirtyTree` 不再保留最终只会导致
  保守 full-frame repaint 的上一棵 layout tree。
- 改进 Win32 browser dirty repaint 路径，改用共享的第二阶段 repaint planner，
  不再在壳层重复手写 layout/framebuffer 尺寸判断。
- 改进 Win32 browser dirty repaint 路径：当估算 dirty rectangles 超过 framebuffer 面积
  70% 时，直接退回全帧重绘。
- 收紧 dirty-region 重绘成本 helper 语义：即使阈值为 100%，保守估算面积超过 viewport
  时也不会继续走局部重绘。
- 避免无变化的表单激活制造 paint dirty，例如再次点击已选中的 radio，或循环只有一个
  option 的 select。
- 改进 core 文本 fallback，使测量和绘制按 UTF-8 码点处理，而不是把每个非 ASCII 字节当成独立 glyph。
- 改进 bitmap font backend：缺字现在会绘制可见且宽度稳定的 fallback 方框，而不是只保留空白 advance。
- 改进文本换行启发式，单个不可断符号即使测量宽度略超小控件，也不会被当成多行文本。
- 改进 grid layout：auto-width grid item 会按分配到的 track 宽度布局内部内容，使按钮文字在 stretch 后仍居中。
- grid placement 现在保留显式 item height 和 margin。
- 更新伪浏览器和 Win32 browser 壳，使用 body/html 背景作为 canvas clear color，不再总是白底清空。
- 将 calculator 示例改为使用受支持的 grid/gap 子集，不再依赖 inline-block whitespace。
- 更新 scripting 和路线图文档，将 M7 script loading 标为可用，并把下一项主要工作转向
  host presentation 和 dirty rectangles。
- 更新架构、宿主抽象和兼容性规划文档，使下一步建议与硬件无关主线范围保持一致。
- 修复带空格的 child combinator selector 解析，例如 `.list > li` 不再错误匹配更深层后代。
- 修复表单控件状态变化未标记 DOM dirty 的问题，确保 Win32 壳中输入、select、range 等交互后会重绘。
- 改进交互控件键盘行为：`datalist` 输入支持 Tab/Enter 选择第一个匹配候选，
  `select` 支持上下方向键跨 `optgroup` 切换 option。
- 为 Win32 验证壳添加 `<a href="#id">` hash anchor 滚动。
- 更新 `jellyframe_pseudo_browser`，让它通过 `HostFrameSink` 提交帧，同时保留 BMP/PPM 验收输出。
- 更新 Win32 browser shell，使其在非结构性 DOM 变化后复用 framebuffer，并只重绘计算出的
  dirty rectangles。
- 添加嵌入式 framebuffer 后端文档，并更新 host/roadmap 文档，将平台文本和可穿戴导航列为下一优先级。
- 实现 `hidden` 渲染语义和 disabled 表单控件行为，覆盖 pointer/text/control activation 路径。

### 说明

- 已将被当前维护文档取代的旧资料归档到工作区外：旧现代/全流程兼容性分析、
  已完成的 JerryScript 接入计划和旧嵌入式 app 子集状态说明。
- `jellyframe_pseudo_browser` 在没有注入平台 `TextPainter` 时仍使用极小内置 bitmap
  字体，因此 BMP smoke-test 输出中的非 ASCII 文本会显示为 fallback glyph。Win32 browser
  shell 使用 GDI 文本测量和绘制，可用于可读的 UTF-8/中文验证。
- 示例/Win32 helper 会相对于命令行传入的 CSS 路径解析本地 linked stylesheet。缺失的外链文件会被保守忽略，
  符合当前引擎的合理降级策略。
- `@container` 和 `object-fit` 仍延后。Container query 需要有界的 style/layout
  反馈处理；`object-fit` 应等待真实 image decode 能力。

## 0.2.0-dev - 2026-06-15

### 新增

- 添加 CPU framebuffer 渲染：`FrameBuffer`、`SoftwareRasterizer` 和 `SoftwareCompositor`。
- 添加 source-over alpha compositing、opacity layer 离屏合成以及 BMP/PPM 图像输出辅助函数。
- 添加 `jellyframe_pseudo_browser`，用于完整管线 framebuffer 验证。
- 添加核心 `Event`、`MouseEvent`、`WheelEvent` 和 `EventTarget`。
- 添加类 DOM 的捕获、目标、冒泡事件派发，支持 `preventDefault`、传播停止和一次性 listener。
- 添加基于 layout/layer geometry 的 hit testing，覆盖 z-index 顺序、overflow clipping 和文本节点目标归一化。
- 添加平台无关 `InputController`，支持 pointer move/down/up、click synthesis、wheel dispatch 和 hover/active/focus 状态。
- 添加 Windows-only `jellyframe_win32_browser`。它使用核心管线渲染，用 GDI blit framebuffer，通过平台文本回调注入原生文本绘制，并将鼠标/滚轮输入转发给 `InputController`。
- Win32 browser shell 增加 viewport scrolling。滚轮事件仍先派发给核心 input controller，然后壳执行桌面默认滚动行为。
- 添加 `document_style` helper，用于收集 HTML 内嵌 `<style>` 文本并合并为 author CSS。
- 添加常见静态页面 CSS 的轻量支持：小数长度、`rem`/`em`、`max-width`、水平 `margin: auto`、`line-height` 和 `text-indent`。
- 添加 event、hit test、input synthesis、内嵌样式和 wrapped text layout 回归测试。
- 添加双语 events/hit-testing 范围文档，并更新架构、优化和 README 文档。

### 优化

- `EventTarget` listener storage 改为惰性分配，普通 DOM 节点不再携带空 listener table。
- 通过可选 `TextPainter` 回调把原生文本绘制移出 core software renderer。核心保留纯 C++ bitmap fallback，不再链接 Win32/GDI。
- 对不透明矩形填充使用直接行填充。
- offscreen compositing 在像素循环前完成裁剪。
- framebuffer resize 使用安全的像素数计算，避免极端 viewport 参数下的整数乘法溢出。
- 增加 wrapped text 行高余量，避免原生桌面文本度量略高时裁掉最后一行。

### 说明

- `jellyframe_render_core` 保持平台无关。Windows 库只由 Windows 专用例程链接。
- core 文本 fallback 刻意保持极小和可移植；Win32 browser 使用原生 GDI 文本进行 UTF-8/中文验证。

## 0.1.0-dev - 2026-06-13

### 新增

- 创建初始 C++17/CMake 工程和 `jellyframe_render_core` 核心库。
- 添加容错 HTML tokenizer/parser，支持 start/end tag、attribute、doctype、comment、text、raw-text 和 character reference。
- 添加韧性 DOM construction，支持合成 `html/body`、常见隐式闭合、void elements、不匹配 end tag 容错和 parser 资源上限。
- 添加 `jellyframe_dom_dump`，用于输出 tokenizer 结果和 ASCII DOM 树。
- 添加容错 CSS parser，支持 comment、balanced block recovery、有序 declarations、selector-list splitting、`@layer` flattening 和不支持增强 block 的保守恢复。
- 添加轻量 CSSOM rule metadata、specificity、source order 和 cascade ordering。
- 添加 selector matching：tag、class、id、descendant、child、简单 attribute selector 和 `:root`。
- 添加常见 controls 和 UI 节点默认样式，使 form、input、button、dialog、media 等节点至少保留可用框体。
- 添加 render tree、box-model layout、稀疏 layer tree 和 display-list generation。
- 添加管线检查工具：`jellyframe_style_dump`、`jellyframe_render_tree_dump`、`jellyframe_layer_tree_dump` 和 `jellyframe_pipeline_dump`。
- 添加现代 HTML/CSS 兼容性样例和双语分析文档。
- 添加微基准、CTest 注册以及 examples/tests/benchmarks 的 CMake 选项。
- 添加双语文档维护约定、路线图、版本规则、架构说明和各阶段裁剪范围文档。

### 优化

- DOM construction 流式消费 tokenizer 输出，不保存完整 token stream。
- tokenizer 在不需要 CR normalization 时避免输入复制。
- CSS rule 按 id/class/tag/universal bucket 建索引，并在 parsing 阶段预计算 selector parts。
- style cascade 使用固定槽位，避免 per-node cascade hash map。
- layer creation 保持稀疏：普通 box 绘制进父 layer，只有 clipping、stacking 或 compositing boundary 需要时才成层。
- Render Core CSS parser 统一直接 parser 选项的零值语义：规则和声明预算为 `0` 时与其他可选 parser 上限一样表示不限制；同时报告未闭合字符串和声明块，并保持可预测的恢复行为。
- 样式候选规则在重叠的 selector bucket 之间收集时改用指针集合去重，避免大样式表中重复候选的二次复杂度扫描。
- CSS 恢复现在会报告未闭合注释；上下文样式测试补充 inline custom property 变更后的缓存失效校验。
- layout 现在对外层 box 和 flex gap 的极端合法 CSS 尺寸使用饱和整数计算，避免有符号几何量溢出回绕。
- UTF-8 扫描现在会拒绝畸形、过长、代理区和超出范围的序列，并以有界的 U+FFFD replacement scalar 替代。
