# render_core Review — Remaining Modules (excluding 12 already-reviewed files)

Scope: all `.cpp` under `src/render_core/` except the 12 files already reviewed
(html_tokenizer, html_tree_builder, dom, style, css_parser, text_backend, text_scan,
text_layout_reuse, layer_tree, render_tree, software_renderer, raster_primitives.h).

Remit: performance, implementation correctness, readability. No security review, no rewrites.

Files read for this report: `layout.cpp`, `layout.h`, `flex_grid_paint.cpp`, `modern_paint.cpp`,
`frame_loop.cpp`, `frame_update.cpp`, `scroll_blit.cpp`, `text_repaint.cpp`, `style_repaint.cpp`,
`display_invalidation.cpp`, `dirty_region.cpp`, `animation_invalidation.cpp`, `animation_timeline.cpp`,
`pipeline_statistics.cpp`, `arena.cpp`, `arena.h`, `bitmap_font.cpp`, `bitmap_font_resource.cpp`,
`text_normalization.cpp`, `text_adapter.cpp`, `hit_test.cpp`, `input.cpp`, `event.cpp`,
`document_style.cpp`, `document_script.cpp`, `dom_owner.cpp`, `canvas2d.cpp`, `canvas2d_disabled.cpp`,
`form_control.cpp`, `form_submission.cpp`, `form_submission_disabled.cpp`, `embedded_framebuffer.cpp`,
`html_parser.cpp`. Also read for context: `project_docs/perf_notes.md`.

Total findings: 24.

---

## HIGH severity

### H1. Rounded-rect gradient fills compute subpixel circle coverage for every pixel of the whole box

- **File**: `src/render_core/modern_paint.cpp:87`, `modern_paint.cpp:99`, `modern_paint.cpp:114`
- **Category**: Performance
- **Severity**: High

Inside `modern_paint_fill_linear_gradient`, all three direction branches (vertical `87`, horizontal `99`,
diagonal `114`) run a full `for (y …) for (x …)` sweep of the clipped rectangle and call
`const int coverage = rounded_rect_coverage(rounded, x, y);` for every pixel. The interior of a rounded
rectangle is provably 255 and only the four `radius x radius` corner squares ever need the 4x4-subsample
math. The fast path is unavailable here because `modern_paint_fill_opaque_linear_gradient_fast` bails out
when `has_corner_radius(border_radius)` (`modern_paint.cpp:20`), so *every* rounded gradient lands on the
per-pixel path. The vertical branch already hoists the row color out of the x loop yet still recomputes
coverage per pixel. A 200x120 card costs ~24,000 subsampled circle tests where ~4x `r*r` would do.

**Fix**: split each loop into three x-spans — `[left, left+r)`, `[left+r, right-r)` (coverage = 255,
straight blend), `[right-r, right)` — and only call `rounded_rect_coverage` in the two corner spans, plus
the top/bottom row bands.

### H2. Conic gradient fill recomputes rounded-rect coverage per pixel

- **File**: `src/render_core/modern_paint.cpp:145`
- **Category**: Performance
- **Severity**: High

`modern_paint_fill_conic_gradient_region` has no interior shortcut at all; the body is a bare
`for (y) for (x) { … const int coverage = rounded_rect_coverage(rounded, x, y); … }` over the full
bounding box. Unlike the linear-gradient function there is not even a fast path to fall back from, so a
rounded conic fill pays the subsample cost across the entire area.

**Fix**: same corner-span decomposition as H1; hoist a `row_is_interior` check so the whole row skips
coverage when `y >= top + r && y < bottom - r` and the column is interior.

### H3. Radial gradient fill recomputes rounded-rect coverage per pixel

- **File**: `src/render_core/modern_paint.cpp:184`
- **Category**: Performance
- **Severity**: High

`modern_paint_fill_radial_gradient_region` is the same shape as H2: `rounded_rect_coverage(rounded, x, y)`
at `modern_paint.cpp:184` inside a full-bounds `for (y) for (x)` sweep. Note the file already contains the
correct model to copy: `modern_paint_fill_soft_box_shadow` (`modern_paint.cpp:199`, separable center-span
fast fill at lines `304-311`) proves the codebase knows how to do this.

**Fix**: mirror the `modern_paint_fill_soft_box_shadow` approach — compute the corner bands separately and
fill the interior span directly.

### H4. Text is re-wrapped and thrown away just to count lines, then re-wrapped again at paint

- **File**: `src/render_core/layout.cpp:1048`, `layout.cpp:1081`, `layout.cpp:1088`, `layout.cpp:1095`
- **Category**: Performance
- **Severity**: High

`layout_text_box` normalizes/transforms the node's text into a fresh `std::string text` at
`layout.cpp:1048` (`transformed_render_text(*box.node, …)`), measures it, then calls
`wrap_text_anywhere(text_measure_, …)` (`1081`) or `wrap_text_at_opportunities(…)` (`1088`). Both return a
`std::vector<std::string> lines` — a heap vector of heap strings — **used only** to take `.size()` for
`line_count` at `1095`: `clamp_layout_value(… std::min<std::size_t>(…, lines.size()))`. The wrapped lines
are immediately discarded, and the paint pass re-normalizes and re-wraps the same node from scratch. On a
text-heavy layout this is the single most expensive redundant computation in the file.

**Fix**: add a counting-only variant (`wrap_text_*_count`) that advances the same cursor without
materializing substrings and returns the line count, or have `layout_text_box` cache the produced lines on
the `LayoutBox` for the paint pass to reuse.

### H5. `layout_fields_equal` is a hand-maintained field-by-field Style comparison

- **File**: `src/render_core/style_repaint.cpp:21`, `style_repaint.cpp:61`
- **Category**: Implementation
- **Severity**: High

`layout_fields_equal(const Style& left, const Style& right)` (`style_repaint.cpp:21`) compares roughly 90
`Style` fields written out by hand, and `Style::position` is compared as a `std::string`
(`left.position == right.position`, `style_repaint.cpp:61`). Any newly added layout-affecting `Style` field
that the author forgets to add here silently reports "layout unchanged" (`style_repaint.cpp:118`) and lets
the engine reuse a stale layout — a latent correctness bug that will not fail loudly. A hand list is also a
standing per-edit hazard on a file that is touched whenever `Style` grows.

**Fix**: at minimum add a comment block at the top of `layout_fields_equal` demanding new fields be added
here, and consider grouping the layout-affecting subset into a nested `LayoutStyleKey` struct compared with
a single `operator==` so the compiler flags an omission via aggregate-init arity.

### H6. Flex non-wrap layout runs up to three full `layout_box` passes per child, re-measuring text each time

- **File**: `src/render_core/layout.cpp:1297`, `layout.cpp:1370`, `layout.cpp:1444`, `layout.cpp:1476`
- **Category**: Performance
- **Severity**: High

The column flex path probes with `layout_child_for_size(child, probe_height, force_height)`
(`layout.cpp:1297`), then re-lays out the child again if the target differs from the probe
(`layout.cpp:1370`), then the row path does the same probe/re-layout at `layout.cpp:1444` and
`layout.cpp:1476`, and Stretch adds a further pass. Each pass descends into the child subtree and re-runs
`layout_text_box` (and therefore H4). A flex row of text-bearing boxes pays the full text pipeline three
times per child per layout.

**Fix**: cache the measured intrinsic height on the child between the probe and the final pass and reuse it
when only the cross-axis size changed, skipping the redundant `layout_box` descent.

---

## MEDIUM severity

### M1. `is_out_of_flow_positioned` compares `std::string` in the hottest per-box predicates

- **File**: `src/render_core/layout.cpp:196`
- **Category**: Performance
- **Severity**: Medium

```cpp
bool is_out_of_flow_positioned(const Style& style) {
    return style.position == "absolute" || style.position == "fixed";
}
```

`Style::position` is a `std::string` (see `style.h`), so this allocates-and-strcmps on every invocation, and
the predicate is called inside the per-child loops at `layout.cpp:414`, `422`, `481`, `761`, `966`, `1153`,
`1193`, `1240`, `1248`, `1535`, `1584` — i.e. at least once per box per layout pass, plus again in
`layout_positioned_children`. The sibling check `style.position != "relative"` at `style_repaint.cpp`-adjacent
sites has the same cost.

**Fix**: store `position` as a small enum on `Style` (parsed once at style time) and compare the enum here.

### M2. `ordered_flex_paint_children` disagrees with layout ordering and lacks the out-of-flow filter

- **File**: `src/render_core/flex_grid_paint.cpp:13`, `flex_grid_paint.cpp:21`
- **Category**: Implementation
- **Severity**: Medium

Layout orders children with `ordered_flex_children`, which filters out-of-flow children via
`is_out_of_flow_positioned` (see `layout.cpp:414`, `layout.cpp:422`). The paint-side
`ordered_flex_paint_children` (`flex_grid_paint.cpp:9`) does not: its `has_nonzero_order` scan
(`flex_grid_paint.cpp:13`) and its push loop (`flex_grid_paint.cpp:21`) include *all* children, including
absolutely-positioned ones. Once any `flex_order` is non-zero the paint stacking order can diverge from the
layout order. It also allocates a fresh vector per call even when the caller will discard it.

**Fix**: apply the same `!is_out_of_flow_positioned(child->style)` filter in both the `any_of` scan and the
push loop so paint order matches layout order.

### M3. Dirty-rect coalescing does an O(n^3)-ish pairwise best-pair search per merge round

- **File**: `src/render_core/dirty_region.cpp:574`, `dirty_region.cpp:599`
- **Category**: Performance
- **Severity**: Medium

`coalesce_dirty_rects_into` (`dirty_region.cpp:546`) repeatedly searches for the best pair to merge with a
nested scan (`dirty_region.cpp:599` computes `extra_area <= percent_of_area(pair_area, …) && merged_cost <
pair_cost` inside a double loop) and restarts after each merge, giving roughly O(rects^3) behavior. It is
bounded by `kMaxPairwiseMergeRects = 128` (`dirty_region.cpp:15`, guarded at `574`), so the worst case is
capped, but pathological frames still pay a large constant.

**Fix**: sort rects by one axis and only test near-neighbors for merge candidates, or cap the number of
merge rounds rather than re-scanning to convergence.

### M4. `append_dirty_bounds_from_layout` computes full subtree bounds per dirty node

- **File**: `src/render_core/dirty_region.cpp:287`, `dirty_region.cpp:178`
- **Category**: Performance
- **Severity**: Medium

At `dirty_region.cpp:287` the code calls `merge_dirty_bounds(output, current->node, subtree_bounds(*current))`
for every dirty node, and `subtree_bounds` (`dirty_region.cpp:178`) walks the whole subtree — allocating a
`std::vector<const LayoutBox*>` worklist each time. For a frame where a large container is dirty this
re-walks the same descendants repeatedly. Called for both previous and current layout at
`dirty_region.cpp:676` and `677`.

**Fix**: compute subtree bounds bottom-up once per layout pass (or memoize on `LayoutBox`) instead of
recomputing per dirty node.

### M5. `intersects_any` is a linear scan run per layer and per display command

- **File**: `src/render_core/display_invalidation.cpp:25`, `display_invalidation.cpp:141`, `display_invalidation.cpp:155`
- **Category**: Performance
- **Severity**: Medium

`intersects_any(rect, dirty_rects, dirty_rect_count)` (`display_invalidation.cpp:25`) loops over every dirty
rect. It is called for layer bounds (`display_invalidation.cpp:141`) and then for every display command in
the layer (`display_invalidation.cpp:155`), producing O(layers x commands x dirty_rects) work per frame.
With a fragmented dirty set this dominates the invalidation pass.

**Fix**: build a small bounding box of all dirty rects and reject quickly, or sort dirty rects and use a
binary search / interval sweep.

### M6. `form_control_kind` lowercases a fresh `std::string` on every call

- **File**: `src/render_core/form_control.cpp:313`
- **Category**: Performance
- **Severity**: Medium

```cpp
const std::string type = ascii_lowercase(node.attribute("type"));
```

`ascii_lowercase` (`form_control.cpp:21`) constructs a copy of the attribute and mutates it. `form_control_kind`
is invoked from nearly every form helper — `is_form_control`, `is_disabled_form_control`,
`is_text_entry_control`, `form_control_display_text`, `form_control_value`, `validate_form_control`, and on
every keystroke path via `append_text_to_control` — so a single keypress can allocate the lowercased type
string several times. The comparisons that follow are all against short lowercase literals.

**Fix**: compare the raw attribute case-insensitively against the literals (the file already has
`ascii_starts_with_case_insensitive`-style helpers) or return an enum cached on `FormControlState`.

### M7. Form-control traversal helpers each walk the whole subtree, sometimes twice per operation

- **File**: `src/render_core/form_control.cpp:122`, `form_control.cpp:139`, `form_control.cpp:226`, `form_control.cpp:479`
- **Category**: Performance
- **Severity**: Medium

`count_options` (`122`), `first_selected_option_index` (`139`), and `option_index_by_value` (`226`) are each
separate full-tree walks over the same `<select>` subtree. A single select operation pays several:
`set_form_control_selected_index` calls `count_options` (`588`) then `option_at` (`601`);
`activate_form_control` calls `count_options` (`479`) then `set_form_control_selected_index` which calls
`count_options` again. Each helper re-allocates its own worklist vector.

**Fix**: gather option pointers into a reusable scratch vector once per operation and index into it.

### M8. `validate_form` re-scans every radio group, making validation quadratic in control count

- **File**: `src/render_core/form_submission.cpp:62`, `form_submission.cpp:68`
- **Category**: Performance
- **Severity**: Medium

`radio_group_checked` (`form_submission.cpp:62`) walks the entire form subtree for each radio control being
validated, so `validate_form` (`form_submission.cpp:227`) over a form with *N* radios is O(N) subtree walks,
each O(subtree). The `reported_radio_groups` vector only deduplicates *reporting*, not the scan
(`form_submission.cpp:68` still scans per control).

**Fix**: scan the form once collecting radio groups by name into a map, then validate against that map.

### M9. Text normalization walks all ancestors on every call, and layout + paint both call it

- **File**: `src/render_core/text_normalization.cpp:63`, `text_normalization.cpp:79`, `text_normalization.cpp:86`
- **Category**: Performance
- **Severity**: Medium

`normalized_render_text` (`text_normalization.cpp:79`) calls `preserves_dom_text_whitespace`
(`text_normalization.cpp:63`), which walks `node.parent` to the root on each invocation. `layout_text_box`
calls `transformed_render_text` (`layout.cpp:1048`) and the paint pass calls it again for the same node, so
the ancestor walk and the normalization run twice per text node per frame with no caching. (The
already-reviewed `text_backend.cpp` / `text_layout_reuse.cpp` findings cover the measure/hash duplication;
this is the normalization half reached from the remaining files.)

**Fix**: cache the normalized string (or at least the `preserves_dom_text_whitespace` boolean) on the node,
invalidated by the existing dirty flags.

---

## LOW severity

### L1. Dead ternary: both `else` branches of `probe_height` are `0`

- **File**: `src/render_core/layout.cpp:1295`
- **Category**: Readability
- **Severity**: Low

```cpp
const int probe_height = use_basis ? child.style.flex_basis : flexible_zero_basis ? 0 : 0;
```

The nested conditional resolves to `flexible_zero_basis ? 0 : 0` — the second condition is evaluated and
discarded, so `flexible_zero_basis` is dead here. If it was meant to select a different height the intent is
lost; if not, the ternary should collapse to `use_basis ? child.style.flex_basis : 0`.

**Fix**: collapse to `use_basis ? child.style.flex_basis : 0`, or restore the intended third branch.

### L2. Flex-row container height uses border-box values where the column path uses content-box

- **File**: `src/render_core/layout.cpp:1495`
- **Category**: Implementation
- **Severity**: Low

The row path derives the stretch height from raw `box.style.height` / `box.style.min_height` (border-box),
while the column path uses `specified_content_height` / `specified_content_min_height`. Under
`box-sizing: border-box` the two disagree, so stretched row children can be sized from the wrong reference
box.

**Fix**: route the row path through the same `specified_content_*` accessors as the column path.

### L3. `add_block` returns a reference into `blocks_` that is invalidated by later pushes

- **File**: `src/render_core/arena.cpp:112`, `arena.cpp:118`
- **Category**: Implementation
- **Severity**: Low

`MonotonicArena::add_block` returns `blocks_.back()` by reference (`arena.cpp:118`), and `allocate` writes
`block.used` through it after the `push_back` (`arena.cpp:58`). It happens to be safe today because the
caller is the one that just pushed, but any future `add_block` call between the push and the write dangles
the reference. Also note `allocate` linearly scans blocks from `next_block_index_`
(`arena.cpp:44`), which is O(blocks) per allocation once the arena has fragmented into many blocks.

**Fix**: return an index (or a `Block*` plus re-fetch) rather than a reference, and add a comment that the
reference is only valid until the next `blocks_` mutation.

### L4. Duplicated percent helpers `percent_of_area` and `area_for_percent`

- **File**: `src/render_core/dirty_region.cpp:101`, `dirty_region.cpp:110`
- **Category**: Readability
- **Severity**: Low

Two adjacent functions implement the same percent-of-area math with subtly different rounding
(`percent_of_area` at `101` used at `dirty_region.cpp:599`; `area_for_percent` at `110` used at
`dirty_region.cpp:494` and `515`). Having both invites the reader to wonder whether the difference is
intentional.

**Fix**: collapse into one helper with a documented rounding direction, or add a comment stating why the two
differ.

### L5. `merge_overlapping_rects` restarts a pairwise scan until fixpoint

- **File**: `src/render_core/dirty_region.cpp:46`
- **Category**: Performance
- **Severity**: Low

`merge_overlapping_rects` (`dirty_region.cpp:46`) uses an outer `while` that re-scans all pairs after each
merge, giving quadratic-plus behavior; it is bounded by the early-out at `dirty_region.cpp:47`
(`rects.size() > kMaxPairwiseMergeRects`). It is called at `dirty_region.cpp:536` and `542` on every
coalescing path.

**Fix**: merge in a single pass using a union of bounding boxes when the rect count exceeds a small
threshold, or sort by x and merge overlapping neighbors directly.

### L6. `resolved_transform` re-parses the CSS transform string on every call

- **File**: `src/render_core/animation_invalidation.cpp:151`, `animation_invalidation.cpp:155`
- **Category**: Performance
- **Severity**: Low

`resolved_transform` (`animation_invalidation.cpp:151`) calls `parse_css_transform_2d(transform_source, …)`
(`155`) each time. `collect_animation_rects_iterative` calls it twice per animated node
(`animation_invalidation.cpp:263` and `264`) and `expand_for_paint_effects` runs twice per node as well
(`265`, `269`). The transform string was already parsed once when the timeline sampled the animation.

**Fix**: have `StyleOverride` carry the parsed `Transform2D` alongside the serialized string so invalidation
does not re-parse.

### L7. Per-frame `unordered_map` construction in `build_override_index`

- **File**: `src/render_core/animation_invalidation.cpp:135`, `animation_invalidation.cpp:306`
- **Category**: Performance
- **Severity**: Low

`build_override_index` (`animation_invalidation.cpp:135`) builds lookup maps from the override vectors, and
`animation_rects_between` calls it twice per frame (`animation_invalidation.cpp:306` and `307`). For the
common small-override case a hash map costs more than the linear scan it replaces.

**Fix**: keep a linear scan for small override counts (the vectors are typically tiny) and only build the
map above a threshold.

### L8. Transform re-serialized per sampled animation per frame

- **File**: `src/render_core/animation_timeline.cpp:364`, `animation_timeline.cpp:416`
- **Category**: Performance
- **Severity**: Low

Both the transition path (`animation_timeline.cpp:364`) and the keyframe path (`416`) build the transform as
`serialize_css_transform_2d(mix_transform(active.from_transform, active.to_transform, eased))` — mixing a
struct back into a string every sample. The downstream invalidation then re-parses it (L6), so a single
animated transform makes a string round-trip per frame per node.

**Fix**: carry the `Transform2D` in the override and serialize only at the boundary that truly needs a
string.

### L9. `retain_keyframe_animations` is O(keyframes x keys)

- **File**: `src/render_core/animation_timeline.cpp:326`
- **Category**: Performance
- **Severity**: Low

`retain_keyframe_animations` (`animation_timeline.cpp:326`) does a nested `std::find_if` over `keys` for
every active keyframe animation. With many animations the retain pass is quadratic.

**Fix**: sort `keys` once and binary-search, or build a small set keyed by `(node, name)`.

### L10. Parallel switch tables in `frame_update.cpp` must be kept in sync by hand

- **File**: `src/render_core/frame_update.cpp:76`, `frame_update.cpp:110`
- **Category**: Readability
- **Severity**: Low

`frame_update_reason_name` (`frame_update.cpp:76`) and `frame_update_reason_index` (`110`) are two
hand-written switches over the same `FrameUpdateReason` enum, plus the enum's own ordering. Adding a new
reason requires three coordinated edits; missing one silently misattributes statistics
(`frame_update.cpp:156`, `199`, `217`, `222` index by these). The `None` case maps to name "none" while the
index mapping is positional, so the coupling is implicit.

**Fix**: define a single `constexpr` table of `{FrameUpdateReason, const char*}` and derive both the name
lookup and the index from it.

### L11. Diagnostic detail strings are built eagerly even when diagnostics are disabled

- **File**: `src/render_core/layout.cpp:85`, `layout.cpp:1071`
- **Category**: Performance
- **Severity**: Low

`text_overflow_detail` (`layout.cpp:85`) builds a `std::ostringstream` plus a `dom_node_path` (truncated to
160 chars) and is called from `layout_text_box` at `layout.cpp:1071` unconditionally, then passed to
`report_diagnostic`, which discards it when no sink is attached. Text overflow is a per-text-box event, so
on a text-heavy page this is per-box string building that is almost always thrown away.

**Fix**: guard the call site with a cheap check (e.g. `if (diagnostics_enabled())`) or make
`report_diagnostic` take a lazy formatter.

### L12. Duplicated rect helpers copy-pasted across the invalidation/dirty/hit-test files

- **File**: `src/render_core/embedded_framebuffer.cpp:9`, `dirty_region.cpp`, `animation_invalidation.cpp`, `display_invalidation.cpp`, `hit_test.cpp`
- **Category**: Readability
- **Severity**: Low

`empty_rect` / `intersect_rect` / `union_rect` / `expand_and_clip_rect` are re-defined as file-local statics
in `embedded_framebuffer.cpp:9` and its siblings, each with slightly different parameter names and edge
handling. A bug fixed in one copy will not propagate. `embedded_framebuffer.cpp:13 intersect_rect` is
representative.

**Fix**: move the shared rect predicates into a single `geometry` helper header and delete the local copies.

### L13. Repeated derived-value recomputation in the inline-block shrink-to-fit path

- **File**: `src/render_core/layout.cpp:952`, `layout.cpp:981`, `layout.cpp:996`
- **Category**: Performance
- **Severity**: Low

In the shrink-to-fit block, `max_child_width` is computed from the block-flow lambda
(`layout.cpp:952`, `974`) and then overwritten at `layout.cpp:989`. Separately, `min_child_x` is computed in
one loop (`981`, `985`) and then recomputed in a second loop (`996`, `999`) using a different helper
(`safe_edge` vs `bounded_subtract`). The two passes over the same children duplicate work and the differing
helpers make it hard to tell whether the results are meant to agree.

**Fix**: compute `min_child_x` once and reuse it for both the width and the horizontal shift; delete the
superseded `max_child_width` assignment.

### L14. Fresh per-box `child_work` vector in `build_layout_tree`

- **File**: `src/render_core/layout.cpp:860`
- **Category**: Performance
- **Severity**: Low

`build_layout_tree` allocates `std::vector<PendingObject> child_work` and reserves it
(`layout.cpp:860`, `861`) for *every* render object visited, fills it, then reverses it into `pending`
(`883`). This is one heap allocation per box per tree build, purely to reverse iteration order.

**Fix**: push the already-reversed children directly onto `pending` (the tree build is a DFS stack, so the
order can be achieved without a second vector).

### L15. `bounded_non_negative_multiply` is marked `[[maybe_unused]]` but is used throughout

- **File**: `src/render_core/layout.cpp:39`
- **Category**: Readability
- **Severity**: Low

`bounded_non_negative_multiply` is declared `[[maybe_unused]]` at `layout.cpp:39` yet is called at
`layout.cpp:573`, `591`, `606`, `1103`, `1322`, `1468`, `1558`, `1627`, `1628`, `1630`, `1684`. The attribute
is stale and misleading.

**Fix**: remove `[[maybe_unused]]`.

### L16. `bounded_non_negative_multiply` and the duplicated `resolve_percent` in `resolved_content_width`

- **File**: `src/render_core/layout.cpp:104`, `layout.cpp:123`
- **Category**: Performance
- **Severity**: Low

`resolved_content_width` (`layout.cpp:104`) computes `resolve_percent(...)` once for the content-box case
and then again on the border-box branch (`123`) for the same value. Cheap, but it is the same "recompute a
derived value per box" shape as L13.

**Fix**: hoist the `resolve_percent` result into a local used by both branches.

### L17. `layout_positioned_children` parameter is misnamed and mis-sizes fixed boxes

- **File**: `src/render_core/layout.cpp:1185`, `layout.cpp:1200`
- **Category**: Implementation
- **Severity**: Low

The parameter is named `viewport_width` (`layout.cpp:1185`) but receives a containing-block width. Inside,
fixed boxes set `area_width = viewport_width` (`layout.cpp:1200`) while `area_height` is left `0`, so a
`position: fixed` box anchored with `inset_bottom` cannot resolve its height correctly. The `resolve_percent`
for width is also duplicated on the border-box branch here, matching L16.

**Fix**: pass the actual viewport extent for fixed boxes (or rename the parameter and document the
containing-block semantics), and supply the viewport height for the fixed case.

### L18. Grid occupancy bookkeeping is guarded but fragile at the budget fallback

- **File**: `src/render_core/layout.cpp:666`, `layout.cpp:720`, `layout.cpp:747`
- **Category**: Implementation
- **Severity**: Low

`mark_grid_item_occupied` (`layout.cpp:720`) indexes `state.occupied[r]` using `placement.column`, which
defaults to `0` when the column search is exhausted. The rows are kept in range by `ensure_grid_rows`
bounded by `kMaxGridRows` and by the budget fallback path (`layout.cpp:747`), so this is currently safe, but
the invariant depends on a distant bound rather than a local check at the write site.

**Fix**: assert/clamp the row and column at the point of write in `mark_grid_item_occupied`.

### L19. Per-box `Style` copy in `apply_styles_iterative`

- **File**: `src/render_core/style_repaint.cpp:150`, `style_repaint.cpp:163`
- **Category**: Performance
- **Severity**: Low

`apply_styles_iterative` (`style_repaint.cpp:150`) copies a full `Style` (a large struct with several
`std::string`s and vectors) per node visited. On a large tree this is a per-box allocation burst during the
paint-prepare pass.

**Fix**: move the style in or apply fields in place rather than copying the struct.

### L20. Override lookup is a linear scan per call in `override_for`

- **File**: `src/render_core/animation_timeline.cpp:124`
- **Category**: Performance
- **Severity**: Low

`override_for` (`animation_timeline.cpp:124`) linearly scans `overrides` for a matching `node` on every
sampled animation (`animation_timeline.cpp:348`, `401`). A node with several animated properties triggers a
scan per property.

**Fix**: build a `node -> index` map for the duration of the sample pass, or sort overrides by node pointer
since node addresses are stable and monotone within a pass.

---

## Files that were clean

- `src/render_core/frame_loop.cpp` — short, no issues found.
- `src/render_core/text_repaint.cpp` — thin, no issues found.
- `src/render_core/pipeline_statistics.cpp` — counters only; no issues found.
- `src/render_core/text_adapter.cpp` — no issues found.
- `src/render_core/event.cpp` — no issues found.
- `src/render_core/document_style.cpp` — no issues found.
- `src/render_core/document_script.cpp` — no issues found.
- `src/render_core/dom_owner.cpp` — no issues found.
- `src/render_core/bitmap_font_resource.cpp` — bounds checks are thorough; no issues found.
- `src/render_core/html_parser.cpp` — thin wrapper; no issues found.
- `src/render_core/canvas2d_disabled.cpp` — no-op stubs; no issues found.
- `src/render_core/form_submission_disabled.cpp` — no-op stubs; no issues found.

## Notes on files reviewed but not reported

- `bitmap_font.cpp`: the glyph blit loops are per-glyph-pixel by construction, not per-bounding-box, so the
  H1/H2/H3 pattern does not apply. `draw_missing_glyph` (`bitmap_font.cpp:214`) blends each stroke edge pixel
  individually rather than using a rect fill, but it is a fallback path for absent glyphs, so it was not
  raised.
- `canvas2d.cpp`: the 2D canvas fill/stroke paths are per-pixel by necessity; `fill_polygon`
  (`canvas2d.cpp:650`) reuses `surface.fill_intersections` rather than allocating per scanline, which is the
  right pattern. No finding raised.
- `embedded_framebuffer.cpp`: the format-conversion loops are inherently per-pixel. The only finding is the
  duplicated rect helpers (L12).
- `scroll_blit.cpp`, `hit_test.cpp`, `input.cpp`: reviewed; the scrolling, hit-testing, and input dispatch
  paths did not yield findings above the reporting bar.
