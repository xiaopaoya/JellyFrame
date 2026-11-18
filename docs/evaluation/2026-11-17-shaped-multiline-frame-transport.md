# ABI4 local shaped paragraph frame transport

2026-11-17. Runtime worktree: `Runtime-shaping-package-v1-20261116`, baseline
`c0fe1cb3`; matching local Core: `Core-shaped-paint-integration-20261117`.
Uncommitted experimental slice; production lock and defaults are unchanged.

## Implemented boundary

The local, unpublished frame v11 now transports multiple ordered JFSP final-line
records. This extends the previous single-record prototype; v9/v10 and Core's
existing public ABI4 structs keep their contracts. The frame still requires one
plain root with no transforms, clipping groups, effects, images or scalar text
alongside shaped records. It does not enable v11 for ordinary worker output.

`append_script_task_shaped_paragraph` consumes Core's immutable Paragraph. Each
visible final line supplies its own exact packet, Q1000 baseline, and ink metrics.
The helper projects ink outward with signed floor/ceil, including negative and
fractional origins. Lines without ink do not need a paint record. The caller
supplies placement and clip; this helper does not derive DOM/style geometry.
It publishes only after a complete draft passes codec validation. The draft
copies the existing value frame, so this is not a global peak-memory guarantee.

Sidecars consume the existing aggregate `max_commands`, `max_text_bytes`, and
`max_payload_bytes` budgets, plus an independent hard cap of 128 records. Decode
checks count/minimum framing before copying packets. Nondecreasing insertion
positions are required; equal positions retain vector paint order. Legacy
codecs and builds without the Binding API reject sidecars explicitly.

Accepted-frame admission validates every packet against the exact session owner
using one cumulative owner work ledger. Rendering starts with the smaller of
the owner and renderer work limits, shared across all validation, lines and dirty
regions. Validation completes before staging pixel changes; any later paint or
allocation refusal discards the staged framebuffer. Ordinary basic raster work
and allocator overhead are outside that shaping ledger.

Diff and retained replay now examine every record, including old/new clipped ink
bounds. A stable plain-root frame can select partial damage for a changed line;
count/order structure changes conservatively select a full redraw. Repainting
replays all overlapping lines/ordinary commands in order, including unchanged
translucent content. Retained replay continues its independent full-pixel check.

## Validation

Release build/test trees:

- `D:/CodexTemp/Builds/runtime-shaping-owner-on-20261117`: full CTest 81/81.
- `D:/CodexTemp/Builds/runtime-shaping-owner-off-20261117`: 38 passed, three
  configuration skips (installed CoreText/resource-owner lanes unavailable).
- After extending the edge corpus and respecting reduced owner work limits,
  the targeted ON `jellyframe_script_task_runtime_tests` passes again.

Logs: `E:/Temp/runtime-shaped-multiline-{on,off}-{build,ctest}-20261117.log`
and `E:/Temp/runtime-shaped-multiline-final-{build,ctest}-20261117.log`.

Tests exercise real three-line Paragraph production and wire roundtrip; session
admission and accepted-frame painting after teardown; reduced owner work limits;
pixel equality against direct Core full-clip painting; fractional/negative ink
projection; third-line-only damage, dirty repaint and retained replay; overlapping
translucent lines and ordinary-command insertion order; last-record corruption;
aggregate validation/paint work exhaustion; count/text/payload framing refusal;
the 128-record cap; malformed/truncated/count-tampered input; and output/pixel
preservation on refusal. Feature-OFF and legacy codecs cannot drop sidecars.

## Remaining integration

DOM Layout/style admission and authoritative shaped box geometry, ordinary worker
production, negotiated package capability, source/cache generation invalidation,
measured peak allocation/latency and exact-device execution remain open. This
host result does not supersede the earlier IDF build identity or qualify a new
device image. Production keys, Core lock and default rendering remain untouched.

Last updated: 2026-11-17; Applies to: 0.6.0-dev.
