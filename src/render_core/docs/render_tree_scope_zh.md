# Render Tree 裁剪范围

最后对照实现源码的时间：2026-06-13。

- Blink layout tree sources:
  https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/layout/
- WebKit rendering tree sources:
  https://github.com/WebKit/WebKit/tree/main/Source/WebCore/rendering
- Gecko frame tree sources:
  https://searchfox.org/firefox-main/source/layout/generic

WHATWG Living Standard 没有规范浏览器 render tree。这是 DOM/style resolution
和 layout 之间的实现定义层。JellyFrame 参考主流引擎形态，但保持数据模型足够小，
适合低端可穿戴设备。

## 参考形态

- Blink 围绕 `LayoutObject` 和 block、inline、text 等专用 layout 类构建
  layout tree。
- WebKit 使用以 `RenderObject`、`RenderBlock`、`RenderInline` 和 text renderer
  为核心的 rendering tree。
- Gecko 使用围绕 `nsIFrame` 子类的 frame tree。

JellyFrame 采用接近 WebKit 的命名：

```text
DOM + computed style
  -> RenderTreeBuilder
  -> RenderObject tree
  -> LayoutBox tree
  -> LayerNode tree
  -> DisplayList
```

## 已实现模型

- `RenderObjectType::View`
- `RenderObjectType::Block`
- `RenderObjectType::Inline`
- `RenderObjectType::Text`
- 每个 render object 保存：
  - 来源 DOM node 指针
  - computed `Style`
  - child render objects

## 第一阶段规则

- `display:none` 节点不创建 render object。
- `head`、`script`、`style`、`meta`、`link`、`title`、`template` 和 `noscript`
  通过默认样式排除在 render tree 外。
- 非保留上下文中的纯格式化空白文本节点会在 render tree 构建阶段被跳过，避免
  缩进换行污染 block、flex 和 grid 布局。
- Text nodes 继承父 render object 的文字颜色和字号。
- `inline-block` 暂时表示为 `RenderInline`。
- `flex` 和 `grid` 保持 block-like render object 形态，但 computed style 中保留
  原 display value。Layout 随后会对常见 flex row 和响应式 grid card 应用小型专用算法。
- Layout 消费 render tree，不再直接遍历 DOM。

## 明确延后

- Anonymous block/inline box generation。
- Pseudo-elements。
- List marker renderers。
- Replaced-element intrinsic sizing。
- 完整 CSS stacking-context 语义。
- Paint invalidation 和 retained display lists。
- Fragmentation 和 multi-column layout。
- 完整 CSS flex/grid layout algorithms，包括 named grid lines、显式 placement、
  dense packing、baseline alignment 和完整 track sizing。

## 已实现 Layout 子集

- 响应式 grid card：支持 `display:grid`、
  `grid-template-columns: repeat(auto-fit, minmax(<length>, 1fr))`、`gap`、
  `grid-auto-rows: minmax(<length>, auto)`，以及 `grid-column`/`grid-row:
  span N`。`repeat(N, 1fr)` 和 `repeat(N, minmax(0, 1fr))` 也属于该子集。
- `aspect-ratio` 会参与空媒体框/card 的 intrinsic content height 计算。
- 简化 flex row 可以使用 `column-gap`，并保留现有 justification 行为。

## 可用性策略

Render tree construction 必须保留功能 UI 盒子。现代页面可以失去 blur、动画、
高级布局和合成，但 form、input、button、image、dialog、card 等控件和结构应当
创建 render objects，并携带可用 computed styles。
