# Win32 Browser natural image metadata

The Win32 Browser can use dimensions from decoded, app-owned surfaces during a
synchronous layout call. Its natural-size callback is read-only: it performs
validated handle lookup and reads the same decoded record used by painting.
Missing, pending, failed, stale or invalid resources remain unknown/invalid
according to the Core sizing contract. Decode submission stays in the bounded
host request pass and the normal layer resolver.

Accepted image completions invalidate layout because intrinsic dimensions can
move later content. A ready-to-ready `img src` change does the same. These
metadata transitions require full-frame repaint after relayout: the existing
node-local dirty bounds do not include downstream content moved by the changed
size. Other Core versions retain their previous image invalidation behavior.

Natural metadata selections retain only validated handle identities for the
current layout snapshot. This protects images used solely for layout, including
zero-width and otherwise nonpainted nodes, from cache eviction while a layout
derived from them remains active. The selection is bounded by Core's 128-query
default. A package with more distinct image sources can leave later sources
unknown when the host's bounded request or Core query budget is exhausted.

The static capture path drains at most 256 completions. After each handled
completion it reruns layout before rebuilding layers; reaching the cap emits a
diagnostic and preserves a partial result. Static capture does not promise to
finish packages that exceed this explicit cap.

Qualification used the source-override provider with the frozen Core checkout
at `82ad1dcc98394d5568d51b6665ae92e629831e55`, source manifest SHA-256
`8eb12e841028a0d56ee5eac1b4d8789f55787d784e2a046b4cc7ffc5fed0c0b8`
(163 files, Core 0.9.0-dev, ABI 4). This is
test evidence only. Runtime's default installed-package lock remains Core
0.8.0, ABI 3. The normal Browser regression script exercises six painted
images plus three zero-width metadata consumers, checks the following sibling
position after decode, checks replacement from one known source to another,
and compares normal versus full-repaint captures.

This qualification covers the synchronous Win32 Browser only. It does not
qualify worker frames, pseudo-browser geometry, firmware ports, shared-inline
Core behavior, or arbitrary image counts beyond the stated host/Core bounds.
