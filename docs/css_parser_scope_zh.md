# CSS Parser 裁剪范围

最后对照 CSS Syntax Module Level 3、Media Queries 和浏览器 parser 源码结构的时间：
2026-06-16。

- CSS Syntax Module Level 3: https://www.w3.org/TR/css-syntax-3/
- CSS Syntax editor draft: https://drafts.csswg.org/css-syntax/
- CSS Conditional Rules Module Level 3: https://www.w3.org/TR/css-conditional-3/
- Media Queries Level 4: https://www.w3.org/TR/mediaqueries-4/
- Blink CSS parser sources:
  https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/css/parser/
- WebKit CSS parser sources:
  https://github.com/WebKit/WebKit/tree/main/Source/WebCore/css/parser

JellyFrame 应该能解析现代 CSS 而不发生灾难性失败，但不应该假装已经实现完整现代
cascade。Parser 接受常见语法，在 at-rule 和 declaration 边界恢复错误，并让不
支持的样式降级为统一 fallback，而不是把局部现代增强和旧布局行为混在一起。

## 视觉一致性策略

- 优先选择稳定 baseline styling，而不是局部现代 styling。
- 保留 declaration 顺序，让传统 fallback 生效：
  `color: #333; color: oklch(...);`。
- 不支持的 value 不能覆盖之前已经支持的 value。
- 不支持的增强 block 在 evaluator 存在前整体跳过。
- 坏规则不能破坏后续规则。

## 第一阶段：直接实现

- Comments。
- Qualified style rules。
- 按顶层逗号拆分 selector lists。
- Declaration blocks。
- 有序 declarations，包括重复属性。
- 解析阶段识别 `!important`。
- 在 selector 和 declaration 中处理 strings、escapes、functions 和 bracketed
  component values。
- 对 malformed declarations 和 malformed rules 做顶层错误恢复。
- 展开 `@layer` block。
- 当 prelude 为空、`all`、`screen`，或由 `screen`/`all` 加 `min-width`、
  `max-width`、`min-height`、`max-height` 条件组成时，展开 `@media` block。
  条件值支持 `px` 和无单位 px-like 数字；逗号分隔的 media list 按“任一匹配”处理，
  使用 parser options 中的紧凑 viewport 进行一次性判断。
- CSS custom property declarations，以及 style resolution 阶段的
  `var(--token)` / `var(--token, fallback)` 子集。Token 沿 DOM 路径继承，
  `:root` token 和 inline token 可用；无法解析的值不会覆盖之前的 fallback
  declaration。
- Type/class/id/attribute compound selectors、descendant 和 child combinators，
  以及 adjacent/general sibling combinators。
- 面向嵌入式应用子集的 UI 属性声明，包括 `aspect-ratio`、`gap`、
  物理 `margin-*`/`padding-*`/`border-*-width` longhands、`column-gap`、
  `row-gap`、带 `minmax()` 最小轨道的 `grid-template-columns`、带最小轨道的
  `grid-auto-rows`，以及
  `grid-column`/`grid-row: span N`。

## 第一阶段：懒处理

- `@supports`、不支持或复杂的 `@media`、`@container`、`@scope`：整体跳过 block。
- `@font-face`、`@keyframes`、`@page`、`@property`：识别 balanced block
  边界，但暂不暴露给 style resolution。
- 未知 at-rules：跳过 statement 或 balanced block。
- 暂时整体跳过 `:has()`、`:is()`、`:where()`、`::part()`、`::slotted()` 等
  不支持 selector 的完整规则。

## 明确尚未实现

- 完整 CSS token stream objects。
- 完整 selector parser。
- Cascade layers 和 layer ordering。
- 完整 media query evaluation，包括 `not`、range syntax、width/height 之外的
  media features，以及解析后随 viewport 变化动态重算。
- Feature query evaluation。
- 完整 custom property dependency graph、区分大小写的 custom property 名称、
  超出有界递归保护的循环处理，以及完整 invalid-at-computed-value-time 语义。
- CSS nesting semantics。
- Shadow DOM selectors。
- Animation/keyframe model。
- 完整 grid value grammar、named lines、显式 placement 和 dense packing。
- Container query evaluation。该能力有价值，但会引入 style/layout 反馈环；
  在能可靠限制循环前刻意延后。

## 当前 parser 限制

- `max_rules`：4096
- `max_declarations_per_rule`：256
- `max_nesting_depth`：8
- `flatten_layer_blocks`：true
- `parse_plain_media_blocks`：true
- `media_viewport_width`：360
- `media_viewport_height`：240
