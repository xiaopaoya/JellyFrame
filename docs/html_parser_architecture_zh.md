# HTML Parser 架构说明

最后对照实现源码的时间：2026-06-13。

- Chromium/Blink parser sources:
  https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/html/parser/
- WebKit HTML parser sources:
  https://github.com/WebKit/WebKit/tree/main/Source/WebCore/html/parser
- html5ever parser sources:
  https://github.com/servo/html5ever
- WHATWG parsing algorithm:
  https://html.spec.whatwg.org/multipage/parsing.html

## 我们采用的结构

现代浏览器内核通常会拆分 parser 职责：

- token 表示
- tokenizer 状态机
- tree builder / token sink
- 公开 parser 编排入口

JellyFrame 采用这个结构：

```text
HtmlTokenizer
  -> HtmlTokenSink
  -> HtmlTreeBuilder
  -> DOM

HtmlParser = orchestration wrapper
```

`HtmlTokenizer::tokenize()` 仍然保留给测试和诊断工具使用，但正常 DOM parsing
使用 `tokenize_to_sink()`，避免 runtime 在构建 DOM 前保存完整 token stream。

## 命名规则

- Parser 相关类型使用 `Html` 前缀。
- 使用 `HtmlToken`、`HtmlAttribute`、`HtmlTokenizer`、`HtmlTokenSink`、
  `HtmlTreeBuilder` 和 `HtmlParser`。
- Tree builder 栈命名为 `open_elements_`，对应标准里的
  "stack of open elements"。
- Token 分类使用 `StartTag`、`EndTag`、`Text`、`Comment`、`Doctype` 和
  `EndOfFile`。

## 性能规则

- DOM parsing 必须把 tokenizer 输出流式送入 tree builder。
- 如果不需要 CR 换行归一化，tokenizer 不复制输入。
- Raw-text end-tag 匹配不能在每个字符处创建临时字符串。
- 热路径标签分类使用静态 `std::string_view` 数组，不使用堆分配容器。
- 资源上限耗尽后跳过新节点，但继续消费 token。

## 和浏览器的有意差异

- 不做 quirks mode。
- 不做 adoption agency algorithm。
- 不做 table foster parenting。
- 不做 parser pause 或 `document.write()` 重入。
- 不做完整 script-data 历史 escape-state 兼容。

