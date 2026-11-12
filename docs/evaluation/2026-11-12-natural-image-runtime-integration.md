# Runtime natural image metadata integration

Last updated: 2026-11-12; Applies to: 0.6.0-dev

Runtime base2d3eddec.

Canonical ScriptTaskWorkerRuntime frame9/10 layout now uses the immutable
RuntimeImageProvider already bound to its paint resolver. The integration is
guarded by JELLYFRAME_NATURAL_IMAGE_LAYOUT_API_VERSION >= 1 and the canonical
resource API. Older headers compile the existing layout path. Core owns the
pass-local memo, metadata validation and ordinary static block img admission.

The gated worker natural_image_sizing options retain Core's default bounds and
allow hosts to set limits and synchronous statistics. The worker overrides the
provider/context with its owned image snapshot. A statistics pointer must outlive
the worker and subsequent mutation-driven rebuilds. The callback only calls
RuntimeImageProvider::resolve and returns intrinsic_size. It performs no decode
and introduces no retained per-image cache. The source contract requires immutable
owned resources. Accepted frame ownership retains the same provider after worker
stop and supervisor owner replacement.

Ten focused cases run through initialize(supervisor, html, css), publish, take and
accepted-frame replay. They cover both auto axes, either authored axis, authored
aspect ratio, max-width, min-height, invalid and unknown metadata, zero query
allowance and both definite axes skipping metadata. Statistics count exactly one
layout metadata query where admitted. Source resolution counts distinguish that
query from one layer handle resolution. Replay leaves source resolution counts
unchanged. Positive cases check command dimensions and provider pixels after stop
and owner replacement. Invalid/unknown/exhausted cases check legacy empty fallback.

Release host qualification uses MSVC19.41 and the existing JerryScript libraries.
New API installed Core0.9/ABI4 identity is
97aa69552d4e5e32b4996748330b7fea7312295f307b299129638931a4c2449d.
The installed worker script suite passes. The pre-API installed ABI4 identity
8dfb676910a681543b4c2cf2711b807a7444f33c8386e6378d58e564b25eef08
and locked Core0.8/ABI3 script suites also pass. Exact Core3468c310 source
script suite also passes. Build trees are
E:/Builds/runtime-natural-image-20261112-{new,preapi,abi3,3468}.
Raw build/test logs are retained under
E:/Experiments/runtime-natural-image-20261112.

The ordinary Win32 Browser uses mutable AppImageSurfaceCache::resolve_or_request
and ImageDecodeMock through BrowserImageResolveContext. It has no corresponding
owned immutable RuntimeImageProvider binding, so its natural sizing integration
remains pending. Inline, picture, positioned, Flex/Grid and other contexts remain
bounded by Core's current admission. Device and performance qualification remain
pending for this integration.

Clang22 ASan+UBSan with assertions, R3 and Node identity enabled passes the
combined script suite and ten natural-image cases against Core7d3ae5fb. Runtime
and Jerry core/ext are instrumented, with the existing Jerry platform port
uninstrumented. Raw sanitizer logs live in the same evidence directory.

The v10 installed-consumer CI matrix retains the two pinned F5 profiles and adds
full/cropped Core93744c8f, source97aa6955/151, with R3 and Node identity enabled.
An explicit required-API configure probe prevents natural-image qualification
from passing through an older Core's skipped cases. Local required-API admission
passes on the new package and refuses pre-API source8dfb6769. The new package's
three installed checks pass with the accepted patched Jerry engine. An initial
R3/Node run linked an older local Jerry build and failed in script_runtime.
That failed log is retained, followed by the corrected engine inputs and pass.
