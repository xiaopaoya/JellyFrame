# 变更记录

WearWeb Engine 的重要变更会记录在这里。

项目使用轻量语义化版本规则。详见 `docs/versioning_zh.md`。

## 0.1.0-dev - 2026-06-13

### 新增

- 创建初始 C++17 CMake 项目。
- 添加 `wearweb_core`，包含最小 DOM、HTML 解析、CSS 解析、样式解析、
  block layout 和 display-list 生成。
- 添加 `wearweb_demo`，用于把 HTML/CSS 输入转换为平台无关绘制命令。
- 添加初始路线图。
- 添加双语文档维护约定和中文文档文件。
- 添加基于 WHATWG HTML Living Standard 的首版双语 HTML tokenizer 裁剪范围文档。
- 添加独立的宽容 HTML tokenizer，支持 start/end tag、属性、doctype、comment、
  text、raw-text 和 character reference 处理。
- 重构 `HtmlParser`，改为消费 tokenizer 输出后构建 DOM。
- 添加 tokenizer 回归测试。
- 添加双语 HTML tree-builder 裁剪范围文档。
- 添加更有韧性的 DOM construction，支持合成 `html/body`、常见隐式闭合、
  void elements、不匹配 end tag 容错和 parser 资源上限。
- 添加 `wearweb_dom_dump` DOM 可视化例程，可输出 tokenizer 结果和 ASCII DOM 树。
- 将 DOM construction 拆分为 `HtmlTreeBuilder`，并添加 `HtmlTokenSink`，使
  parser construction 可以流式消费 tokenizer 输出，不必保存完整 token stream。
- 优化 tokenizer 输入处理，在不需要 CR 归一化时避免复制输入，并优化 raw-text
  end-tag 匹配以避免临时字符串。
- 添加基于 Blink、WebKit 和 html5ever 源码结构的双语 parser 架构说明。
- 添加宽容 CSS parser，支持 comment 处理、balanced block 恢复、有序
  declarations、selector-list 拆分、`@layer` 展开，以及对不支持现代增强 block
  的保守处理。
- 将 CSS declarations 从 unordered map 改为有序列表，以保留 fallback
  declarations。
- 更新 style resolution，使不支持的属性值不会覆盖之前已支持的 fallback 值。
- 添加 CSS parser 回归测试和双语 CSS parser 裁剪范围文档。
- 添加轻量 CSSOM，包含 `CssStyleSheet`、rule metadata、specificity 和 source
  order。
- 更新 style resolution，使用 author-style cascade 顺序：`!important`、
  specificity 和 source order。
- 添加 `wearweb_cssom_dump` 以及双语 CSSOM/cascade 裁剪范围文档。
- 添加现代 HTML/CSS 兼容性样例和双语分析文档，对比现代浏览器预期行为与当前
  WearWeb DOM/CSSOM 输出。
- 添加 descendant selectors、child selectors、简单 attribute selectors 和
  `:root` 的 selector matching。
- 添加小型 default style layer，让常见 controls 和 UI elements 中的 form、
  input、button、dialog、media nodes 至少保持可用框体。
- 扩展 computed style，加入 display variants、border、radius、min size、shadow
  和 overflow 字段。
- 添加 `wearweb_style_dump`，用于检查功能 UI 节点的 computed styles。
- 添加第一版 render tree 层，包含 `RenderTreeBuilder`、`RenderObject` 和
  view/block/inline/text render object 类型。
- 更新 layout，使其消费 render tree，而不是直接遍历 DOM。
- 添加 `wearweb_render_tree_dump`、render tree 测试和双语 render tree 裁剪范围文档。
- 添加感知 box model 的 layout 和基于矩形的 border painting。
- 添加 `wearweb_pipeline_dump`，用于端到端检查 DOM/render/layout/display-list。
- 添加 `wearweb_microbench` 和双语嵌入式优化说明，并记录 Release 微基准基线。
- 修正默认 `dialog` 行为，closed dialog 默认不渲染，除非 CSS 显式使其可见。
- 添加类浏览器 CSS rule indexing，按 id/class/tag/universal buckets 建索引。
- 在 CSS parsing 阶段预计算 selector parts 和 rule index keys。
- 在 80-card benchmark 场景中，将 Release render-tree 微基准从约 2860 us 降至
  810 us。
- 添加双语 engine architecture 文档。
- 添加稀疏 `LayerTreeBuilder`，包含 root、clip、stacking 和 composited layer nodes。
- 添加 overflow clipping、opacity、transform、positioned content、显式 `z-index`、
  shadows 和 rounded clips 的成层原因。
- 添加 `wearweb_layer_tree_dump` 和 `wearweb_layer_tree_tests`。
- 将端到端管线改为 `LayoutBox -> LayerNode -> DisplayList`，layout 不再负责 painting。
- 添加面向嵌入式 library-only 构建的 CMake 选项：`WEARWEB_BUILD_EXAMPLES`、
  `WEARWEB_BUILD_TESTS` 和 `WEARWEB_BUILD_BENCHMARKS`。
- 将回归测试注册到 CTest。
- 扩展 computed style 解析，支持 `opacity`、`transform`、`position` 和 `z-index`。
- 添加双语 layer tree 裁剪范围文档。
- 添加 CPU `FrameBuffer`、`SoftwareRasterizer` 和 `SoftwareCompositor`。
- 添加 source-over alpha compositing，以及 opacity layers 的离屏合成。
- 添加 BMP/PPM 输出辅助函数和 `wearweb_pseudo_browser`，用于桌面完整管线验收。
- 添加基于 Windows GDI 的 UTF-8 文本栅格化，其他平台保留 tiny ASCII fallback。
- 添加 `wearweb_software_renderer_tests`。
- 添加验证页面所需的常见 CSS 解析/布局支持：四值 box edges、`rgb()/rgba()`、
  保守 `linear-gradient()` fallback、`box-sizing:border-box`、`text-align`、
  最小 flex 居中/横向布局，以及文本颜色/字号/对齐继承。
- 添加保守 text overhang padding 和基础多行文本布局，避免行尾被裁剪。
- 添加双语 software renderer 裁剪范围文档。
