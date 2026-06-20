# 引擎架构


JellyFrame 参考 Blink、WebKit 和 Gecko 的大体分层，但为可穿戴目标使用更小的数据结构，并明确裁剪功能边界。

源码树现在拆成三个平台无关逻辑子项目：

- `src/render_core` / `jellyframe_render_core`：HTML/CSS/DOM/rendering 子集；
  不依赖 JerryScript、app 安装、文件系统、网络或 OS API。
- `src/app_runtime` / `jellyframe_app_runtime`：安装式 app 生命周期和可选 host-service
  队列；可依赖 `render_core` 的宿主能力与预算类型。
- `src/script` / `jellyframe_script`：可选 JerryScript 桥接层；嵌入式构建可以完全关闭。

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
- Selector 支持有限但实用：compound、descendant、child、attribute 和 `:root`。
- 不支持的现代 selectors 尽量在插入 CSSOM 前跳过。
- Render object 保持紧凑的 block/inline/text 形态；layout 为常见 flex row
  和响应式 grid card 模式提供小型专用路径。
- Render tree、layout tree 和 layer tree builder 都同时提供 heap 与 `MonotonicArena`
  分配路径；嵌入式 benchmark 使用 arena 路径以减少小对象堆抖动。
- Layer tree 支持稀疏裁剪、opacity 边界、positioned stacking hints 和保守 compositing boundaries。
- Display list 只使用 rectangles、gradients 和 text。
- Display invalidation 诊断可以把 dirty rectangles 映射到受影响的 layers 和 display commands，
  但 retained display-list reuse 仍延后。
- 文本 layout 接受 `TextMeasureProvider`；文本输出接受 `TextPainter`。core fallback 保持很小，Win32 browser 同时使用 GDI 测量和绘制。
- Event dispatch 保持平台无关；当前使用 C++ callbacks，还不是 JavaScript functions。

## 下一步专业化

1. 将 selector parsing 移入独立 `selector.*` 模块。
2. 收敛运行循环和 dirty update 契约。
3. 基于 dirty-region 和 display-invalidation 诊断，判断哪些 retained render/layout/layer 子树值得加入。
4. 为重复 class pattern 添加 style sharing 或 computed-style cache。
5. 通过 `DomOwner` 原型和 detached-node instrumentation 评估 DOM node 分配策略。
6. 整理本地资源包、app packaging 和发布产物。
7. 继续 allocator 工作；只有真实硬件压力证明收益足够时，再细化 tile/scanline presentation。
