# HTML Tokenizer 裁剪范围

最后对照 WHATWG HTML Living Standard 的时间：2026-06-13。

- Parsing model: https://html.spec.whatwg.org/multipage/parsing.html
- Tokenization: https://html.spec.whatwg.org/multipage/parsing.html#tokenization
- Named character references: https://html.spec.whatwg.org/multipage/named-characters.html

这个项目不打算实现完整浏览器兼容的 HTML parser。Tokenizer 应该在关键结构上
贴近标准，并且足够宽容，能吞下大部分现代 Electron 或 web-app HTML，不因为
局部不支持就丢掉后续文档。

边界放在纯浏览器兼容行为上。Quirks mode、`document.write()` 重入、
parser pause、speculative parsing 和 foreign content integration 都明确不进入
核心 runtime。

## 应该保留的标准事实

HTML parser 主要分为两个阶段：tokenization 和 tree construction。
Tokenizer 会输出这些 token 类型：

- `DOCTYPE`
- start tag
- end tag
- comment
- character
- end-of-file

对 JellyFrame 来说，重要的公开契约是稳定输出 start tag、end tag、text、
按需 comment，以及 EOF。Tree construction 可以比标准简单，但 tokenizer
应该先变得更确定。

## 第一阶段：直接实现

这些状态和行为是第一阶段处理现代 app HTML 的基线：

- Data state
- Tag open state
- End tag open state
- Markup declaration open state
- Tag name state
- Before attribute name state
- Attribute name state
- After attribute name state
- Before attribute value state
- Attribute value, double-quoted
- Attribute value, single-quoted
- Attribute value, unquoted
- After attribute value, quoted
- Self-closing start tag state
- Comment recognition，并支持可选 comment token 输出
- Bogus comment consumption
- 简单 `DOCTYPE` 识别
- `script`、`style`、`textarea` 和 `title` 的简化 raw-text 处理
- CDATA sections 按文本消费，用于容错
- Character reference state，搭配一个紧凑 named entity 表
- Numeric character reference state，支持十进制和十六进制

支持行为：

- ASCII 范围内大小写不敏感的标签名统一转小写。
- ASCII 属性名统一转小写。
- 重复属性：保留第一个属性，后续记录 parse error。静默覆盖容易隐藏 app
  作者的错误。
- 换行预处理：tokenization 前把 CRLF 和 CR 统一为 LF。
- Null character：在文本和属性值中替换为 U+FFFD。
- 相邻文本合并为一个 text token，减少小设备上的内存抖动。
- malformed `<` 序列保留为文本，而不是中断后续 tokenization。

## 第一阶段：懒处理

这些情况不应该阻塞有用的 app，但要有可预测的降级行为：

- `DOCTYPE`：输出名称，但永远不进入 quirks mode。
- Comment：默认忽略，诊断模式下可以输出。
- 未知 markup declaration：消费到 `>` 并忽略。
- Named character references：先保留紧凑表：
  `amp`、`lt`、`gt`、`quot`、`apos`、`nbsp`、`copy`、`reg`。未知引用保持
  字面量。
- `script`：按 raw text 处理直到 `</script>`。这不是完整浏览器 script-data
  状态族，但可以避免打包后的 JavaScript 破坏后续 token stream。
- `textarea` 和 `title`：先做简化 raw-text 处理。如果真实 app 需要其中的实体
  解码，再补完整 RCDATA。

## 推迟到需要时再做

这些标准状态主要服务浏览器兼容，应该延后：

- 完整 RCDATA 状态族。
- 简化 raw-text 路径之外的完整 RAWTEXT 状态族。
- `</script>` raw-text 消费之外的完整 script data 状态族。
- PLAINTEXT state。
- 完整 comment 子状态机。
- 完整 DOCTYPE public/system identifier 解析和 force-quirks 行为。
- 完整 named character reference 表。
- CDATA section 状态。
- SVG 和 MathML 的 foreign content integration。
- 由 `document.write()` 触发的 parser pause flag 和 reentrant parsing。
- speculative HTML parser。

## 第一阶段明确不做

这些能力和“小型 app runtime”的第一目标冲突：

- 浏览器兼容的 `script` tokenization。支持简化 raw-text script 处理，但不支持
  浏览器历史遗留 escape-state 兼容。
- `document.write()`。
- 解析过程中的运行时 encoding 切换。
- Quirks mode。
- SVG/MathML foreign-content parsing。
- table-specific foster parenting 行为。

## 建议实现形态

在 tree construction 前加入独立 tokenizer：

```text
HtmlTokenizer
  -> HtmlToken stream
  -> HtmlTreeBuilder
  -> DOM
```

建议核心类型：

- `HtmlTokenType`
- `HtmlToken`
- `HtmlAttribute`
- `HtmlTokenizer`
- `HtmlTokenizerOptions`
- `HtmlParseError`

Tokenizer 应该可以独立测试。这样 parser 正确性不会被 layout 或 rendering
问题掩盖。

## 最小测试语料

- 纯文本。
- 单个 start/end tag pair。
- 嵌套元素。
- 引号属性。
- 非引号属性。
- Boolean attributes。
- Self-closing syntax。
- 文本中不是合法 tag 的小于号。
- Comments 和 bogus comments。
- Numeric references：`&#65;`、`&#x41;`。
- Named references：`&amp;`、`&lt;`、未知引用字面量 fallback。
- CRLF normalization。
- Null character replacement。
