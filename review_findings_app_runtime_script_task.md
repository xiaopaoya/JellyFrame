# Code Review — `app_runtime` script-task subsystem

Scope: the 10 listed `.cpp`/`.h` pairs under `src/app_runtime/`. Remit: performance, implementation
correctness, readability. No security review; no rewrites proposed.

Files read in full:
`script_task_worker_inbox`, `script_task_input_codec`, `script_task_input_dispatch`,
`script_task_fatal_codec`, `script_task_frame_codec`, `script_task_frame_renderer`,
`script_task_service_request_codec`, `script_task_service_bridge`, `script_task_contract`
(`.cpp` + `.h` each).

Findings are ordered by severity. 17 findings total.

---

## 1. Duplicate detection is O(n²) and runs twice on every frame encode and decode

- `src/app_runtime/script_task_frame_codec.cpp:99-104` (`has_duplicate_target_key`), called at `:182` (encode) and `:403` (decode)
- Category: Performance
- Severity: High

The helper is a nested `for (i) for (j < i)` scan over `frame.input_targets`. It is the wrong
shape for a per-frame codec: with `max_input_targets` set to a few hundred, a full frame's
target list costs ~n²/2 comparisons, and it is paid on **both** the worker encode and the UI
decode path. Worse, this is exactly the pattern where a value that is unique by construction in
the common case is re-proven by brute force every frame:

```cpp
for (std::size_t i = 0; i < targets.size(); ++i) {
    for (std::size_t j = 0; j < i; ++j) if (targets[i].target_key == targets[j].target_key) return true;
}
```

Suggested fix: keep a reusable `std::unordered_set<std::uint32_t>` (member scratch on the
`ScriptTaskAppFramePublisher`, local on decode) with `reserve(targets.size())`; insert each key
and bail on a failed insert. O(n). If the target list is documented as small and ordered, an
alternative is a single `std::adjacent_find` after a sorted copy, but the set is simpler.

---

## 2. `diff_script_task_app_frames` is computed twice per replay-eligibility check

- `src/app_runtime/script_task_frame_renderer.cpp:334` (inside `equal_paint_skeleton`) and `:844` (call site in `eligible`)
- Category: Performance
- Severity: High

`eligible()` first calls `equal_paint_skeleton(*previous_frame_, frame)`, whose body calls
`diff_script_task_app_frames(previous, current)` and throws away everything except
`paint_structure_equal`. Then, immediately after, `eligible()` calls
`diff_script_task_app_frames(*previous_frame_, frame)` a second time to get the counts and
changed bounds. The diff walks every command, compares full `DisplayCommand` values
(including `text` string compares), and runs `transform_bounds`/`union_rect` over the changed
set — so the entire O(commands) analysis is done twice on the retained-replay path.

```cpp
if (!equal_paint_skeleton(*previous_frame_, frame)) {           // computes a full diff, discards it
    fallback_reason = ScriptTaskFrameRetainedReplayFallbackReason::PaintSkeleton;
    return false;
}
...
const ScriptTaskFrameDiff report = diff_script_task_app_frames(*previous_frame_, frame); // computes it again
```

Suggested fix: compute the report once and pass it (or its `paint_structure_equal` plus command
counts) into the skeleton test. e.g. have `equal_paint_skeleton` take the already-computed
`ScriptTaskFrameDiff`.

---

## 3. Clip-chain validation is O(clips × depth) with an unbounded scan; the decoder never bounds depth

- `src/app_runtime/script_task_frame_codec.cpp:110-125` (`validate_clip_chain`), called per clip at `:189` (encode) and `:338-342` (decode)
- Category: Implementation / Performance
- Severity: High

`encode` loops over every clip index and, for each, walks its whole parent chain:

```cpp
for (std::size_t index = 0; index < frame.clips.size(); ++index) {
    ...
    if (!validate_clip_chain(frame.clips, static_cast<std::uint32_t>(index), options.max_clip_depth)) {
        return ScriptTaskAppFrameCodecStatus::TooDeepClipChain;
    }
}
```

Two problems. (a) Cost is O(clips × max_depth) even though the chain property is monotone —
parents must be strictly lower-indexed (checked at `:186`), so a single forward pass could mark
depth in O(clips). (b) On the **decode** side the same per-index loop runs at `:338-342` and is
guarded only by `validate_clip_chain`'s own `++depth > max_depth` check inside the `while`; the
individual parent was already range-checked at `:332`, so this is not a buffer overread, but it
means a hand-crafted buffer can force `clips.size() × max_depth` steps of work before
`TooDeepClipChain` is returned — a decode amplification on the UI task.

Suggested fix: compute chain depth in one forward pass into a `std::vector<std::uint16_t>`
scratch (`depth[i] = 1 + depth[parent]`, parents guaranteed `< i`), then test
`depth[i] <= max_clip_depth` while filling. O(clips), and it removes the per-index re-walk on
both sides.

---

## 4. `normalize_dirty_rects` is super-quadratic and allocates per frame

- `src/app_runtime/script_task_frame_renderer.cpp:286-317` (merge loop at `:303-315`)
- Category: Performance
- Severity: High

For the dirty-rect path, each incoming rect is compared against every already-kept rect
(`std::any_of ... contains_rect`) and will `erase(std::remove_if(...))` from the middle of the
vector, then a `while (merged)` pass repeatedly re-scans all pairs and `normalized.erase(...)`
from the middle once per merge:

```cpp
while (merged) {
    merged = false;
    for (std::size_t left = 0; left + 1 < normalized.size() && !merged; ++left) {
        for (std::size_t right = left + 1; right < normalized.size(); ++right) {
            if (empty_rect(intersect_rect(normalized[left], normalized[right]))) continue;
            normalized[left] = union_rect(normalized[left], normalized[right]);
            normalized.erase(normalized.begin() + static_cast<std::ptrdiff_t>(right));
            merged = true;
            break;
        }
    }
}
```

Worst case this is O(k³) in the number of dirty rects (outer `while` × pair scan × each
middle `erase` shifting the tail), and the whole vector is rebuilt from scratch on every
`render_into` call. `render_into` is the per-frame present path, so this allocation + merge is
per frame.

Suggested fix: keep the normalized vector in the renderer as reuse-able scratch, and replace
the middle `erase`s with mark-and-compact (write survivors forward, then `resize`), which makes
each convergence pass O(k²) with no shifting and no reallocation.

---

## 5. Encode reads a transform's `source_clip_index` after the clip index has already been bounded away

- `src/app_runtime/script_task_frame_codec.cpp:176-178`, `:197-200`, and `:225-228`
- Category: Implementation
- Severity: Medium

`encode` validates `frame.clips.size() <= std::numeric_limits<std::uint16_t>::max()` at `:176`,
then validates every `display_clip_indices[index]` and `transform.source_clip_index` against
`frame.clips.size()` at `:197-200`, and every `input_targets[i].clip_index` at `:227`. That is
consistent — but the check at `:197-200` is done for `source_clip_index` (a `uint32_t`), while
the wire field only holds 16 bits, and the encoder writes it into two bytes at `:274-275`
without asserting the value fits. If a producer ever supplies a `source_clip_index` between
`65536` and `clips.size()-1` (only reachable if `max_clips > 65535`, which the `:176` guard
prevents) the encode would silently truncate. As written the guard saves it, but the correctness
of a 16-bit field rests on a different file's size cap, which is easy to break.

Suggested fix: assert/guard the field width where it is written, e.g. reject when
`c.transform.source_clip_index > std::numeric_limits<std::uint16_t>::max()` in `valid_command`
or just before the two-byte store, independent of `max_clips`.

---

## 6. `make_script_task_app_frame` silently saturates out-of-range clip indices

- `src/app_runtime/script_task_frame_codec.cpp:430-435`
- Category: Implementation
- Severity: Medium

When flattening clip metadata, any clip reference that does not fit `uint16_t` is rewritten to
"no clip":

```cpp
for (const std::uint32_t clip_index : flattened.display_clip_indices) {
    frame.display_clip_indices.push_back(clip_index > std::numeric_limits<std::uint16_t>::max()
                                             ? kScriptTaskNoClip
                                             : static_cast<std::uint16_t>(clip_index));
}
```

`kScriptTaskNoClip` is not a neutral sentinel — in `render_into` (`script_task_frame_renderer.cpp:750-758`)
an empty `display_clip_indices` and `kScriptTaskNoClip` both mean "no clip group", so silently
collapsing a real clip to `kScriptTaskNoClip` drops the clip and changes paint semantics instead
of surfacing a budget violation. The downstream encoder would otherwise reject the frame at
`:197`; saturating here hides the cause.

Suggested fix: do not saturate. Keep the value and let `encode_script_task_app_frame` reject it
(`InvalidClip`), or emit a diagnostic at this point so the caller knows the frame's clip metadata
exceeded the wire format.

---

## 7. Frame renderer builds diagnostic strings eagerly even with no sink attached

- `src/app_runtime/script_task_frame_renderer.cpp:655`
- Category: Performance
- Severity: Medium

```cpp
report_diagnostic(options_.diagnostics,
                  DiagnosticStage::Paint,
                  DiagnosticSeverity::Warning,
                  "script-frame-transform-budget",
                  "Value-frame command transform exceeded its temporary pixel budget",
                  std::to_string(command.rect.width) + "x" + std::to_string(command.rect.height));
```

`report_diagnostic` takes `std::string_view` and only tests `sink != nullptr` inside
(`diagnostics.h:81-90`), but the concatenation `std::to_string(...) + "x" + std::to_string(...)`
is evaluated by the caller before the call. So the heap allocation happens on the hot
transform path even when `options_.diagnostics == nullptr`, which is the default
(`ScriptTaskFrameRendererOptions::diagnostics = nullptr`). The same pattern exists in other
files in this tree (e.g. `css_parser.cpp`, `html_tokenizer.cpp`) but this one is on a
per-frame, per-transformed-command budget check.

Suggested fix: only build the detail string when a sink is present, or drop the dynamic detail
for a fixed `"command w x h"` marker and record the dimensions in the rasterizer statistics
that already exist (`SoftwareRasterizerStatistics`).

---

## 8. `collect_clip_chain` is re-run per repaint rect, rebuilding the same chains

- `src/app_runtime/script_task_frame_renderer.cpp:746-791` (call at `:759`)
- Category: Performance
- Severity: Medium

`render_into` iterates repaint rects in the outer loop and, for each, walks the display list and
re-derives the clip chain for every clip group via `collect_clip_chain(frame, clip_index, clip_chain)`
(`:759`). `collect_clip_chain` allocates a `std::vector<std::uint32_t> reversed` each call
(`:563-564`), runs a `std::find` cycle check per link, then copies clip rects into `output`
(`:580-593`). With R dirty rects and G clip groups, the same chain is rebuilt R×G times per
present, and it must be rebuilt again on the next present.

Suggested fix: hoist the per-group chain out of the repaint loop — build the chain once per
group before iterating rects, or cache chains keyed by `clip_index` in a renderer-owned scratch
`std::vector<std::vector<RasterClip>>` sized to the clip count and invalidated only when the
frame's clip records change.

---

## 9. Service bridge does an O(records) linear scan for every host completion, under the mutex

- `src/app_runtime/script_task_service_bridge.cpp:533-537`
- Category: Performance
- Severity: Medium

Inside `pump`, for each accepted host completion the bridge searches the whole `records_`
vector by `host_job_id`:

```cpp
for (const HostServiceCompletion& completion : scratch.accepted_completions) {
    const auto found = std::find_if(records_.begin(), records_.end(), [&completion](const Record& record) {
        return record.host_job_id == completion.job_id;
    });
```

This is O(completions × records) while holding `mutex_`, and `records_` is sized to
`max_requests` (documented as possibly large). The same linear scan shape appears in
`submit` (`:201-203`), `cancel` (`:441-443`), and the ledger's `track`/`cancel`/`retire`/
`consume_completion` (`script_task_contract.cpp:316, 334, 347, 378`).

Suggested fix: index `records_` by `host_job_id` with an auxiliary `std::unordered_map<std::uint32_t, std::size_t>`
maintained on push/`erase_record`. `erase_record` (`:339-342`) already does swap-remove, so the
map only needs the moved record's index updated.

---

## 10. `publish_service_request` / `post_service_request` copy the packet payload by value

- `src/app_runtime/script_task_contract.cpp:626-634` and `script_task_service_bridge.cpp:514`
- Category: Performance
- Severity: Medium

`ScriptTaskMailbox::post` takes `const ScriptTaskPacket&` and copies the payload into the slot
with `slot.payload.assign(...)` (`script_task_contract.cpp:104`). For a service request that is
fine, but the bridge's `deliver_ready_record` (`script_task_service_bridge.cpp:514`) calls
`supervisor_.post_service_completion(packet)` where `packet` was just encoded into a local
`std::vector` — the bytes are then copied again into the mailbox slot. Combined with the
`take_worker_packet` path (`script_task_contract.cpp:128`) copying the slot payload out again,
a single completion is copied at least three times (encode → slot → worker output).

Suggested fix: give `ScriptTaskMailbox` a `post(ScriptTaskPacket&&)` overload that
`slot.payload = std::move(packet.payload)` and have the bridge construct the packet in place,
so the encoded bytes move into the slot instead of being copied.

---

## 11. `ScriptTaskServicePayloadWriter::append` has an unchecked subtraction that can wrap

- `src/app_runtime/script_task_service_bridge.cpp:73-81`
- Category: Implementation
- Severity: Medium

```cpp
if (bytes == nullptr || size > capacity_ - storage_.size()) {
    return false;
}
```

The intent is "reject if appending would exceed capacity". It is only correct while
`storage_.size() <= capacity_` is an invariant. `capacity_` comes from
`ScriptTaskServiceBridgeOptions::max_service_payload_bytes` and `storage_` is the bridge's
`payload_scratch_`, which the constructor `reserve`s but does not size-cap; today the writer is
the only thing that appends, so the invariant holds. But the guard reads as a bounds check and
is in fact a subtraction that underflows (to a huge `size_t`) if the invariant is ever broken,
turning "reject" into "accept and overflow the declared budget". For a bounded-writer type this
is the one place the bound is enforced.

Suggested fix: make it explicit and unbreakable, e.g.
`if (bytes == nullptr || size > capacity_ || storage_.size() > capacity_ - size) return false;`
or assert the invariant in the constructor and store `capacity_` as the hard clamp.

---

## 12. `eligible()` computes `expanded` with a tautological double check

- `src/app_runtime/script_task_frame_renderer.cpp:907-920`
- Category: Readability
- Severity: Low

```cpp
const Rect expanded_region = union_rect(changed_region, group.rounded_clip_bounds);
if (!equal_rect(expanded_region, changed_region)) {
    expanded = expanded || !equal_rect(expanded_region, changed_region);
    changed_region = expanded_region;
}
```

The inner `expanded = expanded || !equal_rect(...)` re-tests the same condition that already
guarded the block, so it is always `true` there. The convergence loop is correct, but the
redundant comparison is confusing and invites a reader to think it is doing something with the
prior `expanded` value. Suggested fix: `expanded = true;` inside the block.

---

## 13. A nonzero result handle can publish a zero-byte payload lease

- `src/app_runtime/script_task_service_bridge.cpp:73-76` (zero-size early return) with `:384-437`
  (`prepare_completion_payload`) and `:435`
- Category: Implementation
- Severity: Low

`append` returns `true` unconditionally for `size == 0` before the capacity check, and the
`std::vector` overload (`:84-86`) forwards `bytes.data(), bytes.size()` from a possibly-empty
vector. `prepare_completion_payload` then treats the callback's `true` as a complete successful
copy and publishes `payload_scratch_` — which may be empty — as a real lease, recording
`record.completion.byte_count = static_cast<std::uint32_t>(payload_scratch_.size())` (= 0) at
`:435`. So a completion that carries a nonzero `result_handle` can end up delivered as an
accepted completion with a readable-but-empty payload lease, which the worker has no way to
distinguish from a genuine one-byte-missing result unless it independently checks `byte_count`.

Suggested fix: after a successful `payload_copy_`, treat `payload_scratch_.empty()` as
`ScriptTaskServicePayloadErrorCode::CopyFailed` (or document explicitly that a zero-byte
successful copy is legal for this handle kind).

---

## 14. `ScriptTaskServiceBridge::submit` accepts an input handle whose client token is zero

- `src/app_runtime/script_task_service_bridge.cpp:186-194`
- Category: Implementation
- Severity: Low

```cpp
if (!host_.handles().lookup_copy(input_handle, input) ||
    input.app_instance_id != session.app_instance_id ||
    (input.client_token != 0 && input.client_token != client_token)) {
```

The token match is only enforced when the handle's recorded `client_token` is nonzero. A handle
registered with `client_token == 0` (which `lookup_copy` permits) can therefore be submitted by
any `client_token` for the same app instance. The same permissive rule is used in
`completion_result_handle_is_owned` (`:350-352`). If zero is a documented "unbound handle"
sentinel this is intended; if not, it is an ownership check with a hole. Suggested fix: pin the
intent with a comment, or require an exact match and register handles with a nonzero token.

---

## 15. `ReleaseIntentMailbox::post` linear-scans the queue for duplicates

- `src/app_runtime/script_task_contract.cpp:422-427`
- Category: Performance
- Severity: Low

Every native-release intent does an O(size) scan of the (up to `capacity_`) pending intents
before enqueueing, and `discard_session` (`:448-466`) walks the whole ring twice. Under the lock
this is a small but avoidable cost on finalizer-heavy workloads. Suggested fix: if duplicate
suppression is required, keep a `std::unordered_set<ScriptTaskNativeLeaseReleaseIntent>` keyed on
`(session, native_lease_id)` alongside the ring; on `pop`/`discard_session`, erase the entry.
Otherwise document that duplicates are already idempotent and drop the scan.

---

## 16. `ScriptTaskFrameRetainedReplay` copies whole frames and framebuffers to retain state

- `src/app_runtime/script_task_frame_renderer.cpp:1013` and `:1073-1074`
- Category: Performance
- Severity: Low

`render_into` does `candidate_image_ = previous_image_;` (a full framebuffer copy) on every
candidate attempt, and `observe_presented` assigns both `previous_frame_ = frame;` (deep copy of
all command text and clip vectors) and `previous_image_ = image;`. For a WxH RGBA framebuffer
this is a multi-megabyte memcpy per present on the probe path. The class owns both buffers, so
they can be moved/swapped instead of copied where ownership allows. Suggested fix: use
`std::swap(candidate_image_, previous_image_)` after a successful replay, and have
`observe_presented` take the frame by move (as `ScriptTaskFrameDiffAccumulator::observe_presented`
already does at `:440`).

---

## 17. `ScriptTaskFramePublisher::publish` copies the encoded buffer into the lease registry

- `src/app_runtime/script_task_frame_codec.cpp:457-461`
- Category: Performance
- Severity: Low

`encode_script_task_app_frame(frame, options_, encoded_)` fills the publisher's reuse-able
`encoded_` buffer, and then `supervisor.publish_frame(session, encoded_)` copies it again into
the lease slot (`ScriptTaskFrameLeaseRegistry::publish`, `script_task_contract.cpp:232`,
`free_slot->payload.assign(payload.begin(), payload.end())`). The publisher scratch is
explicitly there to avoid per-frame growth, but the second full copy of the frame bytes remains.
Suggested fix: add a `publish_frame(session, std::vector<std::uint8_t>&& payload)` overload that
swaps the bytes into the slot, and let the publisher re-`reserve` its scratch lazily.

---

## Files with no significant findings

- `script_task_worker_inbox.cpp` / `.h` — thin dispatch; the only note is that a
  `SessionMismatch` copy failure is reported as a generic `LeaseRejected` (`worker_inbox.cpp:47-55`),
  which is acceptable.
- `script_task_fatal_codec.cpp` / `.h` — fixed 40-byte packet, every field bounds-checked
  against `kPacketBytes` before read (`:70`, `:74-82`); correct and tight.
- `script_task_service_request_codec.cpp` / `.h` — fixed 20/12-byte packets with exact-size
  checks before every read (`:64`, `:126-129`); no unchecked access.
