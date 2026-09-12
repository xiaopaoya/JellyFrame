# Code Review — `app_runtime` lifecycle / policy / broker modules

> Last updated: 2026-09-12; Applies to: 0.6.0-dev

Scope: performance, implementation correctness, readability. No security review, no buffer-overflow claims unless they are real logic bugs, no rewrite proposals.

Files reviewed in full: `app_lifecycle.*`, `app_budget.*`, `app_load_telemetry.*`, `app_host_data.*`, `app_frame_policy.*`, `app_storage_lifecycle_policy.*`, `system_events.*`, `app_installed_bundle.*`, `app_capability_broker.*`, `authorized_file_broker.*`, `app_device_services.*`.

14 findings, ordered by severity.

---

## High

### 1. Recovery report can append 13 diagnostics into a fixed 8-slot array; the dropped entries are the later terminate-level and all warn-level ones

`app_budget.cpp:99-150` (13 call sites), `app_budget.cpp:24-26`, `app_budget.h:105`

`AppBudgetRecoveryReport::kMaxDiagnostics` is `8` (`app_budget.h:105`), but `app_budget_recovery_for_snapshot` makes 13 `append_recovery_diagnostic` calls — 9 with `AppBudgetRecoveryAction::TerminateApp` (`99`, `103`, `107`, `111`, `115`, `119`, `123`, `127`, `131`) then 4 with `AppBudgetRecoveryAction::Warn` (`135`, `139`, `143`, `147`). The helper first returns early on `!meter.exhausted()` (`app_budget.cpp:15-17`), so a slot is consumed only by an exhausted meter; then it updates `report.action`/`teardown_reason` *before* the `diagnostic_count >= kMaxDiagnostics` guard (`24-26`), so the action is always correct but the 9th exhausted meter onward is silently discarded. In the worst case (all 9 terminate-level meters exhausted) the dropped entries are `ScriptTimers`, `ScriptEventListeners`, `DetachedDomNodes` plus all four warn-level ones — an operator debugging a teardown sees 8 notices that omit three of the exhausted meters. Note this is a worst-case bound, not an unconditional loss: on a realistic teardown only the meters that actually tripped consume slots, so the cap is usually not reached. Either raise `kMaxDiagnostics` to 13 (or to 9 if only terminate-level matters and the warn pass is split out), or partition the terminate and warn passes into two report sections. A `static_assert` tying the array size to the call-site count would keep this from silently regressing.

### 2. `release_app_samples` collects handles then releases them one at a time, each release re-scanning and erasing `records_` — O(n²) teardown

`app_device_services.cpp:322`, `app_device_services.cpp:330`, `app_device_services.cpp:305`

The loop `for (std::uint32_t handle : handles) { release_sample(host, handle); }` calls `release_sample`, which does its own `std::find_if(records_.begin(), records_.end(), ...)` followed by `records_.erase(found)`. For an app holding k sample records out of n total, teardown is k full scans plus k vector erases (each shifting the tail). On a device that allocates hundreds of sensor records this is quadratic at precisely the moment the system is already under memory pressure. The function already has the handles in a `std::vector`; erase by a single `std::remove_if` over `records_` (matching `app_instance_id`) and call `host.handles().release(handle)` per surviving handle, doing the table release once per element rather than a find-erase per element.

### 3. `release_client_snapshots` and `release_app_snapshots` repeat the same find-erase-per-handle O(n²) shape

`app_device_services.cpp:513`, `app_device_services.cpp:526`, `app_device_services.cpp:532`, `app_device_services.cpp:540`, `app_device_services.cpp:494`

Identical to finding 2 on the location-snapshot side: both functions build a `std::vector<std::uint32_t> handles` of matching records, then `release_snapshot(host, handle)` in a loop, and `release_snapshot` does a fresh `std::find_if` over `records_` plus `records_.erase(found)` each time. `app_device_services.cpp:513` restricts by `client_token` while `:532` matches the whole app instance, so the two can overlap — a client release followed by an app release re-scans the shrunk vector each round. Collapse both onto one `remove_if`-based pass over `records_`, releasing each handle from the host table exactly once.

### 4. `pump_completions` builds and destroys a fresh `std::vector<HostServiceCompletion>` on every frame

`app_lifecycle.cpp:78`, `app_lifecycle.cpp:79`

The 4-argument overload declares `std::vector<HostServiceCompletion> batch;` as a local and forwards it to the 5-argument overload. Every frame that calls this overload heap-allocates a vector, grows it inside `completions.pop(...)`, then frees it — a per-frame allocation in the frame pump, on a ~512 KB target where the whole point of `AppFrameScratch` is to avoid exactly this. The scratch-based 5-arg path exists and `AppRuntimeHost::pump_frame_completions(AppFrameScratch&)` uses it, but `app_lifecycle.cpp:79` keeps the allocating convenience overload live for the public API. Give `batch` the same lifetime as the other scratch members (caller-owned), or delete the 4-argument overload so the allocating path cannot be reached by accident.

### 5. `discard_app_instance` copies each surviving `AppSystemEvent` by value during ring compaction

`system_events.cpp:79`

In the compaction loop the code reads `const AppSystemEvent event = events_[(head_ + index) % capacity_];` — a full by-value copy of the event (which embeds an `AppSystemStateSnapshot`), then writes it back to its new slot. The source and destination can be the same or overlapping, so a plain `std::move` is not automatically safe, but a copy of a fat struct per surviving element on every teardown is avoidable. Where `kept == index` the value is already in place; otherwise move-assign, which is correct here since each source slot is visited once and never read again after being written.

---

## Medium

### 6. `evaluate_app_capability_requests` calls `decision_exists` inside the request loop — O(n²) over app-declared capabilities

`app_capability_broker.cpp:78`, `app_capability_broker.cpp:43`

`decision_exists(decisions, capability)` performs a linear `std::find_if` over the decisions accumulated so far, and it is invoked once per element of `requested_capabilities`. A manifest declaring n capabilities does up to n²/2 string comparisons just to de-duplicate, and the duplicate check is only needed because the output vector mirrors the input order. Since `decisions` is built in this same loop, track seen capabilities in a local `std::vector<std::string_view>` index or sort-and-unique the input once before the loop rather than rescanning the growing output.

### 7. `contains_string_view` linearly scans `host_supported_capabilities` for every requested capability — O(n·m)

`app_capability_broker.cpp:82`

The host-supported check `contains_string_view(host_supported_capabilities, capability)` runs a `std::find_if` over the host list for each requested capability (line 82), while the parallel known-capability check at line 81 is already O(1) via `kKnownCapabilities` (a compile-time array). The host-supported list is fixed for the life of the host, so the asymmetry buys nothing. Either sort `host_supported_capabilities` once and binary-search it, or — matching the known list — snapshot it into a lookup set at broker construction. `contains_string_view` also captures `needle` by value at `app_capability_broker.cpp:38`, copying the `string_view` per element for no reason.

### 8. `launch` re-derives the app id as a `std::string` after already comparing it as a `string_view`

`app_installed_bundle.cpp:48`, `app_installed_bundle.cpp:62`

Line 48 validates identity with the view form — `descriptor.summary.app_id_view() != app_id` — but line 62 then hands the same value to `host.launch(std::string(descriptor.summary.app_id_view()), ...)`, constructing a temporary `std::string` (heap allocation plus a copy of the id) purely to satisfy the by-value parameter. This is a one-shot launch rather than a per-frame path, so severity is medium, but the two lines disagree about the representation of the same value. `AppRuntimeHost::launch` takes `std::string app_id` by value and moves it into the instance, so the allocation is structural; accept a `std::string_view` at the boundary, or reuse the already-validated view to build the string once.

### 9. Bundle resource lookup revalidates every index with a full payload CRC32 before matching the path

`app_installed_bundle.cpp:98`, `app_installed_bundle.cpp:105`

`read_active_resource` calls `find_device_bundle_resource`, which loops over `descriptor.resource_count` and calls `validate_resource(reader, descriptor, index, app_path, &candidate)` per index (`device_bundle.cpp:739`). `validate_resource` reads the entry, reads the path from the string table, and then computes `crc32_for_range` over the resource *payload* before the caller's path can be matched and before the loop can stop. On a device reading from flash, resolving one resource can CRC every preceding resource's payload. `inspect_device_bundle` already validated all entries at install time, so a runtime lookup could compare the path hash first and only compute the payload CRC for the matched entry. This is the heaviest cost in the file and the one most likely to show up as a stall on first resource read. (Reuse intended by the installed-bundle docs, but the cost per lookup is the concrete issue.)

### 10. `app_host_data_filter_for_app` evaluates the location-validity predicate even when the location policy is already disabled

`app_host_data.cpp:51`

The condition `if (!policy.services.location_position || !app_host_data_valid_location(output.location))` short-circuits on the policy flag, so when location is denied the validity call is correctly skipped — but when it is *allowed* the validity check runs on every filter pass over a snapshot whose coordinates the host just produced. Minor; the ordering is already the cheap-first one, so this is low-severity, listed here only because the validity *logic* itself is the next finding.

### 11. `valid_location` is byte-for-byte duplicated across two translation units under two names

`app_device_services.cpp:45`, `app_host_data.cpp:30`

`valid_location(const AppLocationSnapshotFixture&)` and `app_host_data_valid_location(const AppLocationSummarySnapshot&)` have identical bodies: `latitude >= -90.0 && latitude <= 90.0 && longitude >= -180.0 && longitude <= 180.0`. Two copies of the same range rule will drift — a future change to what "valid" means (e.g. rejecting the null island, or handling altitude) has to be made twice and will not be. One of the two should delegate to the other, or both to a shared `app_location_coordinates_are_valid(double, double)` helper in the module's anonymous namespace.

---

## Low

### 12. Legacy `pump_completions` overload grows `accepted` without bound across frames

`app_lifecycle.cpp:90`

`accepted.reserve(accepted.size() + batch.size())` reserves against the *current* size of `accepted`, which the caller owns and may not clear between frames. The scratch path clears `accepted_completions` in `begin_frame()`, so this is not reachable from `AppRuntimeHost`, but the public 4-argument overload has no such contract. If a caller accumulates into `accepted`, the vector keeps reallocating upward instead of settling at a steady-state capacity. Document that `accepted` must be cleared per frame, or clear it here on entry.

### 13. `copy_active_descriptor` copies the entire `DeviceBundleDescriptor` by assignment

`app_installed_bundle.cpp:115`, `app_installed_bundle.cpp:118`

`descriptor = active_lease_->descriptor();` assigns the whole descriptor struct, which is a bundle of fixed-size `std::array` string buffers rather than a handful of scalars. The function's contract is to hand the caller a copy, so the copy is not gratuitous — but the zero path at line 115 duplicates the empty case that an early return would express more clearly. Low severity; flagging only so the cost is on record if this is ever called per frame.

### 14. `collect_app_budget_snapshot` calls `host.requests()` three times for two meters

`app_budget.cpp:41`

`snapshot.service_requests = AppBudgetMeter{host.requests().size() + host.requests().in_flight_size(), host.requests().capacity()};` invokes `requests()` three times, and each call returns a reference used to take a lock inside `size()`/`in_flight_size()`/`capacity()`. The meter is built from three separately-locked reads, so `used` and `limit` can come from different instants. Hoist one `const auto& requests = host.requests();` and read all three from it. Cosmetic for a diagnostics snapshot, but it removes three lock acquisitions from a function that may run per frame.

---

## Files with no significant findings

- `app_frame_policy.cpp` / `.h` — straight-line policy evaluation, no allocations, no scans.
- `app_storage_lifecycle_policy.cpp` / `.h` — the `append_diagnostic` cap is checked per call and the call count (4) is well under `kMaxDiagnostics`; the apply path delegates cleanly.
- `app_load_telemetry.cpp` / `.h` — a few conditions are recomputed into named locals rather than re-evaluated, which is the right shape; the conservative level ladder matches the documented intent and does not traverse DOM or allocate.
- `app_host_data.h`, `app_budget.h` (struct definitions) — plain data.
