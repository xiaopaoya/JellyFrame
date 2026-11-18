# Shaped DOM to worker frame integration

2026-11-17. Local uncommitted slice on Runtime baseline c0fe1cb3, matching
Core-shaped-paint-integration-20261117. Production lock/defaults/keys unchanged.
This supersedes the DOM/worker absence statement in the earlier multiline
transport checkpoint; the earlier raw test logs remain historical evidence.

## Behavior

An explicitly selected local v11 worker now builds a normal RenderTree and calls
the authenticated session owner's `layout_document`. Core's separate immutable
LayoutResult supplies final shaped paragraphs and ordinary block geometry;
Core layer capture supplies backgrounds and exact insertion points. Runtime
copies each visible final line into the existing ordered JFSP sidecar contract.
No scalar JFTP text is generated alongside shaped geometry.

Per-line center/end placement uses final advance and content width, retaining
Q1000 coordinates and outward-rounded ink bounds. Background insertion and
line order are preserved. Generic block hit testing uses the same owned layout
and existing InputController. JS text/width changes and click callbacks rebuild
all admitted shaped geometry, then publish a new value frame. Old accepted
frames retain their original packets and exact Binding independently.

The owner wrapper always replaces any requested Binding with its authenticated
Core Binding and clamps shaping limits to its owner limits. Worker initialization
requires exact session catalog/Binding equality, supervisor validation and the
new Core layout API; missing/mismatched ownership refuses before script work.
Unsupported layout/style/budget mutations stop the worker, with no scalar or
stale-frame publication fallback. Accepted frames survive worker/session teardown
and package source-buffer release. Evaluation follows canonical supervisor
validation rules.

The bounded scope is Core's static ordinary block hierarchy with one direct
Text child per text host, one selected family/weight, auto heights and solid
backgrounds. It is still one plain root with no effects/clip/transform groups,
images or scalar text alongside JFSP. Native controls, general inline ownership,
display:contents, font fallback and general bidi are not newly supported.

A real worker regression found that `make_script_task_group_frame` had not
routed v11 into canonical capture. It now does so, and canonical validation
continues enforcing the restricted v11 root/command contract.

## Verification

- Full matched Runtime shaping-ON build: CTest 82/82.
- Full shaping-OFF build: 38 passed, three expected configuration skips.
- After final pixel/reference and traversal assertions: targeted Core layout,
  script/worker and script-task Runtime tests pass 3/3.
- Independent Core full ON45/45, OFF41/41, cropped/minimal ON44/44.

The real worker tests parse two nested text hosts into five final lines; compare
packets, baselines, per-line center/end offsets and background insertion counts
against a separately invoked Core DOM producer; compare all framebuffer pixels
with direct Core background/final-line painting and require visible glyph ink;
mutate text and precise width to change line count; dispatch a generic block
click through the actual worker inbox; verify new packets and immutable prior
frames; and replay the old frame after teardown/source release.

Negative tests cover absent/wrong Binding, unsupported style/mixed inline DOM,
aggregate source/work exhaustion, supervisor-free initialization/evaluation and
unsupported style mutation. No rejected worker publishes a scalar frame.

Logs under E:/Temp:
`runtime-shaped-dom-on-full-{build,ctest}-20261117.log`,
`runtime-shaped-dom-off-{build,ctest}-20261117.log`,
`runtime-shaped-dom-final-{build,ctest}-20261117.log`.
Core layout evidence: `docs/evaluation/2026-11-17-abi4-shaped-dom-layout.md`
in the matching Core worktree.

## Remaining gates

Later checkpoint: local authenticated installed-package selection and real-entry
IDF installed-worker compile/link are now complete; see
`2026-11-17-shaped-installed-package-selection.md`. The paragraph below records
the earlier DOM/worker checkpoint. Published-version gating and device execution
remain open.

This enables an explicit host-tested worker route, not installed-app capability
negotiation or product defaults. Next negotiate version/capability from matching
package metadata, qualify installed full/cropped packages and hosted CI, rebuild
current sources for IDF (the earlier native-only forced-link image predates this
work), then measure shaped worker peak allocation, refusal/recovery and latency
before exact-device acceptance. Existing work ledgers/storage counters do not
measure general raster/layout instruction cost or allocator/transient peaks.
Signed-device install/reboot/tamper/fallback remains separate, as do ABI9 identity
and migration, display:contents pseudo/hit/browser followup and ABI5 performance.

Last updated: 2026-11-17; Applies to: 0.6.0-dev.
