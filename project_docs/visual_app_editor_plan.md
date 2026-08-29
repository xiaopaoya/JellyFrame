# JellyFrame Visual App Editor Plan

> Updated: 2026-08-29; status: feasibility prototype complete, Stage 1 active.

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

The promotion candidate requires Stage 1, Stage 2 source-conflict protection,
and a reliable save-to-real-runtime handoff. It must be demonstrated from an
independent blank App workspace through check and device deploy. Arbitrary HTML
round-tripping, a third-party marketplace, a Figma-like free canvas, full CSS,
and generated business JavaScript are explicit non-goals.

