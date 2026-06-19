# 变更记录

JellyFrame Engine 的重要变更记录在这里。

项目使用轻量语义化版本规则。详见 `docs/versioning_zh.md`。

## Unreleased

### 新增

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
- 添加 `.jfapp` V0 安装式 bundle 输出：`tools/package_app.py` 和 `jellyframe_cli.py package`
  可生成小端、未压缩、固定索引的二进制资源包，并在报告中记录 bundle CRC/SHA-256 和分段大小。
- 伪浏览器和 Win32 browser 壳现在可以通过 `--app path.jfapp` 直接加载安装式 bundle，
  用于验证 bundle 与源包目录渲染结果一致。
- 添加桌面 installed-app registry mock：`jellyframe_cli.py registry install/list/path/remove`
  可校验 `.jfapp`、通过 staging 安装 bundle、原子提交 registry，并为后续系统壳 app manager 打基础。
- 添加 ESP32-S3 N16R8 benchmark 配置与 16MB 分区表，并记录 2026-06-19 实机基线：
  16MB Flash、8MB octal PSRAM、300x300 / 40 cards / 20 iterations 完整 pipeline 通过。

### 变更

- 根据 ESP32-S3 解码实验审计更新 HAL、宿主抽象、运行循环、app packaging 和路线图：
  MP3 与小尺寸 MJPEG/图片 decode 可作为可选宿主服务，H.264 不进入默认 ESP32-S3 profile；
  网络继续只作为运行时数据 API，不作为远程页面资源 loader。
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
