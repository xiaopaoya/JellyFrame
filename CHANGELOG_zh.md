# 变更记录

JellyFrame Engine 的重要变更记录在这里。

项目使用轻量语义化版本规则。详见 `docs/versioning_zh.md`。

## Unreleased

### 新增

- 项目正式更名为 `JellyFrame`；`WearWeb` 现在仅作为早期代号出现在文档中。
- 添加平台无关的 `TextMeasureProvider`，让 layout 能使用宿主文本 metrics，同时继续把字体 API
  留在 `jellyframe_core` 之外。
- 为 display command 添加最小文本绘制语义：水平对齐，以及单行/可换行文本。
- 在已有 GDI 文本绘制之外，为 Win32 壳添加 GDI 文本测量注入，使 UTF-8/中文桌面验证更接近真实效果。
- 添加双语文本后端文档，描述测量/绘制契约和 fallback 限制。
- 为 `jellyframe_capability_check` 添加字体覆盖能力：可输出源码中用到的非 ASCII 字符，并用 UTF-8
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
- 添加 `core/budget.h` helpers，把 `HostBudgets` 映射到 HTML/CSS parser、render/layout/layer/
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
  `JELLYFRAME_BUILD_SCRIPTING=OFF` 关闭，保证 `jellyframe_core` 不依赖 JerryScript 头文件或库。
- 为 scripting 构建添加初始 `jellyframe_pseudo_browser --script`：执行一个外部 JavaScript
  文件并报告结果或异常。
- 添加 `examples/script_cases/runtime_probe.*`，作为第一个脚本 runtime 验收页面。
- 添加 JerryScript M3 最小 DOM binding：`window`、`document`、`getElementById`、
  `createElement`、`createTextNode`、`appendChild`、`removeChild`、`setAttribute`、
  `getAttribute` 和 `textContent`。
- 添加 `examples/script_cases/dom_mutation_probe.*`，用于通过伪浏览器验证脚本驱动的 DOM mutation。
- 添加 M4 JavaScript 事件 binding：`addEventListener`、`removeEventListener`、event object、
  default prevention 和 propagation control。
- 为 Win32 browser shell 添加 scripting 支持，使桌面 native input 可以派发到 JavaScript listener，
  并在 DOM mutation 后重绘。
- 添加 `examples/script_cases/event_probe.*`，用于交互式事件桥验收。
- 添加 M5 JavaScript 表单控件属性：`value`、`checked`、`selectedIndex` 和 `select.value`。
- 在 `examples/app_cases` 下添加天气、时钟、计时器和计算器应用式验收示例。
- 添加中英文嵌入式应用子集文档，说明 M6 后能构建什么，以及哪些浏览器假设被刻意排除。
- 添加 M6 宿主泵动 timer：`setTimeout`、`clearTimeout`、`setInterval` 和 `clearInterval`。
- 添加 `jellyframe_pseudo_browser --pump-timers ms`，用于无交互窗口的 timer 脚本 smoke test。
- 添加中英文内存管理审视文档，覆盖当前所有权、嵌入式风险和 allocator/container 优化优先级。
- 添加单一聚合测试程序 `jellyframe_core_tests`，覆盖平台无关回归测试，替代普通构建中的多个独立测试
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
- 添加第一版宿主抽象草案和 `src/core/host.h`，覆盖 resource、clock、frame sink
  和 budget structs。
- 添加 `examples/script_cases/inline_loading_probe.*`，用于验证自动 document script loading。
- 添加 `font-weight` 解析、继承和 display-list 传递；核心 fallback 用近似加粗绘制，
  Win32/GDI 文本路径会选择原生字重。
- 添加轻量列表标记支持：`list-style`/`list-style-type`、`ul`/`ol` 原生轻量 marker，
  以及面向常见自定义有序列表的极小 `::before content: counter(...)` 路径。
- 添加简单固定 grid 列模板，例如 `grid-template-columns: 120px 1fr`，用于描述列表和设置页式结构化数据。
- 添加 `SoftwareCompositor::render_into` dirty-rectangle framebuffer 重绘，以及
  `HostFrameSink` presentation 辅助函数。
- 添加 `dirty_region`，作为第一版自动 dirty-rectangle 来源，用于直接文本、属性和表单控件
  mutation。树结构 mutation 仍保守重绘整个 viewport。
- 添加 `embedded_framebuffer`，作为平台无关 `HostFrameSink` adapter，可把 dirty rectangles
  转换到调用方持有的 RGBA8888/BGRA8888、RGB565/BGR565、RGB332、Gray8 或 1-bit
  单色显示 buffer。
- 添加 ESP32-S3 P3 显示 bring-up 支持：8 MB flash 分区布局、RGB565 packed dirty-rectangle
  flush callback、scratch buffer 逐行打包，以及覆盖全帧和局部 dirty 提交的 QEMU 显示 smoke 路径。
- 添加面向嵌入式 app 的 JavaScript helpers：`children`、`parentElement`、简单 selector
  `matches`/`closest`、基于已有属性的 `dataset` 快照、可写的小型 `element.style` 对象，
  以及 boolean `hidden`/`disabled` reflection。
- 添加 mouse-like `pointerdown`/`pointerup` 和 `touchstart`/`touchend` 事件派发，用于可穿戴按下反馈。
- 添加 `jellyframe_capability_check` 桌面 HTML/CSS/JS 扫描器，用于报告受支持子集、降级特性和不支持 API。
- 添加保守的现代长度函数支持：当参数能归约为受支持长度时，解析 `min()`、`max()`、`clamp()`
  和简单 `calc(A +/- B)`。
- 添加简化 `flex-wrap` 行换行，用于常见卡片/盒子布局。
- 添加外链 stylesheet 合并、语义 fallback 样式、inline 高亮绘制、DOM mutation invalidation
  和表单控件 fallback 行为的回归测试。启用 scripting 的构建还会加入 JerryScript runtime
  生命周期和异常路径测试。

### 改进

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
- 改进 core 文本 fallback，使测量和绘制按 UTF-8 码点处理，而不是把每个非 ASCII 字节当成独立 glyph。
- 改进 bitmap font backend：缺字现在会绘制可见且宽度稳定的 fallback 方框，而不是只保留空白 advance。
- 改进文本换行启发式，单个不可断符号即使测量宽度略超小控件，也不会被当成多行文本。
- 改进 grid layout：auto-width grid item 会按分配到的 track 宽度布局内部内容，使按钮文字在 stretch 后仍居中。
- grid placement 现在保留显式 item height 和 margin。
- 更新伪浏览器和 Win32 browser 壳，使用 body/html 背景作为 canvas clear color，不再总是白底清空。
- 将 watch calculator 示例改为使用受支持的 grid/gap 子集，不再依赖 inline-block whitespace。
- 更新 scripting 和路线图文档，将 M7 script loading 标为可用，并把下一项主要工作转向
  host presentation 和 dirty rectangles。
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

- `jellyframe_core` 保持平台无关。Windows 库只由 Windows 专用例程链接。
- core 文本 fallback 刻意保持极小和可移植；Win32 browser 使用原生 GDI 文本进行 UTF-8/中文验证。

## 0.1.0-dev - 2026-06-13

### 新增

- 创建初始 C++17/CMake 工程和 `jellyframe_core` 核心库。
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
