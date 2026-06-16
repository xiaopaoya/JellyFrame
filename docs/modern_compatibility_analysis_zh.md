# 现代 HTML/CSS 兼容性分析

日期：2026-06-13

参考资料：

- WHATWG HTML Living Standard: https://html.spec.whatwg.org/
- CSS Syntax Module Level 3: https://www.w3.org/TR/css-syntax-3/
- CSSOM: https://www.w3.org/TR/cssom-1/
- CSS Cascade: https://www.w3.org/TR/css-cascade-5/

本文用几组刻意包含现代 HTML/CSS 的样例，对比现代浏览器预期行为和当前
JellyFrame 行为。目标不是像素级兼容，而是可用性优先的合理降级：即使视觉增强被
跳过，现代页面的功能骨架仍应保留。

## 样例

- `examples/modern_cases/search_home.html`
- `examples/modern_cases/search_home.css`
- `examples/modern_cases/app_shell.html`
- `examples/modern_cases/app_shell.css`
- `examples/modern_cases/article_cards.html`
- `examples/modern_cases/article_cards.css`

生成命令：

```powershell
.\build\Debug\jellyframe_dom_dump.exe examples\modern_cases\search_home.html
.\build\Debug\jellyframe_cssom_dump.exe examples\modern_cases\search_home.css
.\build\Debug\jellyframe_dom_dump.exe examples\modern_cases\app_shell.html
.\build\Debug\jellyframe_cssom_dump.exe examples\modern_cases\app_shell.css
.\build\Debug\jellyframe_dom_dump.exe examples\modern_cases\article_cards.html
.\build\Debug\jellyframe_cssom_dump.exe examples\modern_cases\article_cards.css
```

## 案例 1：搜索首页

使用的现代特性：

- `modulepreload`
- `script type="module"`
- `form role="search"`
- `input type="search"`
- `autofocus` 等 boolean attributes
- `template`
- `@layer`
- `@supports`
- `:has()`
- `oklch()`、`color-mix()`、`backdrop-filter`
- pill radius 和 shadow 声明

现代浏览器预期行为：

- 构建正常的 `html/head/body` 树。
- module script 是脚本文本，不作为 HTML 解析。
- form 内包含 label、search input 和 submit button。
- template 的内容是 inert template contents，通过 `template.content` 暴露，
  不作为普通渲染子节点。
- CSSOM 保留 cascade layers，解析 feature query，并在支持时应用 `:has()`。

当前 JellyFrame 行为：

- DOM 保留搜索功能骨架：
  `main -> form -> label/input/button`。
- `script type="module"` 作为 raw text 保留，不破坏 DOM。
- Boolean attributes 以空值属性保留。
- `template` 暂时按普通元素带 children 解析。默认样式会隐藏它，所以不应渲染，
  但尚未实现 `template.content` 语义。
- CSSOM 展开 `@layer`。
- CSSOM 会保守求值 declaration-based `@supports` 条件，并跳过不支持或不安全的
  feature query；`:has()` 规则仍会跳过。
- fallback 框体样式保留：
  `#search.search-box` 保留 `display`、`width`、`padding`、`background`、
  `border-radius` 和 `box-shadow`。
- 不支持的 `oklch()` 不会覆盖之前支持的颜色 fallback。

影响：

- 没有灾难性失败。
- 搜索框仍然是一个包含 input 和 button 的 form。
- blur、`:has()` focus ring、高级颜色空间等视觉增强会被跳过。
- 这符合当前可用性优先策略。

## 案例 2：App Shell

使用的现代特性：

- 自定义元素 `app-root`
- `popover` 和 `popovertarget`
- `dialog`
- inline `style`
- CSS custom properties
- `display: flex`
- `@container`
- `:is()` / `:focus-within`
- attribute selector `dialog[open]`

现代浏览器预期行为：

- 未知自定义元素仍是合法元素。
- `popover` 和 `dialog` 有浏览器管理的交互行为。
- CSS variables 通过 cascade 解析。
- `display: flex` 布局 topbar。
- `@container` 在容器条件满足时应用。
- `:is()` 和 attribute selectors 参与 selector matching。

当前 JellyFrame 行为：

- DOM 保留 custom element、buttons、nav links、cards、dialog 和 form。
- Boolean `popover` 以空属性保留。
- `head` 中的 inline `style` 文本被保留。
- CSSOM 保留 `:root` custom property declarations，简单
  `var(--token)` / `var(--token, fallback)` 会通过继承 custom-property 子集解析。
- CSSOM 跳过 `@container` 规则。
- 当宿主/input controller 提供对应输入状态时，`:is()` 和 `:focus-within` 会参与
  selector matching。
- CSSOM 保留 `dialog[open]`，简单 attribute selectors 会参与 selector matching。
- `display: flex` 使用简化 flex-row 子集。完整 flex sizing 尚未实现，但普通 app bar
  和按钮行能保留可用结构。

影响：

- 没有 parser 层面的灾难性失败。
- 导航、卡片和 dialog 内容仍在 DOM 中。
- 主要风险是交互语义，不是解析完整性：popover/dialog 行为后续需要 event/runtime
  支持。
- 视觉降级保持统一：简化 flex 子集和受支持 selector functions 会生效，container
  query 增强仍作为整体跳过。

## 案例 3：文章卡片

使用的现代/常见特性：

- 省略 `p` 和 `li` 结束标签
- `picture/source/img`
- selector list `.story, article.featured`
- descendant selector `.story img`
- 条件 `@media`
- `:where()`

现代浏览器预期行为：

- 第二个 `p` 会隐式闭合第一个 `p`。
- 后续 `li` start tag 会隐式闭合前一个 `li`。
- `picture` 包含 `source` 和 `img`。
- 条件 media rules 在 viewport 匹配时应用。
- `:where()` 在支持时匹配，且 specificity 为 0。
- descendant selector `.story img` 应用图片尺寸。

当前 JellyFrame 行为：

- DOM 正确生成兄弟 `p` 元素。
- DOM 正确生成兄弟 `li` 元素。
- `picture`、`source` 和 `img` 被保留。
- CSSOM 将 `.story, article.featured` 拆分为独立 style rules。
- 当 parser viewport 匹配时，条件 `@media (max-width: 480px)` 会生效。
- `:where()` 规则会以 0 specificity 匹配。
- Descendant selector `.story img` 会通过 selector matcher 生效。

影响：

- 没有灾难性失败。
- 文章文本、列表项和图片节点仍可用。
- 小视口 margin/radius 增强现在可以通过受支持的 media query 子集生效。
- `:where()` 带来的视觉增强可以生效，但不会提高 specificity。

## 总体评估

当前 parser stack 对这些样例满足基本降级目标：

- 现代 HTML 能被 tokenized，不会丢掉后续文档。
- 功能性 DOM 节点被保留。
- Raw script/style 内容不会破坏解析。
- 不支持的 CSS enhancement blocks 会被干净跳过。
- fallback declarations 能保留。
- CSSOM 记录了足够的 cascade 诊断信息。

没有观察到灾难性 parser failure。

## 重要缺口

这些缺口在 parse 阶段不灾难，但会影响可用渲染：

- 只有当嵌入式应用证明需要时，再继续补 positioned layout、flex sizing 和 grid
  placement。

## 建议下一步

1. 继续延后复杂 `@container`、`:has()`、完整图片布局和高级效果，直到能可靠限定成本。
