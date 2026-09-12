# Code Review — `app_runtime` services / host / worker layer

Scope: `app_compute_jobs`, `app_video_frames`, `app_font_set`, `app_services`, `host_services`, `app_host`, `app_service_worker` (`.cpp` / `.h`).
Focus: performance, implementation correctness, readability. No security review. No rewrites proposed — only minimal, local fixes.

Findings are ordered by severity. Line numbers were verified against the files as read.

---

## HIGH

### 1. Font fallback context is rebuilt from scratch on every measure/paint call
**File:** `app_font_set.cpp:279-304` (and callers `225-235`, `237-260`)
**Category:** Performance
**Severity:** High

`context_for_family()` is invoked by `measure_text()` and `paint_text()`, i.e. once per text run per frame, and it does the full rebuild every time: `refresh_context()` re-clears and re-pushes `fallback_fonts_`, then the function clears `family_fonts_` and walks `fonts_` **twice** rebuilding the fallback list (`append_unique_font(family_fonts_, &loaded.resource.font())` in two separate loops at `286-290` and `295-299`), and every `append_unique_font` call is itself a linear de-dup scan (`74-84`). On text-heavy pages (dozens of runs per frame) this is constant per-run allocation plus O(fonts²) list building for data that does not change between frames. `refresh_context()` is also called redundantly from `measure_provider()`/`painter()` (`199`, `207`) in addition to being called inside `context_for_family`.

**Fix:** Cache the resolved context per `(family_hash)` and invalidate it only in `add_jffont`/`clear`/`set_system_font` (the mutations). At minimum, drop the redundant `refresh_context()` at the top of `context_for_family` when the caller already refreshed, and merge the two `fonts_` loops into one pass that appends the family match and the fallback entries together.

### 2. `AppImageSurfaceCache::over_budget()` recomputes full O(n) counts inside the eviction loop
**File:** `app_services.cpp:1549-1552`, `1533-1547`, loop at `1677-1724`
**Category:** Performance
**Severity:** High

`over_budget()` calls `ready_surface_count()` (line 1550) and `ready_byte_count()` (line 1551), each a full traversal of `entries_` (`count_if` / range-for). It is the `while (over_budget())` condition at line 1683, so every eviction iteration re-scans the entire cache — two O(n) passes per victim, O(n·victims) overall, right in the image-heavy per-frame path. `least_recently_used_unprotected()` (another O(n) scan, line 1554) runs in the same loop.

**Fix:** Compute ready-count and ready-byte totals once before the loop and decrement them as entries are erased, or have `evict_unreferenced_with_result` call the counting helper once and loop on a local budget counter instead of re-calling `over_budget()` each iteration.

### 3. Whole pixel/frame buffers copied instead of moved on completion
**File:** `app_video_frames.cpp:203-206` and `app_services.cpp:1095-1104`, `559-566`
**Category:** Performance
**Severity:** High

These are the heaviest buffers in the product and they are duplicated on every completed job:
- `AppVideoFrameRecord{... fixture.pixels}` (line 206) copies the entire decoded frame into the record.
- `AppDecodedSurfaceRecord{... fixture.pixels}` (`app_services.cpp:1103`) copies the whole decoded surface.
- `NetworkFetchRecord{... pending->fixture.body}` (`app_services.cpp:564`) copies the whole response body (and `pending->fixture = *found` at `501` copies it a second time).

In each case the source is about to be discarded or is not needed afterward, so a full-buffer copy per frame/response is pure waste on a 512 KB device.

**Fix:** Move where the source is disposable. For `NetworkFetchRecord`, move out of the copy first: build the record from `std::move(pending->fixture.body)` before `pending_.erase(pending)` (line 569). For the video/image records whose source is a `const` fixture, either hold the payload as a `shared_ptr<const std::vector<uint8_t>>` / `std::span` view into the fixture, or make the record own a moved buffer produced once at fixture-add time.

### 4. `complete_request` copies operation + result buffer, then erases the source
**File:** `app_compute_jobs.cpp:185-190`
**Category:** Performance
**Severity:** High

`records_.push_back(AppComputeResultRecord{handle, request.app_instance_id, pending->operation, pending->result.output})` copies both the operation string and the full compute-result buffer into the record, and two lines later `pending_.erase(pending)` (line 190) destroys the original. This is a per-completed-job copy of a potentially large `std::vector<std::uint8_t>` that immediately follows the `bytes`/handle sizing work.

**Fix:** `pending` is a `const_iterator`; take a mutable iterator (or index) and `std::move(pending->operation)` / `std::move(pending->result.output)` into the record before erasing, so the buffer is transferred rather than duplicated.

---

## MEDIUM

### 5. `pending_.erase(pending)` after a `find_if` result is order-dependent on the earlier path
**File:** `app_compute_jobs.cpp:160-190`; `app_video_frames.cpp:171-209`; `app_services.cpp:1068-1107`
**Category:** Implementation
**Severity:** Medium

`complete_request` binds a `const_iterator` via `std::find_if`, then performs several `push_back`s on *other* vectors (`records_`, `streams_`, `pending_`) before `pending_.erase(pending)`. This is correct only because the iterators point into containers that those `push_back`s do not touch. It is fragile: any future insertion into the iterated container between the find and the erase silently invalidates `pending`. The compute path at `185` (`records_.push_back`) is fine, but the pattern repeats across three files and is easy to get wrong on edit.

**Fix:** Capture the index (`found - pending_.begin()`) or the `job_id`, and re-resolve/erase by index or job_id after the record insertion, rather than holding the iterator across unrelated mutations.

### 6. `source_pending()` linearly scans records, and indexes `fixtures_` unchecked
**File:** `app_video_frames.cpp:129-133`
**Category:** Performance / Implementation
**Severity:** Medium

`source_pending` builds an `any_of` over `pending_` and dereferences `fixtures_[pending.fixture_index]` for each entry (line 131). Beyond the O(pending) scan run on every `request_next_frame`, the `operator[]` assumes the stored index is still in range — if `fixtures_` were ever cleared or reordered while a pending frame exists, this is an out-of-bounds read, not a lookup miss.

**Fix:** Store the `source` string in `PendingFrame` (or validate `fixture_index < fixtures_.size()` before indexing) so the pending check does not depend on fixture-vector lifetime.

### 7. Network response body copied twice per completion
**File:** `app_services.cpp:501` and `559-566`
**Category:** Performance
**Severity:** Medium

At submit time `pending.fixture = *found` (line 501) copies the fixture including its `body`; at completion `NetworkFetchRecord{... pending->fixture.body}` (line 564) copies the body again. Two full response copies per fetch on a memory-constrained target. (Overlaps with finding 3 but the submit-side copy at 501 is separate and easily missed.)

**Fix:** Store only the matched fixture index in `PendingFetch` (as `ImageDecodeMock` already does with `fixture_index`), and copy/move the body exactly once at completion.

### 8. `AppImageSurfaceCache::find_url` is a linear scan; `resolve_or_request` makes it O(n²)
**File:** `app_services.cpp:1512-1524`, used per element in `resolve_or_request` at `1590`, and by the `*_for_url` accessors `1762-1798`
**Category:** Performance
**Severity:** Medium

`find_url` is `std::find_if` over `entries_` comparing strings. `resolve_or_request` calls it once per requested URL, so a page resolving M distinct images against an N-entry cache costs O(M·N) string comparisons. The six `*_for_url` diagnostics accessors each re-run the same scan independently.

**Fix:** Key `entries_` by URL with an `std::unordered_map<std::string, std::size_t>` index, or sort and binary-search. If the cache is genuinely small on-device, document that bound so the linear scans are intentional rather than incidental.

### 9. Audio stream URL copied instead of moved into the stream record
**File:** `app_services.cpp:1339-1346`
**Category:** Performance
**Severity:** Medium

On an `Open` completion, `AudioStreamRecord{ handle, ..., pending->url, ... }` (line 1341) copies the URL, after `pending.url = std::move(url)` already moved it once at submit (`1246`). `pending_.erase(pending)` follows at `1402`, so the copy is disposable. Also `submit_open` passes `const std::string& url` into a by-value `std::string url` parameter (`submit_command`, header `456-461`), adding a copy at the call boundary for the hot `Open` path.

**Fix:** Move `pending->url` into the record where the mutable iterator is available; take `std::string_view` for the `submit_command` url parameter for non-owning cases, or `std::string` by value and `std::move` at the call sites.

### 10. `pending_open_count()` computed twice for the same app in one submit
**File:** `app_services.cpp:1225-1227`
**Category:** Performance
**Severity:** Medium

The budget check calls `active_stream_count(host.current_app_instance_id()) + pending_open_count(host.current_app_instance_id())`, each a `count_if` over `streams_` / `pending_` (lines 1175-1186). `current_app_instance_id()` is fetched from an atomic twice, and both counts are O(n) scans of the same containers on every `Open` submit.

**Fix:** Hoist `const auto id = host.current_app_instance_id();` and compute both counts in a single pass over `pending_`/`streams_` (or cache running counts), rather than two scans plus two atomic loads.

### 11. `complete_request` copies pixel buffer, then erases the pending entry
**File:** `app_services.cpp:1082-1107`
**Category:** Performance
**Severity:** Medium

`AppDecodedSurfaceRecord{... fixture.pixels}` (line 1103) copies the whole decoded surface; the only reason it is a copy is that `fixture` is a `const ImageDecodeFixture&` and `pending` is erased right after (line 1107). Same shape as the video path (finding 3). Because the decoded surface is the single largest allocation here, this is the highest-value copy to eliminate in this file.

**Fix:** Have `ImageDecodeFixture` own its pixels via a shared handle and store a reference/`shared_ptr` in the record, or move the buffer from the fixture if the mock's lifetime allows single-consumer ownership.

---

## LOW

### 12. Diagnostics strings built unconditionally with `ostringstream`
**File:** `app_services.cpp:171-187`, `317-335`, `835-851`, `970-986`, `1800-1831`
**Category:** Performance
**Severity:** Low

`app_network_failure_detail`, `app_storage_failure_detail`, `app_image_failure_detail`, `app_audio_failure_detail` and `diagnostic_detail_for_url` each construct a `std::ostringstream` and do multi-field `<<` formatting to produce a human-readable reason string. If these are called on the failure path only (as the names imply) the cost is fine; the risk is a caller that builds them eagerly for logging even when no diagnostic sink is attached, which on this target is a heap allocation per call for a string nobody reads.

**Fix:** Gate the call sites behind a diagnostics-enabled check, or return a small struct of enum + numbers and format only at the sink. Prefer `std::string::reserve` + append over `ostringstream` if the string must be built on the hot path.

### 13. `pop_worker_request` bounds-check and `completions().full()` per request in the worker pump
**File:** `app_service_worker.cpp:26-46`
**Category:** Performance
**Severity:** Low

`pump_app_host_service_worker` calls `host.completions().full()` (line 27) on each iteration — a mutex lock plus `size_ >= capacity_` — then `pop_worker_request` (another lock) and `push_completion` (which stages and flushes under further locks). For `max_requests > 1` this is several uncontended mutex acquisitions per request. Correct, just avoidable overhead in a per-frame pump.

**Fix:** Check `full()` once before the loop for the common case, and/or expose a batched pop so the request-queue lock is taken once for the batch.

### 14. Redundant `refresh_context()` calls in the provider/painter accessors
**File:** `app_font_set.cpp:198-211`
**Category:** Readability / Performance
**Severity:** Low

`measure_provider()` (line 199) and `painter()` (line 207) each call `refresh_context()` and then hand back callbacks that call `context_for_family()` — which itself calls `refresh_context()` again (line 280). The eager refresh in the accessors is dead work for every obtain-then-use cycle.

**Fix:** Drop the `refresh_context()` calls from `measure_provider()`/`painter()` once `context_for_family()` is made cache-aware (finding 1).

### 15. `valid_handle_for_current_app` does a handle-table lookup *and* a linear stream scan
**File:** `app_services.cpp:1202-1207`
**Category:** Performance
**Severity:** Low

`host.handles().lookup_copy(audio_handle, info)` (a mutex-guarded slot probe) is followed by `find_stream(audio_handle) != nullptr` (line 1206), a second linear scan over `streams_` for the same handle. For a non-`Open` command this runs on every submit.

**Fix:** Keep a handle→index map for `streams_`, or rely on the record's own fields and drop one of the two lookups if the table already establishes ownership.

### 16. `decoded_surface_byte_count` uses raw `4` / `2` byte literals per pixel format
**File:** `app_services.cpp:661-684`
**Category:** Readability
**Severity:** Low

The switch hardcodes `multiply(stride, 4, row_bytes)` (line 664), `multiply(stride, 2, row_bytes)` (line 668) for the callers. It is correct, but the magic numbers make the format→bytes-per-pixel mapping implicit and easy to mistype when a new format is added; the `Mono1` branch also mixes two different bound checks (`std::numeric_limits<std::size_t>::max() - 7` and `...uint32_t::max() - 7U`) at `675-676` whose relationship is non-obvious.

**Fix:** Add a `bytes_per_pixel(HostPixelFormat)` helper or a `static_assert`-guarded table, and comment why the `uint32_t` cap is applied in addition to the `size_t` one.

---

## Files with no significant findings

- `app_compute_jobs.h`, `app_video_frames.h`, `app_font_set.h`, `app_host.h`, `host_services.h`, `app_service_worker.h`, `app_services.h` — interface declarations only; no logic to review. The `host_services.h` inline `contains()` (lines 175-178) is fine.
- `host_services.cpp` — the queue and handle-table operations are all Bounded (fixed `capacity_`), and the linear `find_if`/`remove_if` scans (e.g. `stage_completion` at `116`, `pop_next` at `56`, `cancel_app_instance` at `133`) are over containers capped by `max_in_flight_jobs`. No per-frame scaling issue found. `HostServiceCompletionQueue::pop` correctly `reserve`s and uses a ring buffer.
- `app_host.cpp` — thin pass-throughs over the lifecycle/queue/handle/queue objects; `options_from_capabilities`, launch/terminate, and the two `pump_frame_completions` overloads are clean. `completion_result_handle_matches_owner` (lines 9-31) does one handle lookup per push, which is bounded.
- `app_service_worker.cpp` — see finding 13; otherwise correct, and the per-slot early-out in `pump_app_host_service_workers` (lines 61-63) correctly skips null/zero-capacity slots.

---

## Summary

16 findings: 4 High, 7 Medium, 5 Low. The dominant themes in this file set are (a) full pixel/frame/response buffer copies at completion time across the video, image, network, and compute paths, and (b) per-frame recomputation of font contexts and cache budget totals that could be cached and invalidated on mutation.
