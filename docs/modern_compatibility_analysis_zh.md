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
- CSSOM 跳过 `@supports` 和 `:has()` 规则。
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
- CSSOM 保留 `:root` custom property declarations，但 style resolution 尚未解析
  custom properties。
- CSSOM 跳过 `@container` 和 `:is()` 规则。
- CSSOM 保留 `dialog[open]`，但当前 selector matcher 尚未应用 attribute
  selectors。
- `display: flex` 会被解析，但不是已支持 display mode。topbar 降级为 block
  flow。

影响：

- 没有 parser 层面的灾难性失败。
- 导航、卡片和 dialog 内容仍在 DOM 中。
- 主要风险是交互语义，不是解析完整性：popover/dialog 行为后续需要 event/runtime
  支持。
- 视觉降级统一但朴素：flex/container 增强被跳过，而不是半支持。

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
- 跳过条件 `@media (max-width: 480px)`。
- 跳过 `:where()` 规则。
- CSSOM 保留 `.story img`，但 style resolution 尚未支持 descendant selectors。

影响：

- 没有灾难性失败。
- 文章文本、列表项和图片节点仍可用。
- 潜在功能性视觉问题：`.story img` 的图片尺寸在实现 descendant selector matching
  前不会生效。
- 这是进入真实渲染测试前的优先缺口。

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

- Descendant selector matching，例如 `.story img`。
- Attribute selector matching，例如 `dialog[open]`。
- 基础 pseudo-class 策略，例如 `:root`、`:focus`、`:disabled`、`:checked`、
  `:open`。
- 简单 CSS custom property fallback 解析，例如 `var(--x, fallback)`。
- 更完整的 form controls、media、dialog 和 custom elements 默认 display。
- 即使暂不完整渲染，也应先存储 border、border radius、box shadow、overflow
  等 computed-style 字段。

## 建议下一步

1. 添加 selector matcher 模块，支持 compound、descendant 和简单 attribute
   selectors。
2. 添加小型 UA stylesheet，覆盖 controls 和常见 HTML 元素。
3. 添加 border、border radius、box shadow、overflow 等 computed-style 字段。
4. 添加简单 CSS variable resolution，支持直接 custom property 查找和 fallback
   参数。
5. 添加 combined document demo：解析 HTML，提取 `style` 文本和样例 CSS，构建
   CSSOM，解析样式，并输出 form、input、button 等功能节点的 computed style。

