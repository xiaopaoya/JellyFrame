# 变更记录

WearWeb Engine 的重要变更记录在这里。

项目使用轻量语义化版本规则。详见 `docs/versioning_zh.md`。

## Unreleased

### 新增

- 通过 callback 形式的 `document_style` API 添加平台无关的外链 stylesheet
  收集能力。核心代码仍不执行文件或网络 I/O；示例工具和 Win32 壳只在桌面验证时提供本地文件加载。
- 为常用 HTML5 语义/内容元素添加可用默认样式：`a`、`mark`、`blockquote`、
  `summary`、`details`、`address`、`hgroup`、`progress` 和 `meter`。
- 为 `progress` 和 `meter` 添加简单的软件绘制 value bar。
- 为 `wearweb_win32_browser` 添加 `--capture`，可通过 Win32/GDI 文本路径渲染页面并写出
  BMP/PPM 图片，便于视觉检查。
- 添加轻量、平台无关的表单控件状态层，覆盖嵌入式应用常用的 text input、textarea、
  checkbox、radio、range 和 select。
- 添加核心 UTF-8 文本输入、简单按键处理和有状态控件激活 API。
- 添加面向 JerryScript bridge 的 DOM mutation 原语：子节点插入/删除、属性修改、`textContent`
  更新，以及 tree/attribute/text/style/layout dirty flags。
- 添加双语 JerryScript 接入规划文档，覆盖 runtime 生命周期、binding 所有权、里程碑、风险和第一个交互式
  demo 目标。
- 添加可选 `wearweb_script` JerryScript runtime shell。该能力默认由
  `WEARWEB_BUILD_SCRIPTING=OFF` 关闭，保证 `wearweb_core` 不依赖 JerryScript 头文件或库。
- 为 scripting 构建添加初始 `wearweb_pseudo_browser --script`：执行一个外部 JavaScript
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
- 添加 `wearweb_pseudo_browser --pump-timers ms`，用于无交互窗口的 timer 脚本 smoke test。
- 添加中英文内存管理审视文档，覆盖当前所有权、嵌入式风险和 allocator/container 优化优先级。
- 添加单一聚合测试程序 `wearweb_core_tests`，覆盖平台无关回归测试，替代普通构建中的多个独立测试
  executable。
- 添加 `JERRYSCRIPT_ROOT` CMake 支持，便于使用 `third_party/jerryscript` 这样的官方 JerryScript
  本地源码树。
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

### 说明

- `wearweb_pseudo_browser` 在没有注入平台 `TextPainter` 时仍使用极小内置 bitmap
  字体，因此 BMP smoke-test 输出中的非 ASCII 文本会显示为 fallback glyph。Win32 browser
  shell 使用 GDI 文本绘制，可用于可读的 UTF-8/中文验证。
- 示例/Win32 helper 会相对于命令行传入的 CSS 路径解析本地 linked stylesheet。缺失的外链文件会被保守忽略，
  符合当前引擎的合理降级策略。

## 0.2.0-dev - 2026-06-15

### 新增

- 添加 CPU framebuffer 渲染：`FrameBuffer`、`SoftwareRasterizer` 和 `SoftwareCompositor`。
- 添加 source-over alpha compositing、opacity layer 离屏合成以及 BMP/PPM 图像输出辅助函数。
- 添加 `wearweb_pseudo_browser`，用于完整管线 framebuffer 验证。
- 添加核心 `Event`、`MouseEvent`、`WheelEvent` 和 `EventTarget`。
- 添加类 DOM 的捕获、目标、冒泡事件派发，支持 `preventDefault`、传播停止和一次性 listener。
- 添加基于 layout/layer geometry 的 hit testing，覆盖 z-index 顺序、overflow clipping 和文本节点目标归一化。
- 添加平台无关 `InputController`，支持 pointer move/down/up、click synthesis、wheel dispatch 和 hover/active/focus 状态。
- 添加 Windows-only `wearweb_win32_browser`。它使用核心管线渲染，用 GDI blit framebuffer，通过平台文本回调注入原生文本绘制，并将鼠标/滚轮输入转发给 `InputController`。
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

- `wearweb_core` 保持平台无关。Windows 库只由 Windows 专用例程链接。
- core 文本 fallback 刻意保持极小和可移植；Win32 browser 使用原生 GDI 文本进行 UTF-8/中文验证。

## 0.1.0-dev - 2026-06-13

### 新增

- 创建初始 C++17/CMake 工程和 `wearweb_core` 核心库。
- 添加容错 HTML tokenizer/parser，支持 start/end tag、attribute、doctype、comment、text、raw-text 和 character reference。
- 添加韧性 DOM construction，支持合成 `html/body`、常见隐式闭合、void elements、不匹配 end tag 容错和 parser 资源上限。
- 添加 `wearweb_dom_dump`，用于输出 tokenizer 结果和 ASCII DOM 树。
- 添加容错 CSS parser，支持 comment、balanced block recovery、有序 declarations、selector-list splitting、`@layer` flattening 和不支持增强 block 的保守恢复。
- 添加轻量 CSSOM rule metadata、specificity、source order 和 cascade ordering。
- 添加 selector matching：tag、class、id、descendant、child、简单 attribute selector 和 `:root`。
- 添加常见 controls 和 UI 节点默认样式，使 form、input、button、dialog、media 等节点至少保留可用框体。
- 添加 render tree、box-model layout、稀疏 layer tree 和 display-list generation。
- 添加管线检查工具：`wearweb_style_dump`、`wearweb_render_tree_dump`、`wearweb_layer_tree_dump` 和 `wearweb_pipeline_dump`。
- 添加现代 HTML/CSS 兼容性样例和双语分析文档。
- 添加微基准、CTest 注册以及 examples/tests/benchmarks 的 CMake 选项。
- 添加双语文档维护约定、路线图、版本规则、架构说明和各阶段裁剪范围文档。

### 优化

- DOM construction 流式消费 tokenizer 输出，不保存完整 token stream。
- tokenizer 在不需要 CR normalization 时避免输入复制。
- CSS rule 按 id/class/tag/universal bucket 建索引，并在 parsing 阶段预计算 selector parts。
- style cascade 使用固定槽位，避免 per-node cascade hash map。
- layer creation 保持稀疏：普通 box 绘制进父 layer，只有 clipping、stacking 或 compositing boundary 需要时才成层。
