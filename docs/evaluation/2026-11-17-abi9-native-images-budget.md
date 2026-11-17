# Native ABI9 image ownership, checked paint and layer admission

Last updated: 2026-11-17; Applies to: 0.6.0-dev.

## Result

The desktop scalar Core route now consumes ABI9's existing borrowed natural
image sizing policy, completes bounded mock image work before publishing text
layout/image proofs, and installs Core's fractional image sampler. Full and
cropped installed ABI9 each pass two targeted CTests: actual native rendering
and checked-paint/layer-budget admission. ABI4 compatibility retains its prior
path and passes native, CLI, visual diagnostics and app-shell workflows (5/5
including package staging; the existing CLI symlink privilege skip remains).

The unchanged dense600 fixture now renders all600 commands and still reports
density/scroll diagnostics. Fixed24x16 and auto-height24x16 inline images each
contain384 actual green pixels; natural12x8 contains96. Parent clipping to10px
contains160 pixels. Capture-frame startup matches single capture. A real click
replaces a ready image with another ready resource of a different aspect ratio;
the first image changes width from24 to8 at fixed16px height and the subsequent
frame remains stable. Missing required images return failure without a capture.

These are native scalar image integration results. The authenticated contextual
Desktop route is still absent and package-selected `textPipeline` stays refused.
No contextual geometry is mixed with scalar JFTP text paint or caches.

## Implementation and ownership

ABI4 exposes a value policy/callback returning ImageIntrinsicSize. ABI9 exposes
a borrowed NaturalImageSizingOptions pointer and bool/out-parameter callback.
The native adapter distinguishes those signatures, keeps the borrowed policy
and selected image identities alive throughout layout/layer construction, and
installs completion/attribute-change relayout for both APIs. Metadata callbacks
only inspect ready surfaces; they never decode, mutate DOM or pump completions.

ABI9 cannot publish an inline image proof before required natural metadata is
ready, and layer emission requires the selected surface. Desktop therefore
requests at most128 source images and completes at most128 mock image jobs
before taking layout/style snapshots. Interactive completions use the normal
host handler, including script notification, and stop on callback failure or
document/recovery replacement. This is bounded host mock scheduling, not an
ESP decoder or general asynchronous product loading-state contract. Cache/job
limits remain in effect and excess/missing resources can still refuse.

The prepared inline Image command carries fractional sampling data even when
its dimensions are integral. The old native painter omitted the required
paint_fractional_clipped callback. Consequently a nonempty framebuffer could
be returned with text but no image. The new callback validates storage and the
whole request using Core's public prepare_fractional_image_sampling, then uses
Core's allocation-free sampler/filter within the admitted clip. It supports
validated RGB565/RGBA surfaces and Canvas pixels; no independent sampling math
or temporary image copy is introduced.

Native ABI9 full/partial painting now checks render_into_checked results. A
failed full frame cannot become a successful capture; a failed partial paint
throws before publication. Interactive error recovery clears the framebuffer
so later success must rebuild a complete frame rather than reuse failed damage.
The native admission C++ test proves missing callbacks refuse, an installed
callback paints, and framebuffer-budget overflow refuses. It does not qualify
every interactive failure/recovery path or VM task restart.

## Desktop layer budget

Core's frozen scalar admission conservatively reserves four potential layers
per layout box plus a root. Native defaults previously allowed4096 layout boxes
but only1024 layers, so600 empty elements were refused before actual layering.
The shared desktop-only helper sets max_layers to at least4*max_layout_boxes+1
(16385 at default) with checked arithmetic. This is a finite admission ceiling,
not eager allocation. Display-command limits remain8192; embedded HostBudgets
remain1024 layers. The C++ regression checks unchanged embedded defaults and
overflow rejection; the actual600-node regression checks no node truncation.

## Remaining product gates and correction to the preceding report

The earlier native-default report attributed the SVG preflight to an inline
image proof. Source inspection now establishes that the actual fixture uses
position:absolute, left/top16px and definite32px axes. Frozen Core explicitly
refuses positioned img in CoreTextFrame::admit. The consumer fixes above cannot
remove that Core support boundary. Neither fixture nor assertions were changed.
Pseudo-browser image decoding/preflight and positioned-image support remain
open. The earlier original logs and report are retained as historical evidence;
this report supersedes that diagnosis.

After the dense budget fix, the original visual diagnostics script progresses
past dense/scroll/timer cases and fails at its responsive fixture with Core
status6/UnsupportedText. The full CLI still fails at positioned SVG preflight;
actual sample_launcher still warns grid-track-sizing-fallback with
unsupported-collapsing-block-margins. Those three wider product tests remain
failed. This slice does not suppress warnings, narrow fixtures or mark them
accepted. Core support improvements require a separately identified candidate;
the ABI9 package used here stays frozen.

Next: characterize responsive scalar admission and grid cross-size margin
requirements; add positioned/replaced support and pseudo preflight through
explicit bounded Core contracts; then connect contextual Desktop end to end
with selected-family/fallback ownership. Physical signed installation,
partition migration, target reserve/latency and VM recovery remain separate.

## Identity and evidence

Runtime parent e45cb361. Frozen Core commit
f35f1f778119f360d508b69b0c7a0eaeafae6cb4, source640 files/hash
997ace75be360ad2f5c3afcac3f21eb92bf3e605b670bb00fac23d7e75beed61.
ABI9 full/cropped use installed public headers/imported archives, exact0.6.2/ABI9
qualification lock. ABI4 control is source override0.9.0/ABI4 with hash
7c9dc031519c58577c567e04b72b639b43267d3e68d978ae1f948a9e9292398a.
It is a source compatibility control, not a released-package requalification.

Evidence: E:/Experiments/migration-native-images-budgets-20261117.
audit.py/complete-result.json verify source agreement, installed provenance,
profile cropping, assertion-enabled C++ checks and executable identities.
verified-full/cropped/abi4 retain actual pages, images, traces and refusals;
qualified-frozen retains binaries/build/provenance. Earlier failed probes remain
separate. Full-product-remaining.log and the responsive traceback preserve
the outstanding wider failures. No performance or target-memory claim follows
from these host functional results. Core, production lock, Runtime libraries,
port/defaults/keys, fixtures and device contents remain unchanged.

Parallel read-only delegation was attempted twice; task text was unavailable
to the child agents and no delegated findings or implementation are counted.
