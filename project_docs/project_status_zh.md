# JellyFrame 项目状态与后续里程碑

日期：2026-06-16

本文记录主线项目状态。它只描述 JellyFrame 的硬件无关核心、桌面验证工具、能力检查工具，以及已经审查合并进仓库的移植支撑代码。真实屏幕、触摸、按键、电源管理、ESP-IDF/LVGL 板级驱动等硬件移植实现不属于主线核心开发范围。

## 职责边界

主线维护范围：

- HTML/CSS/DOM/layout/render/input/script 等平台无关核心。
- CPU framebuffer、dirty rectangle、embedded framebuffer adapter 等可移植渲染支撑。
- 文本测量/绘制抽象、bitmap font backend、字体包生成器和能力检查器。
- Win32/pseudo browser 等桌面验证壳。
- 对外部移植提交进行审查、去实验化、命名统一、边界收敛和主线合并。
- 文档、测试、基准和发布准入规则。

不属于主线核心开发范围：

- 真实开发板屏幕驱动、触摸驱动、按键/表冠驱动和电源管理。
- ESP-IDF、LVGL、FreeRTOS 或具体 BSP 的长期维护。
- 在核心中加入文件系统、网络栈、字体文件加载器、GPU API 或窗口系统。
- 为某块板子调试硬件时序、DMA、总线带宽、背光、睡眠唤醒和中断细节。

移植包进入本仓库的标准是：只合并可复用、边界清晰、不会污染核心的平台支撑代码；真实硬件差异保留在 `ports/` 或外部 BSP 中。

## 已完成的核心能力

解析与 DOM：

- 容错 HTML tokenizer/parser，支持常见标签、属性、文本、注释、doctype、raw text、字符引用和错误恢复。
- 轻量 DOM tree，支持元素/文本节点、属性、父子关系、树修改和 dirty invalidation。
- 迭代式 DOM 子树销毁和 `textContent` 替换，降低深树栈压力。
- DOM 统计 instrumentation，可输出深度和属性数量。

CSS 与样式：

- 容错 CSS parser 和轻量 CSSOM。
- 支持 tag/class/id、descendant、child、attribute、`:root` 等选择器子集。
- Indexed rule buckets 和候选规则缓存，降低重复匹配成本。
- 常见属性子集，包括颜色、背景、边距、内边距、边框、文本、简化 flex/grid sizing、gap、aspect-ratio、近似 box-shadow 等。
- 明确跳过 quirks mode、完整现代 selector、container query、复杂 CSSOM 动态能力等高成本特性。

渲染管线：

- Render tree、layout tree、layer tree 和 display list。
- Block/inline 简化布局、文本换行、常见表单控件 intrinsic size。
- 响应式 grid card 子集和固定 grid template 子集。
- 稀疏 layer tree、裁剪、opacity、z-index/stacking hints 和 flatten。
- CPU software rasterizer/compositor，支持矩形、圆角近似、渐变/阴影近似、文本、表单控件绘制和 BMP/PPM 输出。
- `FrameBuffer` dirty repaint 和 `HostFrameSink` presentation。
- `embedded_framebuffer` 可把 RGBA framebuffer 转为 RGB565/BGR565/RGB332/Gray8/1-bit target。

输入与事件：

- Hit testing。
- DOM-style capture/target/bubble 事件流。
- `InputController`，支持 pointer、wheel、click synthesis、hover/active/focus。
- 表单控件状态层：text input、textarea、checkbox、radio、range、select。
- 键盘文本输入、Backspace、focus navigation、activate focused。
- `pointerdown`/`pointerup`、`touchstart`/`touchend` 事件桥接。

JavaScript runtime：

- 可选 `jellyframe_script`，保持在 `jellyframe_core` 之外。
- JerryScript runtime lifecycle、eval、异常报告。
- DOM binding：`window`、`document`、`getElementById`、`createElement`、`createTextNode`、`appendChild`、`removeChild`、attribute、`textContent`。
- 事件 binding：`addEventListener`、`removeEventListener`、event object、default prevention、propagation control。
- 表单属性：`value`、`checked`、`selectedIndex`、`select.value`。
- Host-pumped timers：`setTimeout`、`clearTimeout`、`setInterval`、`clearInterval`。
- Classic document script loading：inline `<script>` 和壳层 callback 提供的本地 `<script src>`。
- 嵌入式 app helper：`dataset`、`children`、`parentElement`、简化 `matches`/`closest`、小型 `element.style`、`hidden`、`disabled`。

文本与字体：

- `TextMeasureProvider` 和 `TextPainter` 抽象。
- Win32/GDI 验证后端。
- 静态 bitmap font backend。
- BDF 子集生成器 `jellyframe_font_pack_gen`。
- 字体覆盖检查：`jellyframe_capability_check --emit-used-chars` 和 `--font-coverage`。
- bitmap font 缺字现在绘制可见且宽度稳定的 fallback 方框。

工具、示例与验证：

- `jellyframe_dom_dump`、`cssom_dump`、`style_dump`、`render_tree_dump`、`layer_tree_dump`、`pipeline_dump`。
- `jellyframe_pseudo_browser`，用于全管线 framebuffer 输出。
- `jellyframe_win32_browser`，用于桌面交互验证、GDI 文本和截图。
- `jellyframe_capability_check`，用于扫描支持/降级/不支持特性。
- `jellyframe_embedded_host_demo`，平台无关静态资源/RGB565/bitmap font/input smoke。
- 小型 app 验收页面：天气、时钟、计时器、计算器。
- 聚合回归测试 `jellyframe_core_tests`。
- 微基准和 virtual board benchmark。

## 已合并的移植支撑代码

这些内容来自外部移植实验或为移植服务，但已经按主线边界合入：

- `ports/esp32s3-idf` bring-up 工程：作为 ESP32-S3/QEMU 参考，不代表主线承担真实硬件移植维护。
- ESP32-S3 静态资源 bundle 生成和 P2 smoke 资源。
- ESP32-S3 8 MB flash 分区布局。
- ESP32-S3 RGB565 panel HAL reference：strided flush、packed dirty-rect flush、scratch row packing、flush 统计。
- ESP32-S3 P4/P5/P6 smoke 支撑：bitmap font callback、有界开发板输入队列、焦点/文本/控件派发、
  dirty-rectangle RGB565 提交。当前内置 bring-up 字体只用于验证，不是生产中文字库。
- QEMU PSRAM 梯度 benchmark 文档与结果。
- `ports/virtual_board` 桌面估算工具。

主线对这些文件的责任是保持可构建、命名统一、接口边界清晰，并为外部硬件移植提供参考。真实板级驱动质量、引脚、时序、DMA、显示芯片、触摸芯片和 RTOS 集成由移植侧负责。

## 当前可用性判断

JellyFrame 现在已经适合开发和验证“小型本地嵌入式 UI app”：

- 天气、时钟、计时器、计算器、设置页、简单仪表盘可行。
- 可以使用 HTML/CSS/JS 子集组织 UI，而不必把所有界面手写成 canvas。
- 支持现代感较强但受控的 UI：grid/gap、卡片、按钮、表单控件、基础交互、timer、DOM mutation。
- 不适合直接运行任意现代网站或完整前端框架。

最主要的剩余风险不是“能不能跑一个简单 app”，而是：

- 动态 DOM 后的增量重建仍偏保守。
- 完整 framebuffer 仍是默认渲染假设。
- 文本 backend 已有默认路线，但 LVGL/vendor adapter 还没有主线示例。
- app 打包格式和发布工作流还没有定型。
- JerryScript 子集可用，但还需要更系统的运行时资源预算和长时间稳定性验证。

## 后续核心里程碑

### M7.5：现代 CSS authoring 兼容性补齐

目标：让常见框架生成的现代 CSS 不至于让页面结构和基础风格崩掉，同时继续守住嵌入式子集，
不追求昂贵的完整浏览器特性。

状态：已开始。兼容性报告中最高收益的项目已经落地：custom-property `var()` fallback
resolution、有界条件 `@media`、动态 pseudo-class invalidation、`:is()` / `:where()`、
sibling selectors、简化 flex grow/shrink/basis sizing，以及有界 `relative`/`absolute`/
`fixed` positioned layout。第一版保守的 `@supports` declaration-query 子集也已可用。

剩余任务：

- 只围绕当前嵌入式 CSS 子集已经支持的 declaration 扩展 `@supports` 测试；保持
  `selector()`、`:has()` 和不安全特性求值为 false。
- 继续延后完整 `:has()`、完整 `@container`、完整动画/filter/image pipeline，除非小型嵌入式
  app 给出明确收益。

### M7.6：HTML parser 与 DOM 兼容性基线

目标：减少普通现代 app HTML 在 parser/DOM 层的意外差异，但不引入旧浏览器兼容包袱。

状态：已开始。第一批兼容项已经落到 tokenizer、tree builder 和 layout/layer 文本路径：
非 void 元素自闭合斜杠语义、DOM 空白保留与显示期文本规范化、`textarea`/`title`
RCDATA-like 字符引用解码、更宽的紧凑常用 entity 表、numeric legacy remap，以及
parser 预算诊断。完整 quirks mode、parser reentrant `document.write()`、speculative
parser、完整 adoption-agency 和完整 table foster parenting 继续不进入范围。

第一批任务：

- 修正非 void 元素自闭合斜杠语义，同时保留真正 void 元素的叶子行为。已完成。
- tree builder 保留普通文本空白，把空白折叠交给 layout/rendering。已完成。
- 将 `textarea` 和 `title` 作为近似 RCDATA 处理，支持字符引用解码。已完成有界简化路径。
- 扩充常用 named character references，并收紧 numeric/semicolon 恢复规则。已完成紧凑常用表和 numeric legacy remap。
- 暴露 parser 预算上限诊断，避免只有静默截断。已覆盖 node、depth 和单元素 attribute 上限。
- 增加最小 doctype/comment/document metadata 支撑。

第二批任务：

- 增加 html/head/body 路由所需的最小 insertion-mode 子集。
- 增加用于 `innerHTML`、template 和组件片段的 fragment parsing。
- 实现 `template.content` 风格 inert fragment。
- 扩大 rich-text、select/option 和 table 常见场景的 implied end tags。
- 增加有界 table tree construction 和 module-script 显式诊断。
- 只在能安全降级时增加最小 inline SVG/foreign-content 边界识别。

### M8：核心运行循环与增量更新收敛

目标：把桌面壳和平台无关 demo 中的 frame loop 经验固化成更清楚的核心/宿主契约。

状态：已开始。`src/core/frame_update.h` 提供第一版平台无关更新计划和
`FramePipelineCacheState` cache snapshot helper，`src/core/frame_loop.h` 增加每帧有界
input/timer 工作规划。`../docs/run_loop_contract_zh.md` 记录推荐运行循环。核心测试已覆盖一条小型宿主帧序列：
首帧、干净帧、paint-only、layout-dirty 和 framebuffer 尺寸变化，也新增了长时间 input/timer
积压 smoke，验证每帧消费有上限，且工作排空后能回到 clean cached idle frame。Planner 还包含第二阶段
repaint 检查：layout 解析出新内容高度后，宿主可在内容高度变化时回退到 full framebuffer repaint。

任务：

- 明确 first paint、input dispatch、timer pump、dirty collection、rebuild/repaint、present 的推荐顺序。
- 把非结构性 DOM mutation 尽量保持在 paint-only 或小范围 repaint。
- 为结构性 mutation 保留保守全 viewport rebuild，但输出清晰诊断。
- 增加长时间 timer/input smoke，避免队列或 dirty flags 越积越多。第一版有界 planner smoke 已完成。

### M9：更细的 invalidation 与 subtree reuse

目标：减少脚本 app 交互后不必要的全管线重建。

状态：已开始。Dirty flag 清理和 dirty-region 遍历已改为显式工作栈，并按聚合 dirty 位剪枝，
降低深层文档的栈压力和干净子树扫描。Dirty-region 的 layout 匹配现在会对 previous/current
layout tree 各扫描一次并聚合 dirty node bounds，不再为多个脏节点反复全树查找。
结构性 `DomDirtyTree` 更新现在会跳过 previous-layout 保留，并规划为保守 full-frame repaint。
Layout/style/text 变化只有在第二阶段 repaint plan 确认 framebuffer 仍匹配新内容尺寸时，才比较 previous/current layout 做增量重绘。
表单控件激活也过滤了少量 no-op 路径，避免 radio/select 状态未变化时安排 paint 工作。
完整 subtree reuse 仍是后续工作。

任务：

- 梳理 tree/style/layout/paint dirty flags 的传播边界。
- 优先复用 render/layout/layer 子树中未变化部分。
- 为 dirty layer/display-command invalidation 增加测试和诊断。
- 保持 fallback 路径简单：遇到复杂结构变化仍允许全 viewport rebuild。

### M10：文本后端适配与字体工作流完善

目标：让中文和小屏文本在正式应用中更稳。

任务：

- 保持 bitmap font backend 作为最低成本默认路线。
- 增加 LVGL/vendor text backend adapter 的平台无关示例或接口说明。
- 完善字体覆盖、缺字、粗体、宽 glyph、标点和字号缩放测试。
- 让 capability checker 更直接地报告字体包大小和缺字影响。

### M11：资源包与 app packaging

目标：把示例资源加载升级为可重复的本地 app 打包流程。

任务：

- 统一 HTML/CSS/classic script/font/resource manifest。
- 明确资源大小、路径解析、缓存和缺失资源策略。
- 让 capability checker、font pack generator 和资源 bundle generator 能串成一条桌面构建链。
- 保持核心无文件系统、无网络；I/O 仍由宿主提供。

### M12：内存与 allocator 深化

目标：继续降低小对象分配和堆碎片。

任务：

- 原型化 `DomOwner` 和 detached-node instrumentation。
- 评估 mutable/scripted DOM 的 arena 或 pool 策略。
- 扩大 small-vector/compact-list 使用范围，但不牺牲可读性。
- 为预算超限路径增加更多稳定性测试。

### M13：可选 tiled/scanline presentation

目标：支持无法保留完整 framebuffer 或 target buffer 的设备。

任务：

- 评估 tile renderer 或 scanline compositor。
- 明确哪些 display commands 可 tile 化，哪些需要 fallback。
- 保留当前 full framebuffer 路径作为默认可靠路径。

该项只有在目标设备确实无法承受完整 framebuffer 时才应提前。

## 下一步建议

主线下一步优先顺序：

1. M7.6：落地第一批 HTML parser/DOM 兼容项。
2. M8：继续加深运行循环和 dirty update 契约测试。
3. M9：做更细的 invalidation/subtree reuse。
4. M10：补文本 backend adapter 和字体工作流验证。
5. M11：整理 app packaging。
6. M12：继续内存/allocator 优化。
7. M13：按硬件需求决定是否做 tiled presentation。

硬件移植侧可以并行推进，但不应阻塞主线核心：移植侧遇到“核心缺能力”时，提交最小复现和需求边界；主线再判断是否进入上述里程碑。
