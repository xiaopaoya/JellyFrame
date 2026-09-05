# JellyFrame Visual App Editor Plan

> Last updated: 2026-08-30; Applies to: 0.6.0-dev; status: Stage 3 complete, Stage 4 active.

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
