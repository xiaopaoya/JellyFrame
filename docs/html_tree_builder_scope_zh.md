# HTML Tree Builder 裁剪范围

最后对照 WHATWG HTML Living Standard 的时间：2026-06-13。

- Parsing model: https://html.spec.whatwg.org/multipage/parsing.html
- Tree construction: https://html.spec.whatwg.org/multipage/parsing.html#tree-construction
- Optional tags: https://html.spec.whatwg.org/multipage/syntax.html#optional-tags
- Elements: https://html.spec.whatwg.org/multipage/indices.html#elements-3

JellyFrame 应该为现代 app HTML 构建有用的 DOM，但不实现完整浏览器
tree-construction 机器。目标是有韧性的 app-runtime parsing：常见结构被保留，
不支持的结构可降级，坏输入不会破坏整棵树，病态输入有硬上限。

## 保留的标准形态

- 输入来自 `HtmlTokenizer` 的 token stream。
- open elements stack。
- document root node。
- 识别 `html`、`head` 和 `body`。
- void elements 永远不接收子节点。
- 常见 implied end tags。
- `DOCTYPE` 不参与结构构建，并且永远不进入 quirks mode。

## 第一阶段：直接实现

- 合成缺失的 `html` 和 `body` 元素。
- 保留显式 `html`、`head` 和 `body` 的属性，重复属性保留第一个值。
- 在 body 尚未创建时，把早期 metadata/resource 元素归入 `head`：
  `base`、`link`、`meta`、`noscript`、`script`、`style`、`template`、`title`。
- body 创建后出现的 metadata/resource 元素按普通 body 节点处理。
- 处理 void elements：
  `area`、`base`、`br`、`col`、`embed`、`hr`、`img`、`input`、`link`、`meta`、
  `param`、`source`、`track`、`wbr`。
- 常见隐式闭合：
  `p`、`li`、`dt`、`dd`、`option`、`tr`、`td`、`th`。
- 遇到不匹配的 end tag 时忽略，而不是弹出无关祖先。
- 普通文本折叠空白，但在 `pre`、`script`、`style`、`textarea` 和 `title`
  内保留文本。
- 应用硬上限：
  最大节点数、最大深度、每个元素最大属性数。

## 懒处理

- 未知元素保留为普通元素。
- 未知属性保留到单元素属性上限为止。
- 错误嵌套的格式化元素以 best-effort 树表示，不做 adoption-agency 修正。
- table 后代按出现位置嵌套，不做 foster parenting。
- `template` 暂时只是普通隐藏元素，不创建独立 template contents document。
- Comments 和 doctypes 会被 tokenizer 处理，但默认不参与 DOM construction。

## 明确弃用

- Quirks mode 和 limited-quirks mode。
- 完整 insertion-mode 状态机。
- Active formatting elements list。
- Adoption agency algorithm。
- Table foster parenting。
- Frameset parsing。
- Template insertion-mode stack。
- Fragment parsing context modes。
- 来自 `document.write()` 的 parser pause/reentrancy。

## 低性能设备规则

- 每个 token 必须推进处理或被忽略；不能出现 retry loop。
- DOM construction 在支持路径上必须是 O(tokens)。
- Builder 内存必须受 parser options 约束。
- 达到上限后继续消费 token 并跳过新节点，而不是抛异常或中止。
- 调试和可视化工具必须限制输入大小和输出量。

## 当前默认值

- `max_nodes`：8192
- `max_depth`：64
- `max_attributes_per_element`：64
- `synthesize_document_structure`：true
- `collapse_whitespace`：true

