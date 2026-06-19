# HTML Parser Architecture Notes

Last reviewed against implementation sources on 2026-06-13:

- Chromium/Blink parser sources:
  https://chromium.googlesource.com/chromium/src/+/main/third_party/blink/renderer/core/html/parser/
- WebKit HTML parser sources:
  https://github.com/WebKit/WebKit/tree/main/Source/WebCore/html/parser
- html5ever parser sources:
  https://github.com/servo/html5ever
- WHATWG parsing algorithm:
  https://html.spec.whatwg.org/multipage/parsing.html

## Structure We Follow

Modern browser engines keep parser responsibilities separated:

- token representation
- tokenizer state machine
- tree builder / token sink
- public parser orchestration

JellyFrame follows that structure:

```text
HtmlTokenizer
  -> HtmlTokenSink
  -> HtmlTreeBuilder
  -> DOM

HtmlParser = orchestration wrapper
```

`HtmlTokenizer::tokenize()` still exists for tests and diagnostics, but normal
DOM parsing uses `tokenize_to_sink()` so the runtime does not store the full
token stream before building the DOM.

## Naming Rules

- Prefix parser types with `Html`.
- Use `HtmlToken`, `HtmlAttribute`, `HtmlTokenizer`, `HtmlTokenSink`,
  `HtmlTreeBuilder` and `HtmlParser`.
- Use `open_elements_` for the tree-builder stack, matching the standard term
  "stack of open elements".
- Use `StartTag`, `EndTag`, `Text`, `Comment`, `Doctype` and `EndOfFile` for
  token categories.

## Performance Rules

- DOM parsing must stream tokenizer output into the tree builder.
- Tokenizer input should not be copied when no CR newline normalization is
  needed.
- Raw-text end-tag matching must avoid per-character temporary string
  allocation.
- Hot tag-name classification should use static `std::string_view` arrays, not
  heap-allocated containers.
- Resource limits must skip new nodes after exhaustion while continuing to
  consume tokens.

## Deliberate Differences From Browsers

- No quirks mode.
- No adoption agency algorithm.
- No table foster parenting.
- No parser pause or `document.write()` reentrancy.
- No full script-data historical escape-state compatibility.
