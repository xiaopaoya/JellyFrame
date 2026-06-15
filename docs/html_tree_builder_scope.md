# HTML Tree Builder Scope

Last checked against the WHATWG HTML Living Standard on 2026-06-13:

- Parsing model: https://html.spec.whatwg.org/multipage/parsing.html
- Tree construction: https://html.spec.whatwg.org/multipage/parsing.html#tree-construction
- Optional tags: https://html.spec.whatwg.org/multipage/syntax.html#optional-tags
- Elements: https://html.spec.whatwg.org/multipage/indices.html#elements-3

JellyFrame should build a useful DOM for modern app HTML without implementing the
full browser tree-construction machine. The goal is resilient app-runtime
parsing: common structure is preserved, unsupported structure degrades, malformed
input does not corrupt the entire tree, and pathological input is bounded.

## Keep From The Standard Shape

- Token stream input from `HtmlTokenizer`.
- A stack of open elements.
- A document root node.
- Recognition of `html`, `head` and `body`.
- Void elements that never receive children.
- Common implied end tags.
- Ignoring `DOCTYPE` for structure while never entering quirks mode.

## Phase 1: Implement Directly

- Synthesize missing `html` and `body` elements.
- Preserve explicit `html`, `head` and `body` attributes, keeping the first value
  for duplicate attributes.
- Route early metadata/resource elements into `head` when no body exists:
  `base`, `link`, `meta`, `noscript`, `script`, `style`, `template`, `title`.
- Treat metadata/resource elements after body creation as ordinary body nodes.
- Handle void elements:
  `area`, `base`, `br`, `col`, `embed`, `hr`, `img`, `input`, `link`, `meta`,
  `param`, `source`, `track`, `wbr`.
- Common implied closures:
  `p`, `li`, `dt`, `dd`, `option`, `tr`, `td`, `th`.
- Ignore unmatched end tags instead of popping unrelated ancestors.
- Collapse normal text whitespace, but preserve text inside `pre`, `script`,
  `style`, `textarea` and `title`.
- Apply hard parser limits:
  max node count, max depth and max attributes per element.

## Lazy Handling

- Unknown elements are kept as ordinary elements.
- Unknown attributes are kept until the per-element attribute limit.
- Misnested formatting elements are represented as a best-effort tree without
  adoption-agency correction.
- Table descendants are nested where they appear; no foster parenting.
- `template` is an ordinary hidden element for now, not a separate template
  contents document.
- Comments and doctypes are tokenized but ignored by DOM construction by default.

## Explicitly Dropped

- Quirks mode and limited-quirks mode.
- Full insertion-mode state machine.
- Active formatting elements list.
- Adoption agency algorithm.
- Table foster parenting.
- Frameset parsing.
- Template insertion-mode stack.
- Fragment parsing context modes.
- Parser pause/reentrancy from `document.write()`.

## Low-End Device Rules

- Every token must either advance the input or be ignored; no retry loops.
- DOM construction must be O(tokens) for supported paths.
- Builder memory must be bounded by parser options.
- On limit exhaustion, keep consuming tokens and skip new nodes instead of
  throwing or aborting.
- Debug and visualization tools must cap input size and printed output.

## Current Defaults

- `max_nodes`: 8192
- `max_depth`: 64
- `max_attributes_per_element`: 64
- `synthesize_document_structure`: true
- `collapse_whitespace`: true

