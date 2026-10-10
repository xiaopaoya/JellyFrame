# JellyFrame Visual App Editor Plan

> Last updated: 2026-10-07; Applies to: 0.6.0-dev; status: Stage 1/2 acceptance pending, Stage 3 bridge implemented, Stage 4 active.

## Wearable-first Checkpoint

Extension 0.4.74 adds button/text/list content alignment, control font sizes and
horizontal/vertical padding, explicit container alignment axes, and vertical
overflow controls. Four wearable recipes now precede the generic palette:
function list, metric screen, round action, and notification detail. They expand
into ordinary editable nodes; their buttons do not implicitly implement routing.
Bottom navigation remains available in the panel-device group.

Round function lists and notifications use an inset scroll container. Insets are
chosen at insertion from the viewport shape and remain editable. Changing the
viewport does not rewrite existing layouts or imply responsive target overrides.
Browser tests cover property editing, keyboard interaction and preview/drop
geometry. Native tests exercise twelve recipe/viewport combinations, nine text
alignment cases and scrolling, with zero pipeline diagnostics. Device acceptance
is separate; these are desktop results.

Next slices, in order:

1. Bounded page collection, stable page IDs, active page, previous/next commands
   and indicators, with generated behavior isolated from author scripts. The
   model is now format version 4: every page owns a bounded container root, while old
   version 1-3 models are migrated by cloning their former root for additional
   pages. The editor can switch the active page, edit its tree, and export the
   configured initial page on launch. Multi-page HTML exports retain every page
   and include a generated classic-script controller inside the managed body
   region. Buttons expose `pageAction` (none/previous/next). Author scripts can
   call `window.JellyFramePages.current/go/next/previous`. The existing Save & run
   command opens the interactive desktop Runtime in a separate VS Code tab.
   Multi-page saving requires `runtime.script: classic`; single-page output
   remains script-free. IDs and the 128-node limit apply across the page set.
   Indicator metadata is retained, but indicator painting is still pending.
2. Horizontal swipe navigation with direction locking, cancellation and control
   priority, followed by device touch testing without photographic pixel diffs.
   The model now also carries a disabled-by-default swipe contract with bounded
   threshold, cancellation distance, edge behavior and child-control priority;
   generated Runtime handlers use the supported pointer coordinate contract:
   pointerdown starts a sequence, mousemove samples it while the button is held,
   and pointerup completes it. They intentionally do not depend on TouchList,
   touchmove or touchcancel, which are not part of JellyFrame's native event
   contract. Vertical cancellation persists through the sequence; child
   controls retain priority.
   The shared model module now exposes side-effect-free swipe classification
   and page-index resolution for desktop replay and future touch hosts. These
   functions cover disabled gestures, direction cancellation, threshold,
   child-control priority, clamped edges and wrapped edges.
   CSS scroll-snap is not supported and must not stand in for this implementation.
   `tests/tool_regression/visual_editor_gesture_replay.js` now provides a
   deterministic JSON replay for the contract; it is not a Runtime or hardware
   result.
3. Range controls, icon/text function rows, label/value/action settings rows and
   richer metrics, gated on actual Runtime support and touch/font budgets.
4. Target-specific layouts, round safe-area guides, font coverage/size integration
   and packaged icons.

The Chinese plan contains the detailed acceptance checkpoint and boundaries.

### Runtime Acceptance, 2026-10-07

`tests/tool_regression/visual_editor_pages_runtime_tests.js` exercises exported
navigation code and optionally the scripting shell. Trace assertions require
only the first page on initial paint, the second page after Next, and the first
after Back. Sequence tests cover threshold, pointer cancellation, child priority
and vertical cancellation followed by horizontal movement.

Hardware execution remains pending. Use a matching scripting developer image
and provider, a two-page package with previous/next buttons and a range control,
and the available 172x320/round-300 targets. Verify launch, button navigation,
horizontal swipe, sub-threshold movement, vertical scrolling, pointer cancellation,
child-control drag, edge clamping and repeat after restart. Record package/image
identity, script errors and input/frame telemetry. Visual inspection is sufficient
for unexpected page changes, clipping or stuck content; photographic pixel
comparison is not required. All cancelled gestures must leave the page unchanged,
and each completed gesture must cause at most one transition.

The editor is a constrained, code-friendly JellyFrame App designer embedded in
VS Code. It is not a full-browser page builder. It emits readable HTML/CSS,
leaves JavaScript author-owned, and hands authoritative rendering to the real
desktop shell.

The design adopts the useful boundaries demonstrated by GrapesJS (model as
source of truth; canvas as a view), Craft.js (serializable node actions,
connectors and history), Puck (declarative component fields and slots),
Webstudio (Navigator and viewport-first workflow), and LowCodeEngine (a future
material/setter/schema split). It does not copy third-party code or adopt their
full-browser style systems, React runtime output, remote material markets or
enterprise plugin stacks.

## Target Experience

- One compact command bar for save state, history, target viewport, Design /
  Runtime mode, save and run.
- Left-side Components and Navigator views; the Navigator is the reliable way
  to select and restructure deep trees.
- A resizable device canvas with insertion indicators, selection chrome,
  breadcrumbs and target shape/size.
- A grouped inspector for Content, Layout, Appearance and Interaction using
  typed controls rather than unbounded text fields.
- A small status surface for source conflict and actionable diagnostics, not a
  growing raw log.
- A declarative component registry owns defaults, fields, placement rules,
  design view, validation and HTML serialization.
- The sidecar model and generated HTML/CSS carry a generation digest. External
  edits cause an explicit reload/diff/overwrite decision.
- Design mode is fast but approximate. Runtime mode reuses the existing desktop
  shell frame/input/lifecycle protocol and remains authoritative.

## Stages

1. **Editor shell and structure operations:** polished layout, Components /
   Navigator, before/after/inside drops, keyboard operations, resizable panels,
   responsive VS Code UI, and extracted webview assets.
2. **Declarative fields and source consistency:** component registry, typed
   setters, model migration, generated-region digest, conflict handling and
   transactional failure tests.
3. **Real Render Core loop:** Design/Runtime modes, existing shell session
   reuse, bounded checks and diagnostics mapped to nodes or source locations.
4. **Small, high-value materials:** supported primitives, 4-8 transparent
   recipes, package assets, target-specific overrides and event-ID guidance.
5. **Trial-driven beta candidates:** routes, reusable subtrees, first-party
   material protocol, bounded import and service binding only after an RFC.

Stage 2 progress (2026-08-30): the first slice is implemented. Models migrate
from v1 to v2, the palette receives a serializable host-provided registry,
successful saves record HTML/CSS generated-region digests, and external saves
update an explicit conflict notice; replacing a generated region requires
confirmation. Three-file writes now save in a defined order and restore all
original document contents when a save fails; fault-injected rollback tests
cover both recoverable and unrecoverable rollback failures. The generic
inspector now renders the registry-declared content, layout and appearance
fields, and the Webview and host model validator apply the same typed-setter
baseline for numbers, enums, lengths, text, colors and package resources. The
registry now also carries a validated renderer key consumed by both the design
canvas and source serializer; either side fails clearly when a node has no
renderer. The runtime/source serializer remains the authoritative renderer.
Full renderer capability parity and extension points remain open, so Stage 2
is not closed.

The promotion candidate requires Stage 1, Stage 2 source-conflict protection,
and a reliable save-to-real-runtime handoff. It must be demonstrated from an
independent blank App workspace through check and device deploy. Arbitrary HTML
round-tripping, a third-party marketplace, a Figma-like free canvas, full CSS,
and generated business JavaScript are explicit non-goals.

Stage 3 progress (2026-08-30): the embedded desktop-shell session already
uses the production frame, input, log and teardown bridge. Its first handoff
slice now isolates runtime logs and reports by session run, and automatically
invokes the existing `check` workflow after the shell exits, including an
explicit report-generation status. Save-and-run now forwards the model
viewport to the same embedded session, with host-side bounds validation;
ordinary menu debug retains App-default viewport behavior. Design/Runtime
selection sharing is now reflected in the visual editor through a lifecycle
state bridge (`running`, `reporting`, `stopped`). Editing also performs a
180-ms debounced bounded model check, validating at most 128 nodes and the
generated region without starting the full CLI or Render Core. Report
diagnostics are attributed to a visual node ID and property group only when a
stable ID is present; otherwise the extension marks them as unattributed.
Remaining work is focused regression and real desktop interaction acceptance.

Stage 4 progress (2026-08-30): the first material slice is implemented in the
registry and both editor renderers. It adds `divider`, `spacer`, a bounded
`select` with 1-6 options, a bounded `list` with 1-8 items, and a bounded
`navigation` row with 2-4 items. List-valued properties use an add/remove
control in the inspector and are validated for count, length and safe text
before entering the model. The generated output is ordinary `div`, `select`,
`ul`/`li` and `nav`/`button` markup with no private runtime component.
`select` remains subject to the App target's documented forms capability;
the editor does not claim to provide a browser-style unrestricted picker.
The canvas now also has a compact floating icon toolbar for history, fit,
zoom, structure and save. Three transparent recipes are now available in the
palette: status card, settings row and bottom navigation. Each expands to an
ordinary editable node subtree with fresh stable IDs. Settings rows use a
bounded switch, and device-oriented defaults use black or near-black surfaces.
The inspector now reports statically recognizable listeners for stable IDs from
package-local scripts and can copy a minimal event skeleton without changing
author JavaScript. Target-specific overrides remain open; Stage 4 is not
closed.

## Trial Follow-Up: 2026-09-28

Plugin `0.4.75` (`b08f42e2`) added canvas resize handles, a dedicated context
menu, and four expandable wearable recipes: icon detail row, value row, metric
block and segmented control (11 recipes total, eight wearable). The user reports
normal operation except that selecting a control resets its scrolling page.

`0.4.76` preserves scroll offsets by stable node ID when rebuilding the design
DOM, including selection, property edits, undo/redo and drop projections. Nested
viewports restore independently; browsers clamp offsets when content shrinks.
These offsets are transient editor view state, not model/source/history data.
Browser regressions are not device scrolling or performance acceptance.

Upcoming slices, in execution order:

1. **Direct manipulation and sizing contracts:** audit handles on all types,
   including replaced elements such as inputs/images; use registry bounds and
   number/length types consistently. Test cancellation/no-op history, keyboard
   menus, focus, 50/100/150% zoom, save/reopen and undo. Handles must not enter
   editable text; resizing flex children is not arbitrary positioning.
2. **Fine-grained fields and wearable materials:** verify Core support before
   adding bounded line height, wrapping/truncation and borders. Refine icon/title/
   subtitle/value rows and segmented-button spacing. Use package assets rather
   than assuming symbol glyph availability. Keep registry, validation, design,
   source and Runtime aligned; check three target sizes and old-model defaults.
3. **Page navigation model:** bounded pages, stable IDs, initial page and explicit
   previous/next actions. Review ownership of generated navigation code before
   implementation; never overwrite author scripts. Test deletion/reordering,
   migration and real Runtime input/lifecycle behavior before claiming navigation.
4. **Gestures and hardware:** direction lock, thresholds, cancellation and child
   input priority on the existing input path. Vertical scrolling, horizontal
   paging and slider drags must coexist. Issue a device test brief only after
   desktop replay passes; structured logs and visual inspection suffice, without
   photographic pixel-by-pixel comparisons.

This order supersedes starting immediately with paging. Later independent work
includes target-specific overrides, visible round-screen safe areas, package
icon/font browsing and reusable subtrees. Stage 4 and A2 evidence remain open;
recipe count alone is not an acceptance criterion.

The first slice of Stage 2's fine-field work is complete in plugin `0.4.78`
(2026-09-28): the registry, typed setter, design renderer and source renderer
now expose bounded `line-height`, `white-space: normal/nowrap`,
`text-overflow: clip/ellipsis`, and `border-width`/`border-color`. Missing fields
retain the old automatic line-height, breakable text and borderless defaults;
invalid enum, range and color values are rejected before entering the model.
Browser and Native Runtime coverage includes positive/negative models, all three
target viewports, a no-diagnostic fixture, and a browser-baseline alignment
conformance matrix for text, button, and list controls. Default alignment values
are materialized for new nodes and resolved for older models that omit them.
The next slice audits icon assets and touch sizing for detail rows; arbitrary CSS
remains out of scope.

The alignment parity fix is in plugin `0.4.79` (2026-09-28): new button,
text, and list nodes persist their documented alignment defaults, while legacy
models receive the same defaults during source and design rendering. The new
browser-versus-Native Runtime conformance test covers all three horizontal and
vertical alignment values without relying on hardware image comparison.

Slice 1 delivered in `0.4.77` (2026-09-28): selection chrome now lives in an
external layer, follows zoom/scroll and clips to scrolling ancestors without
entering controls or editable text. Resize uses registry types/bounds and the
element's box-sizing contract; flex constraints remain unchanged. Projected
trees do not mutate the model until release. No-op/return-to-origin, Escape,
pointer cancellation and focus loss preserve history/dirty state; cancellation
also preserves redo and pre-preview scroll. Menus support Shift+F10, arrows,
Home/End, Enter and Escape/Tab with focus restoration.

Automated coverage includes all 12 types, valid saved models and reopened
geometry, eight handles at 50/100/150% zoom, auto/percent conversion, cancellation,
redo, nested scroll and keyboard menus. Real VS Code interaction acceptance
awaits user retesting; no hardware claim is added. Next is slice 2's capability
audit and fine-grained fields, not page/gesture implementation yet.

### Scenario review slice (2026-09-29)

Three scripting-enabled wearable scenario packages were added under
`samples/apps/packages`: `jelly_alarm`, `jelly_workout`, and `jelly_messages`.
They pass package/responsive preflight for round-300, rect-320x240 and
rect-172x320, and each has an 18-frame deterministic desktop capture. The
editor registry also gained static `alarm-list`, `workout-dashboard` and
`message-inbox` recipes. These recipes intentionally stop at ordinary nodes;
page state, swipe gestures, host activity/audio/storage and business logic
remain author-owned.

The review is recorded in
`project_docs/wearable_scenario_render_editor_review_20260929.md`. The next
editor slice is now a bounded page model, followed by a gesture contract and
device replay requirements. Recipe count is not an acceptance criterion.
