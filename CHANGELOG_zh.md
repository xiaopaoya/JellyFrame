# 变更记录

WearWeb Engine 的重要变更记录在这里。

项目使用轻量语义化版本规则。详见 `docs/versioning_zh.md`。

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
