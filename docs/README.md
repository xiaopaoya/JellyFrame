# JellyFrame Documentation Index

This directory contains technical documents: module behavior, supported subsets,
interfaces and host contracts. These docs should stay close to the code.

Active project/process documents now live in
[../project_docs/README.md](../project_docs/README.md).

## First-Time Reading Order

1. [../HOW_TO_START.md](../HOW_TO_START.md)
2. [engine_architecture.md](engine_architecture.md)
3. [developer_capability_matrix.md](developer_capability_matrix.md)
4. [../project_docs/project_status.md](../project_docs/project_status.md)
5. The module document for the area you want to change.

## Technical Documents

### Core Pipeline

- [engine_architecture.md](engine_architecture.md): browser-like layers and
  current tradeoffs.
- [html_tokenizer_scope.md](html_tokenizer_scope.md): tokenizer subset,
  recovery rules and intentionally dropped states.
- [html_tree_builder_scope.md](html_tree_builder_scope.md): DOM tree-building
  subset and low-end device limits.
- [html_parser_architecture.md](html_parser_architecture.md): parser structure,
  naming and performance rules.
- [css_parser_scope.md](css_parser_scope.md): CSS syntax, at-rule and selector
  subset.
- [cssom_scope.md](cssom_scope.md): cascade and computed-style policy.
- [render_tree_scope.md](render_tree_scope.md): render tree construction and
  layout-facing rules.
- [layer_tree_scope.md](layer_tree_scope.md): layer reasons, clipping and
  degradation policy.
- [software_renderer_scope.md](software_renderer_scope.md): CPU renderer,
  display commands and deliberate cuts.

### Interaction And Runtime

- [events_scope.md](events_scope.md): hit testing, DOM-style event dispatch and
  input cuts.
- [scripting_scope.md](scripting_scope.md): current JerryScript support and
  unsupported browser APIs.
- [run_loop_contract.md](run_loop_contract.md): host run-loop order, dirty flags
  and repaint planning.

### Host, Embedded And Text

- [host_abstraction.md](host_abstraction.md): thin host boundary for resources,
  time, framebuffer, text and budgets.
- [app_packaging.md](app_packaging.md): M11 app package format, manifest and
  resource-bundle workflow.
- [embedded_hal_api.md](embedded_hal_api.md): board-side API checklist.
- [embedded_framebuffer_backend.md](embedded_framebuffer_backend.md): caller-owned
  framebuffer conversion and flush contract.
- [text_backend.md](text_backend.md): text measurement/painting API and font
  workflow.
- [embedded_optimization_notes.md](embedded_optimization_notes.md): current
  optimization choices and benchmark baseline.
- [porting_work_guide.md](porting_work_guide.md): staged board-port work guide.

### Developer-Facing Feature Contract

- [developer_capability_matrix.md](developer_capability_matrix.md): the primary
  can-do/cannot-do contract.
- [versioning.md](versioning.md): versioning and release discipline.

## Bilingual Maintenance

English docs use the base filename. Chinese docs use `_zh`, for example
`engine_architecture.md` and `engine_architecture_zh.md`. User-facing,
architecture, status, release and onboarding docs should be updated in both
languages in the same change.
