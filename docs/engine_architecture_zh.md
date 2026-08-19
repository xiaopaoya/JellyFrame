# 引擎架构

> 最后更新：2026-08-19；适用版本：0.6.0-dev


JellyFrame 参考 Blink、WebKit 和 Gecko 的大体分层，但为可穿戴目标使用更小的数据结构，并明确裁剪功能边界。

源码树现在拆成三个平台无关逻辑子项目：

- `src/render_core` / `jellyframe_render_core`：HTML/CSS/DOM/rendering 子集；
  不依赖 JerryScript、app 安装、文件系统、网络或 OS API。
- `src/app_runtime` / `jellyframe_app_runtime`：安装式 app 生命周期和可选 host-service
  队列；可依赖 `render_core` 的宿主能力与预算类型。
- `src/script` / `jellyframe_script`：可选 JerryScript 桥接层；嵌入式构建可以完全关闭。

首个保留历史的 Render Core 仓库现已建立于
[`xiaopaoya/JellyFrame-Render-Core`](https://github.com/xiaopaoya/JellyFrame-Render-Core)。
本 JellyFrame checkout 保留 in-tree Core provider 用于同步开发。首个带签名的 Core release
`v0.6.0` 建立了边界；Runtime 当前通过 package lock 接纳 `v0.6.1`，并由 release-boundary CI 消费。Render Core 已具备独立导出边界。将
`JELLYFRAME_BUILD_APP_RUNTIME=OFF`、`JELLYFRAME_BUILD_SCRIPTING=OFF` 并关闭上层示例后，
即可只配置和构建 Render Core。设置 `JELLYFRAME_INSTALL_RENDER_CORE=ON` 并执行
`cmake --install`，会导出版本化的 `JellyFrame::jellyframe_render_core`、公共头文件和能力 profile，
供独立的 `jellyframe-render-core` 仓库消费。这个阶段不引入 Git submodule，也不把 App Runtime、
JerryScript、ports 或设备协议混入 Render Core 包。

同一 checkout 还可以生成可独立使用、可复现的源码归档：

```powershell
python project_tools\package_render_core_source.py --output-dir build\dist
tar -xzf build\dist\jellyframe-render-core-0.6.1.tar.gz -C build\unpacked
cmake -S build\unpacked\jellyframe-render-core-0.6.1 -B build\core-from-archive
cmake --build build\core-from-archive --config Release --parallel
ctest --test-dir build\core-from-archive -C Release --output-on-failure
```

归档只包含 Render Core 源码、共享 CMake 边界、测试、独立 README 和许可证；不包含
Runtime、JerryScript、ports、设备契约、示例或 app 资源。in-tree 的 Core-only 配置也会显式设置
`JELLYFRAME_BUILD_DEVICE_RUNTIME_CONTRACTS=OFF`；Device contracts 为 Device OS 工作保留独立的显式构建模式。
打包器会将声明为文本的成员规范为 LF，并规范化成员顺序与
归档元数据，写出 SHA-256 sidecar。CI 会分别验证这一 monorepo export，并下载已发布的
`v0.6.1` artifact、校验其记录的 archive SHA-256，再构建、安装并由 Runtime 的
package-provider 配置消费该 release package。Standalone Core CI job 会将 monorepo 归档和
sidecar 保留为 workflow artifact；consumer 仍只能通过显式 dependency-lock 更新接纳已发布 release。

App Runtime 可以消费已经安装的 Render Core 包，而不编译当前 checkout 中的
Render Core 源码：

```powershell
cmake -S . -B build\framework-external-core `
  -DJELLYFRAME_RENDER_CORE_PROVIDER=package `
  -DJELLYFRAME_RENDER_CORE_PACKAGE_DIR=C:\path\to\render-core-install `
  -DJELLYFRAME_BUILD_RENDER_CORE_TESTS=OFF
cmake --build build\framework-external-core --config Release `
  --target jellyframe_app_runtime_tests jellyframe_device_runtime_contracts_tests
```

接受的 package 版本、engine ABI 和确定性的 source hash 固定在
`cmake/jellyframe_dependency_lock.cmake`。package 模式会校验这三个值，
并把包含 Core package 版本的能力 profile 复制到 Runtime 构建目录。默认的
`in-tree` 模式仍适用于 Core 与 Runtime 同步开发；package 模式用于验证
Core 独立发布后的消费边界。每次配置还会生成
`generated/jellyframe_render_core_provenance.json`，其中记录选中的 provider、
Core package 版本、ABI、profile 文件名、Runtime 锁定值和确定性的 SHA-256 source identity，
且不写入工作站路径。已安装的 Core package 会导出匹配的 source manifest；package consumer 会校验、与 lock 比对并复制
该 manifest 到生成目录。Runtime 或 port 构建报告应同时归档二者。内容 hash 用于识别确切消费的
Core source 集合，包括 source archive 或本地 override；它不替代 release signature、经审查的版本 lock
或发布权威。Device Runtime、JFDP 协议、launcher 和硬件 port 不属于这个 package 边界。

Runtime 侧的 provider 选择、package lock 校验和 provenance 输出位于
`cmake/jellyframe_render_core_provider.cmake`。它有意不属于
`render_core_*.cmake` 的归档边界：独立 Core 拥有源码与 package export，Runtime 则拥有接受哪一份
Core 的策略。

App manifest 也会显式声明这一边界。`1.0` 前，`runtime.minJellyFrame` 与
`runtime.minRenderCore` 都必须精确匹配 Runtime 的活跃 line 和锁定的 Core package；
packer、已安装 bundle registry 与原生桌面 package parser 都会拒绝不匹配项。

跨仓库开发时，可以用 `JELLYFRAME_RENDER_CORE_SOURCE_DIR` 让 `in-tree`
provider 指向另一个 checkout 或已解压的 Render Core 源码树。这只是本地开发覆盖项，
不是第二套公开依赖机制；package 模式和源码覆盖不能同时使用。CI 会以解压归档作为
覆盖源构建并运行 App Runtime 测试。覆盖目录必须提供 Render Core 的 CMake 边界和其文档化
源码布局（`v0.6.1` archive 使用 `src/` 加 `include/render_core/`）；原有 feature profile
与源码归属检查仍然生效。

## 计划中的仓库边界

当前源码 monorepo 是开发便利，不是最终所有权模型：

| 未来仓库 | 负责内容 | 迭代规律 | 当前状态 |
| --- | --- | --- | --- |
| `jellyframe-render-core` | HTML/CSS/DOM、layout、paint、input 和可选 feature family | 高频优化与兼容性发布 | 物理仓库已建立；首个带签名的 `v0.6.0`、当前锁定的 `v0.6.1`、standalone CI、安装/导出与 package-consumer 边界均已验证。 |
| `jellyframe` | App Runtime、Japp 格式、JerryScript binding、桌面壳和开发工具 | 慢速迭代，维护 App 兼容契约 | 当前 Runtime 源码边界；可消费锁定 Core package 或 in-tree Core |
| `jellyframe-device-os` | launcher、registry、Device Runtime、JFDP、板卡 port 和官方镜像 | 强硬件依赖的实验性迭代 | 尚未物理迁出；D0 契约仍位于过渡位置 |
| JerryScript | 第三方脚本引擎 | 跟随上游 commit/tag | 可选依赖，由 Runtime/port 构建锁定 |

`src/device_runtime_contracts/device_install_transaction.*` 和
`src/device_runtime_contracts/device_runtime_protocol.*` 是明确的 D0 边界。它们虽然是平台无关
契约，但表达的是设备安装和 JFDP，而不是 App Runtime 行为。它们不能进入 Render Core
package，最终应迁移到 `jellyframe-device-os` 或小型 `device_runtime_contracts`
package。D0 将其编译为独立的 `jellyframe_device_runtime_contracts` target，并在不链接
App Runtime 或 Render Core 实现对象的情况下运行测试。未来 Device OS package 迁移完成前仍处于
monorepo 过渡状态；typed JFDP desktop reference dispatcher 不表示已经存在物理设备 transport。

物理拆分需要同时满足三个条件：独立可构建的 Core source archive/package、锁定 Core
版本/ABI 的 Runtime consumer，以及不导入 Core 实现细节而消费同一 Runtime 契约的
Device OS reference host。以上门槛都不要求 Git submodule。

拆仓、发布、profile 和保留历史的具体规则见
[render_core_release_policy_zh.md](render_core_release_policy_zh.md)。

## 模块化状态

仓库模块化和内部 feature 模块化是两项不同结论。Core/Runtime 的 package boundary 已关闭：Core
已有保留历史的独立仓库、首个带签名的 `v0.6.0` release、当前锁定的 `v0.6.1`、standalone archive/install 路径、Runtime
package consumer、provenance record 和本地 source override。in-tree Core-only 配置也有独立回归检查，确保不会创建
Device contracts target 或测试。Device OS boundary 尚未物理迁出；其 launcher、
registry、port 和 image tooling 应整体迁移，不能零散复制进 Runtime 或 Core。

Render Core feature family 是 compile-time profile choice，不是运行时 native plugin。Canvas2D、modern paint、
flex/grid paint ordering 和 advanced form submission 已有明确 source selection 与 feature-off fallback。`core.document`
和 `core.paint` 仍是必选 baseline。style resolution、layout、layer construction、software renderer 等大 baseline
实现不会只因文件大小就拆分：内部拆分必须先完成 dependency、allocation、hot-path audit，并维持当前的
no-indirection boundary。App 永远不携带 native rendering module。

接纳新的 Core release 前，maintainer 必须完成相应 standalone、package-consumer 与 source-override 检查；
内部拆分还必须留下 dependency、allocation 与 hot-path review 结论。

```text
HTML bytes/string
  -> HtmlTokenizer
  -> HtmlTreeBuilder
  -> DOM

CSS bytes/string
  -> CssParser
  -> CssStyleSheet / CssRule
  -> StyleResolver 内部 indexed rule set

Platform-neutral input
  -> HitTester
  -> InputController
  -> Event / MouseEvent / WheelEvent
  -> DOM nodes 上的 EventTarget dispatch

Host async services
  -> decode/network/install workers
  -> bounded completion queue
  -> UI/main task event dispatch or dirty marking

DOM + StyleResolver
  -> RenderTreeBuilder
  -> RenderObject tree
  -> LayoutEngine
  -> LayoutBox tree
  -> LayerTreeBuilder
  -> LayerNode tree
  -> DisplayList
  -> SoftwareRasterizer / SoftwareCompositor
  -> FrameBuffer / platform renderer
  -> HostFrameSink present / panel flush completion
```

## 类浏览器分层

- `HtmlTokenizer`：容错 token stream 生成。
- `HtmlTreeBuilder`：带 open-elements stack 的韧性 DOM construction。
- `CssParser`：参考 CSS Syntax 的 rule/declaration parser 和错误恢复。
- `CssStyleSheet`：轻量 CSSOM rule list。
- `StyleResolver`：cascade、selector matching 和 indexed rule collection。
- `RenderTreeBuilder`：过滤不渲染 DOM，并附加 computed style。
- `LayoutEngine`：从 render objects 生成几何。
- `LayerTreeBuilder`：把绘制命令组织进稀疏 clip、stacking、composite layers，并可为简单后端 flatten。
- `DisplayList`：面向 framebuffer backend 的简单 rectangle/text command list。
- `SoftwareRasterizer` / `SoftwareCompositor`：CPU 验证 renderer，支持 source-over alpha compositing、可选平台文本绘制和 BMP/PPM 输出。
- `HostFrameSink`：本帧显示提交边界。嵌入式宿主应在 panel flush 完成或缓冲区安全移交后，才允许下一帧重用同一 framebuffer/target buffer。
- `HitTester`：通过 layout 和 layer geometry 将 viewport 坐标映射到 DOM event target。
- `InputController`：将平台无关 pointer/wheel input 转成类 mouse events、hover/active/focus state 和 click synthesis。
- `EventTarget`：保存 C++ listeners，并执行类 DOM 的 capture、target 和 bubble phases。
- `Host async services`：位于 `app_runtime` 的可选宿主服务，用于图片/音频/轻量视频、网络数据请求和安装式 bundle。
  它们不拥有 DOM 或 framebuffer，只通过有界 completion events 回到 UI/main task。
- `PipelineStatistics`：可选只读统计入口，用于统计 DOM、render、layout、layer、display-list、
  framebuffer、resource 和 arena 使用情况。它面向验证壳和 benchmark，不进入渲染热路径。

## Rule Indexing

现代浏览器会构建 rule sets，避免每个元素都扫描所有规则。JellyFrame 现在根据最右侧 simple selector 建 buckets：

- id bucket
- class bucket
- tag bucket
- universal bucket

每个 `CssRule` 保存：

- selector text
- parsed selector parts
- specificity
- source order
- index key
- ordered declarations

Style resolution 时，resolver 只收集相关 buckets，按 source order 排序，然后做 selector matching 和 cascade comparison。

## 当前取舍

- Rule indexing 刻意保持简单、低分配。
- Selector 支持有限但实用：compound、descendant、child、adjacent/general sibling、
  attribute、`:root`、部分动态伪类，以及文档化的 `:is()` / `:where()` 子集。
- 不支持的现代 selectors 尽量在插入 CSSOM 前跳过。
- Render object 保持紧凑的 block/inline/text 形态；layout 为常见 flex row
  和响应式 grid card 模式提供小型专用路径。
- `css.flex-grid` profile family 现在拥有独立的 flex paint-order helper。
  其余 flex/grid 布局算法仍留在 `layout.cpp`，因为它们调用共享的递归 layout、
  geometry 和预算路径；只有在能用窄内部接口保持这些契约且不引入运行时间接层后，才继续拆分。
- Render tree、layout tree 和 layer tree builder 都同时提供 heap 与 `MonotonicArena`
  分配路径；嵌入式 benchmark 使用 arena 路径以减少小对象堆抖动。
- Layer tree 支持稀疏裁剪、opacity 边界、positioned stacking hints 和保守 compositing boundaries。
- Display list 使用有界 rectangles、gradients、text 和 image-surface-handle commands。
  Canvas 输出通过同一条有界 image surface 路径进入显示列表。
- Dirty-region planning 可以让 paint-only 变更复用现有 frame，对有界 layout-dirty
  frame 比较 previous/current layout，并提供 affected layers / display commands 的
  display-invalidation 诊断。完整 retained display-list diffing 仍延后。
- 文本 layout 接受 `TextMeasureProvider`；文本输出接受 `TextPainter`。core fallback 保持很小，Win32 browser 同时使用 GDI 测量和绘制。
- Event dispatch 保持平台无关。核心用户可以挂 C++ callbacks；可选 JerryScript 构建也会把文档化的
  `addEventListener` 和 `on*` handler 子集接到同一条事件路径。

## 延后工程领域

当前公开契约是上文描述的架构。下面这些领域目前不是 app 作者或 port 作者必须掌握的内容，
也不应在能力矩阵明确写入前视为可用行为：

- 独立 selector 模块内部结构。
- 更细的 retained subtree 和 retained display-list diffing。
- 面向重复 class pattern 的 computed-style sharing。
- 更完整的 DOM ownership/arena 策略。
- 面向无法保留所选 framebuffer 表示的目标设备的 tile 或 scanline presentation 路径。
- 更进一步的 flex/grid 布局算法物理拆分。需要先完成接口和分配审计；当前的 paint-order
  拆分是已接受的低风险边界。
