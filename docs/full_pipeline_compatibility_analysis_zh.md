# 全流程兼容性分析

日期：2026-06-14

本文分析当前端到端管线：

```text
HTML -> DOM
CSS -> CSSOM
DOM + CSSOM -> computed style
computed style -> render tree
render tree -> layout tree
layout tree -> layer tree
layer tree -> display list
```

## 样例

- `examples/modern_cases/search_home.html` + `.css`
- `examples/modern_cases/app_shell.html` + `.css`
- `examples/modern_cases/article_cards.html` + `.css`

命令：

```powershell
.\build\Release\wearweb_pipeline_dump.exe examples\modern_cases\search_home.html examples\modern_cases\search_home.css 360
.\build\Release\wearweb_pipeline_dump.exe examples\modern_cases\app_shell.html examples\modern_cases\app_shell.css 360
.\build\Release\wearweb_pipeline_dump.exe examples\modern_cases\article_cards.html examples\modern_cases\article_cards.css 360
```

## 搜索首页

现代浏览器预期：

- `head`、`script`、metadata 和 `template` 不创建普通可视盒。
- `form`、`input` 和 `button` 创建功能性 UI 盒。
- `@supports`、`:has()`、高级颜色和 blur 增强视觉，但不是表单可用性的必要条件。

WearWeb 结果：

- `dom_nodes=21`
- `render_objects=10`
- `layout_boxes=10`
- `layers=2`
- `display_commands=14`
- Display list 包含：
  - 背景矩形
  - 搜索 form 矩形
  - input 背景和边框矩形
  - button 背景和边框矩形
  - label/button 的 text commands

评估：

- 没有灾难性失败。
- 搜索 UI 作为框、输入框和按钮保留下来。
- 缺失的 blur、`:has()` focus ring、高级颜色和圆角裁剪属于可接受降级。

## App Shell

现代浏览器预期：

- Custom elements 创建普通盒。
- `popover` 需要 runtime/event 支持。
- `dialog` 没有 `open` 时默认不渲染，除非 CSS 显式让它可见。
- `@container` 和 `:is()` 可以增强布局和 focus 样式。

WearWeb 修正后结果：

- `dom_nodes=40`
- `render_objects=29`
- `layout_boxes=29`
- `layers=1`
- `display_commands=18`
- closed `dialog` 默认被 render tree 过滤。
- header button、nav links 和 metric cards 保持可见。

评估：

- 没有灾难性失败。
- 已修复 closed dialog 误显示问题，因为现代浏览器会隐藏它。
- popover/dialog 的运行时行为尚未实现，但解析和视觉降级是统一的。

## 文章卡片

现代浏览器预期：

- 省略 `p` 和 `li` 结束标签时生成兄弟段落/列表项。
- `picture/source/img` 被保留。
- Descendant selector `.story img` 应用图片样式。
- 条件 media 和 `:where()` 增强小视口样式。

WearWeb 结果：

- `dom_nodes=22`
- `render_objects=22`
- `layout_boxes=22`
- `layers=1`
- `display_commands=8`
- 段落和列表项的隐式闭合保留。
- `.story img` 已通过 descendant selector matching 应用到 computed style/layout。

评估：

- 没有灾难性失败。
- 文章内容和图片节点保留。
- 图片尺寸已经进入 computed style 和 layout。
- 条件 media 和 `:where()` 按预期跳过。

## 面向嵌入式的观察

- Display list 仍然简单：fill rectangles 和 text commands。
- 边框被拆成四个 fill rectangles，适合没有路径光栅化器的 framebuffer 后端。
- Render tree 在 layout 前过滤非可视节点，减少 layout 工作。
- 文件型诊断工具限制输入为 512 KiB。
- DOM 和 CSS parser limits 仍然生效。

## 非致命缺口

- 尚无真实 text shaping、bidi 和完整 font fallback。
- 平台无关 input dispatch 已存在，但浏览器式加载、网络和面向框架的 DOM API
  仍刻意保持很小。
- Flex/grid 仍是子集：常用 flex row 和响应式 grid card 可用，完整 CSS 算法、
  subgrid、显式 placement 和 dense packing 延后。
- 尚无 retained layout 或 dirty rectangle invalidation。
- 已有圆角填充和便宜阴影，但没有真实 shadow blur 或高级 filter pipeline。

## 下一步功能优先级

1. Inline/local classic script loading ergonomics。
2. Dirty layer invalidation 和 rectangle repaint。
3. Arena allocation 和紧凑 DOM/layout object storage。
4. 面向非拉丁生产设备的 text shaping/font fallback 策略。
5. 只有当嵌入式应用证明需要时，再增加 selector/module 能力。
