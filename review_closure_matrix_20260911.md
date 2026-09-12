# Review Closure Matrix

> Last updated: 2026-09-11; Applies to: 0.6.0-dev

This matrix reconciles the review reports with the current source tree. A
finding is not considered closed merely because a similar change exists: the
status below requires a source check and a regression or device evidence.

## Closed and Verified

| Area | Current disposition | Evidence |
| --- | --- | --- |
| Script service privilege boundary | Closed | `script_task_service_bridge.cpp` rejects privileged kinds and requires exact session/App/client-token ownership for `inputHandle`, including zero-token handles; script-task bridge tests cover rejection paths. |
| Host handle generation wraparound | Closed | `host_services.cpp` retires a slot before the 16-bit generation wraps; host service tests cover stale handles and retirement. |
| JFDP result byte bounds | Closed | `device_runtime_protocol.cpp` rejects `received_bytes > expected_bytes`; device protocol tests cover the malformed result. |
| Viewport-dependent CSS lengths | Closed | `StyleResolveContext` carries the actual viewport; layout tests cover `50vw`/`50vh` at `172x320` and the responsive matrix. |
| Timer, animation-frame and queued-XHR lifecycle | Closed | Current callback batching preserves order/cancellation; queued XHR payloads are dropped on cancellation; Debug and scripting runtime tests pass. |
| Animation invalidation lookup and subtree bounds | Closed | `animation_invalidation.cpp` builds both override and subtree-bound indexes; Core regression tests cover animation invalidation. |
| DOM statistics repeated scans | Closed | `JerryScriptRuntime` caches document statistics by document mutation generation; script runtime tests cover DOM budget accounting. |
| App/runtime completion payload copies | Closed for ephemeral sources | Compute, network and audio completion paths move consumable payloads; persistent fixtures remain copied intentionally. |
| App budget diagnostic capacity | Closed | Diagnostic capacity is derived from the enum count; the all-diagnostics regression test passes. |
| Device service batch release | Closed | Sensor and location teardown use one-pass record compaction and retain records when host release fails. |
| Image cache budget accounting | Closed | Ready-surface count and bytes are maintained incrementally; image cache eviction tests cover count and byte budgets. |
| Capability broker duplicate lookup | Closed | Requested and host capabilities use temporary `string_view` indexes while preserving request order and duplicate semantics. |
| App data directory naming | Closed for new stores | `app_registry.py` maps the UTF-8 App ID to an injective hex directory name; adversarial collision vectors are covered by the tool regression suite. Existing pre-change stores remain a migration RFC. |
| Reference endpoint durability | Closed for reference endpoint | `device_reference.py` synchronizes at the first chunk, 16 KiB boundaries and completion; `device_runtime.md` and its Chinese counterpart distinguish acknowledged bytes, persisted metadata and durable phase boundaries. Physical ports still define and test their own policy. |
| DOM mutation cache invalidation | Closed | Review confirmed tree, text, attribute, detached-node, rebind and document-destruction paths all invalidate or bypass the document-generation cache; runtime and scripting tests pass. |
| Font fallback context lifecycle | Closed | Review confirmed app-font add/clear, system-font replacement and family-context construction invalidate/rebuild safely; runtime and scripting tests pass. This does not close the separate per-query rebuild cost. |
| Dirty-rect public input bounds | Closed | Core and script-task rendering share the bounded normalizer; inputs above 128 rectangles conservatively fall back to the viewport, with direct script-renderer coverage. |
| Script-task target duplicate and clip-chain validation | Closed | Target keys are checked with a bounded hash set; clip parent depths are computed in one forward pass on encode/decode while preserving `InvalidClip` and `TooDeepClipChain` results. Script-task codec regression passes. |
| Script-task payload-writer subtraction | Closed | The writer rejects a storage state already beyond its declared capacity before subtracting, preventing an unchecked `size_t` underflow; scripting regression passes. |
| Script-task retained-replay duplicate frame diff | Closed | `eligible()` computes one `ScriptTaskFrameDiff` and reuses it for paint-skeleton and changed-region decisions; scripting CTest passes. |
| Script-task clip-chain rebuild per repaint | Closed for one render call | `render_into()` lazily caches each clip chain by clip index and reuses it across repaint rectangles; transformed source clips retain a local translated copy. Script-task renderer regression passes. |

## Still Needs Evidence or Targeted Work

| Area | Current assessment | Next action |
| --- | --- | --- |
| Dirty-rect invalidation scans | Bounded; benchmark baseline recorded | Render Core microbench (`jellyframe_render_core_microbench 80 2000`) on the 2026-09-12 Release build measured 100 fragmented rects at 63.807 us, while 500 and 1000 rect inputs take 5.342 us and 10.190 us because the configured threshold falls back to one viewport rect (`forced_merges=499/999`). No unbounded growth was observed; defer a sweep/tile rewrite until device telemetry shows invalidation is dominant. |
| DOM mutation accounting beyond the document cache | Closed | All current mutation and detached-node paths were reviewed; detached subtrees bypass the document cache, and rebind/destruction invalidate it. A dedicated repeated-mutation benchmark remains optional evidence, not a correctness blocker. |
| Font fallback context rebuild | Closed | `contexts_dirty_` is set on every font-set/system-font change; family contexts are rebuilt per query and do not retain stale internal pointers. A text-heavy benchmark remains optional evidence. |
| Physical-port durability policy | Deferred port evidence | The reference contract now documents acknowledged versus durable semantics. Each physical port must publish and power-interruption-test its own policy; this is not an A2 blocker for the existing accepted image. |
| App ID migration for pre-existing stores | Deferred RFC | There is no authoritative metadata linking a legacy sanitized directory to its original App ID. Do not auto-migrate or merge colliding legacy paths; define an explicit opt-in migration manifest before changing existing on-disk names. |
| Font fallback context per-query rebuild | Deferred benchmark/RFC | Context invalidation is correct, but family lookup can still rebuild/scan the font set on each query. Measure a text-heavy workload before introducing a cache with an explicit invalidation contract. |
| Completion buffer copies for video/image and persistent fixtures | Deferred with rationale | Compute and consumable completion paths move payloads; video/image surfaces and persistent network/image fixtures still copy by ownership design. Revisit only with a measured memory/latency workload and explicit lifetime semantics. |
| Per-layer/per-command dirty invalidation scans | Deferred benchmark/RFC | Public dirty input and coalescing are bounded, but `intersects_any` remains a linear scan. Do not introduce tiles or broad indexing until device telemetry shows invalidation is dominant. |
| Layout/style structural hot paths | Deferred benchmark/RFC | Manual `Style` comparison, repeated flex probes and remaining derived-value work are not correctness gates. Require a stable workload, output-equivalence test and benchmark before changing layout representation. |
| Script-task frame replay bookkeeping | Open assessment | Duplicate target/clip validation, duplicate frame diff and per-render clip-chain reuse are closed. Service-record lookup still needs a stable replay workload before indexing changes. |
| Script-task service completion lookup | Closed | Accepted nonzero host jobs are indexed by `host_job_id`; the index is updated with vector swap-removal, while job-zero terminal records retain the existing path. Scripting service bridge regression passes. |
| Script-task result-handle client ownership | Closed | Completion-producing compute, image, video, audio, network and storage paths propagate the request `client_token`; bridge completion ownership now requires exact App/token equality. Existing foreign-token and stale-completion regressions pass. |
| Script-task zero-byte payload lease | Closed by contract | A successful copy callback may publish an empty payload lease for a valid empty response; the nonzero lease ID distinguishes it from no payload and `byte_count` is authoritative. The integration guide documents this explicitly. |
| Script-task source clip wire width | Closed | `DisplayCommandTransform::source_clip_index` remains `uint16_t`; flattening records unrepresentable clip references instead of silently converting them to no clip, and encoding rejects the frame as `InvalidClip`. The v4 round-trip and explicit overflow-rejection regressions pass. |
| Initial Core review residuals | Deferred or open by item | Rounded-border full-box coverage, range measurement API, odd-width stroke alignment and other residuals remain separate from the completed low-risk batch. The original report is preserved unchanged; no blanket closure is claimed. |

The public dirty-rect input bound is closed separately above; that status must
not be confused with the still-open per-layer invalidation scan.

## Deferred RFC Work

- Rework rounded-border rasterization to avoid scanning the complete bounding box.
- Add a range/string-view text measurement contract for providers.
- Rework large `Style` copies and multi-pass layout only after benchmark evidence.
- Introduce broader dirty-region data structures or tile/scanline rendering only
  after device telemetry demonstrates that invalidation or bandwidth is the
  dominant cost.

## Verification Baseline

- `build/a1-runtime-check`: 13/13 CTest tests pass.
- `build/a1-scripting-check`: 15/15 CTest tests pass.
- Core regression baseline: 9/9 CTest tests pass for the full/responsive Core
  profiles; the current scripting bridge regression was rebuilt and passed.
- Tool regressions: documentation freshness 1/1 and render performance report 10/10 pass.
- Dirty-region benchmark: 100/500/1000 fragmented input baseline recorded; over-threshold inputs remain one-viewport bounded fallback.
- `git diff --check` passes.

The same benchmark run measured `text_anywhere_wrap_32/128/512/2048` at
0.496/1.460/4.581/18.909 us respectively. This remains consistent with the
bounded incremental-width path; no separate range-measurement API is justified
without a provider workload that demonstrates a material device cost.

## Baseline caveats

- The preserved report `review_findings_render_core_rest.md` contains a stale
  headline total (`24`) even though its severity sections enumerate 35 items;
  the index and this matrix use the section counts. The original review file is
  intentionally left unchanged.
- The index and several reports describe the 0.6.1 review baseline while this
  matrix tracks the 0.6.0-dev tree. A closed row therefore means “verified on
  the current source tree against that finding”, not that the historical report
  was generated from the current commit.
- `build/r0-render-core` is a stale Core-only build directory and is not part of
  the current Runtime gate; its missing targets and old protocol-vector binary
  must not be used as current regression evidence.

The original reports remain preserved as review input. This matrix is the
working status summary; individual report files are not rewritten in place.
