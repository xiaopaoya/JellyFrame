# 引擎架构

日期：2026-06-14

JellyFrame 参考 Blink、WebKit 和 Gecko 的大体分层，但为可穿戴目标使用更小的数据结构，并明确裁剪功能边界。

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
- `HitTester`：通过 layout 和 layer geometry 将 viewport 坐标映射到 DOM event target。
- `InputController`：将平台无关 pointer/wheel input 转成类 mouse events、hover/active/focus state 和 click synthesis。
- `EventTarget`：保存 C++ listeners，并执行类 DOM 的 capture、target 和 bubble phases。

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
- Selector 支持有限但实用：compound、descendant、child、attribute 和 `:root`。
- 不支持的现代 selectors 尽量在插入 CSSOM 前跳过。
- Render object 保持紧凑的 block/inline/text 形态；layout 为常见 flex row
  和响应式 grid card 模式提供小型专用路径。
- Render tree、layout tree 和 layer tree builder 都同时提供 heap 与 `MonotonicArena`
  分配路径；嵌入式 benchmark 使用 arena 路径以减少小对象堆抖动。
- Layer tree 支持稀疏裁剪、opacity 边界、positioned stacking hints 和保守 compositing boundaries。
- Display list 只使用 rectangles、gradients 和 text。
- 文本 layout 接受 `TextMeasureProvider`；文本输出接受 `TextPainter`。core fallback 保持很小，Win32 browser 同时使用 GDI 测量和绘制。
- Event dispatch 保持平台无关；当前使用 C++ callbacks，还不是 JavaScript functions。

## 下一步专业化

1. 将 selector parsing 移入独立 `selector.*` 模块。
2. 收敛运行循环和 dirty update 契约。
3. 基于已有 dirty flags 复用 render/layout/layer 子树，避免每次 mutation 后重建完整管线。
4. 为重复 class pattern 添加 style sharing 或 computed-style cache。
5. 通过 `DomOwner` 原型和 detached-node instrumentation 评估 DOM node 分配策略。
6. 完善文本后端 adapter 和字体工作流验证。
