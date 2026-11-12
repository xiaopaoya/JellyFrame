# Stylesheet-relative installed CSS delivery

2026-11-12. Runtime branch codex/stylesheet-resource-urls-20261112, based on
19c12c92. Core source qualification uses the supplied Core-button-text-delivery
checkout (37919b82); installed canonical package identifies source hash
8dfb676910a681543b4c2cf2711b807a7444f33c8386e6378d58e564b25eef08, ABI 4.
Production lock remains Core 0.8/ABI 3.

Installed linked stylesheets now prepare supported background URL tokens against
their own canonical stylesheet path before both snapshot extraction and normal
combine_author_css. Entry style text and attributes use the entry path. The
worker's optional synchronous document preparation callback handles attributes
in the original HTML reparsed by initialize; its context is cleared immediately
after use. Generic/static stylesheet loading retains its default behavior.
Snapshot link admission now matches Core's case-insensitive rel-token behavior.

Preparation uses lexical comment/string/declaration boundaries, not a replacement
regex or CSS AST serialization. Core validates a complete substituted declaration
before committing it. Unsupported/malformed declarations remain byte-identical,
preserving previous fallback. A supported declaration with a refused URL fails
preparation before admission/publication. Remote/scheme/protocol-relative URLs,
above-root traversal, query/fragment, percent syntax, backslashes, controls and
embedded whitespace are refused. Dot segments inside the package root normalize.
Source/output CSS and aggregate embedded preparation have a 48 KiB cap; existing
16-entry / 48 KiB resource / 96 KiB snapshot limits and deduplication remain in force.
Kind checks now precede resource reads. BMP preflight and owner publication are
unchanged.

Validation: assertion-enabled MSVC Release resource-reference/owner/four admission
combinations pass 6/6 with new Core. Locked installed Core 0.8/ABI 3 passes 5 with the
explicit canonical-owner skip. Focused clang ASan + UBSan owner/reference tests pass
2/2. Installed canonical-v10 consumer worker regression and new stylesheet owner
test pass 2/2. New tests cover nested paths, identical relative names in two sheet
directories, rel tokens, absolute URLs, original inline attributes, comments,
strings/custom-property blocks, malformed/unsupported fallback, refusal, output
expansion budgets, kind conflicts and derived resource owner replacement/lifetime.
The actual worker publishes a derived BMP background; its accepted frame paints
red pixels after worker stop. Callback refusal publishes no frame.

Evidence: E:/Experiments/stylesheet-resource-delivery-20261112. Build trees:
E:/Builds/stylesheet-resource-20261112-{new,abi3,asan,installed}.
No hardware access, push, PR or production dependency change. @import loading
and dynamic JS-authored relative style URLs remain outside this launch-time
delivery. Device-only snapshot limit boundaries were not newly exercised by a
host extraction of lifecycle.cpp; existing limits were retained, not redesigned.
