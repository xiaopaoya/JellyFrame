# HTML Tokenizer Scope

Last checked against the WHATWG HTML Living Standard on 2026-06-13:

- Parsing model: https://html.spec.whatwg.org/multipage/parsing.html
- Tokenization: https://html.spec.whatwg.org/multipage/parsing.html#tokenization
- Named character references: https://html.spec.whatwg.org/multipage/named-characters.html

This project is not trying to implement a full browser-compatible HTML parser.
The tokenizer should follow the shape of the standard where it matters and be
tolerant enough to consume most modern Electron or web-app HTML without failing.
Rendering may degrade, but tokenization should not easily lose the rest of the
document.

The line is drawn at compatibility-only browser behavior. Quirks mode,
`document.write()` reentrancy, parser pauses, speculative parsing and foreign
content integration are intentionally outside the core runtime.

## Standard tokenizer facts we should preserve

The HTML parser has two major phases: tokenization and tree construction. The
tokenizer emits these token kinds:

- `DOCTYPE`
- start tag
- end tag
- comment
- character
- end-of-file

For JellyFrame, the important public contract is a stream of start tags, end tags,
text, comments if enabled, and EOF. Tree construction can stay simpler than the
standard while the tokenizer becomes more deterministic.

## Phase 1: implement directly

These states and behaviors are the phase-1 baseline for modern app HTML:

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
- Comment recognition, with optional comment token emission
- Bogus comment consumption
- Simple `DOCTYPE` recognition
- Simplified raw-text handling for `script`, `style`, `textarea` and `title`
- CDATA sections consumed as text for tolerance
- Character reference state, with a compact named-entity table
- Numeric character reference state, decimal and hexadecimal

Supported behavior:

- ASCII case-insensitive tag names normalized to lowercase.
- Attributes normalized to lowercase for ASCII names.
- Duplicate attributes: keep the first attribute and report a parse error later,
  because replacing silently can hide authoring mistakes.
- Newline preprocessing: normalize CRLF and CR to LF before tokenization.
- Null character handling: replace with U+FFFD in text and attribute values.
- Emit adjacent text as coalesced text tokens to reduce memory churn on small
  devices.
- Malformed `<` sequences remain text instead of aborting tokenization.

## Phase 1: lazy handling

These cases should not block useful apps, but should degrade predictably:

- `DOCTYPE`: emit the name, never enter quirks mode.
- Comments: ignore by default, optionally emit for diagnostics.
- Unknown markup declarations: consume until `>` and ignore.
- Named character references: keep a compact table first:
  `amp`, `lt`, `gt`, `quot`, `apos`, `nbsp`, `copy`, `reg`. Unknown references
  remain literal.
- `script`: treat as raw text until `</script>`. This is intentionally not the
  full browser script-data state family, but it keeps bundled JavaScript from
  corrupting the remaining token stream.
- `textarea` and `title`: simplified raw-text handling first. Proper RCDATA can
  be added if app behavior needs entity decoding inside them.

## Defer until needed

These standard states mostly exist for browser compatibility and should wait:

- Full RCDATA state family.
- Full RAWTEXT state family beyond the simplified raw-text path.
- Full script data state family beyond raw-text consumption to `</script>`.
- PLAINTEXT state.
- Full comment sub-state machine.
- Full DOCTYPE public/system identifier parsing and force-quirks behavior.
- Full named character reference table.
- CDATA section states.
- Foreign content integration for SVG and MathML.
- Parser pause flag and reentrant parsing caused by `document.write()`.
- Speculative HTML parser.

## Explicitly out of phase 1

These features conflict with the first target of a tiny app runtime:

- Browser-compatible `script` tokenization. Simplified raw-text script handling
  is supported; browser legacy escape-state compatibility is not.
- `document.write()`.
- Runtime encoding switching during parse.
- Quirks mode.
- SVG/MathML foreign-content parsing.
- Table-specific foster parenting behavior.

## Proposed implementation shape

Add a dedicated tokenizer before tree construction:

```text
HtmlTokenizer
  -> HtmlToken stream
  -> HtmlTreeBuilder
  -> DOM
```

Suggested core types:

- `HtmlTokenType`
- `HtmlToken`
- `HtmlAttribute`
- `HtmlTokenizer`
- `HtmlTokenizerOptions`
- `HtmlParseError`

The tokenizer should be usable independently in tests. That keeps parser
correctness visible before layout or rendering becomes involved.

## Minimum test corpus

- Plain text only.
- Single start/end tag pair.
- Nested elements.
- Quoted attributes.
- Unquoted attributes.
- Boolean attributes.
- Self-closing syntax.
- Less-than signs in text that are not valid tags.
- Comments and bogus comments.
- Numeric references: `&#65;`, `&#x41;`.
- Named references: `&amp;`, `&lt;`, unknown reference literal fallback.
- CRLF normalization.
- Null character replacement.
